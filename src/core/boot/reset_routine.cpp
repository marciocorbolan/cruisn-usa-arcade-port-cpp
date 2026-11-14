/**
 * @file reset_routine.cpp
 * @brief Tradução linha a linha da rotina de RESET (ROM `0x4AD5`–`0x4BAE`).
 */
#include "core/boot/reset_routine.hpp"

#include <initializer_list>

#include "core/tms_float.hpp"
#include "core/vunit_hw.hpp"

namespace rea::core::boot {

namespace {
/// Endereço da palavra que guarda o SP inicial (RAM, copiada da ROM: 0x809C00).
constexpr uint32_t kInitialSpPtr = 0x0063;
/// RAM interna do TMS320C31: cópia da máscara IE usada pelos tratadores.
constexpr uint32_t kSavedIeMask = 0x99DF;   // @DP 0x80
/// RAM interna: cópia da porta 0x991050.
constexpr uint32_t kPort1050Copy = 0x99E1;  // @DP 0x80
/// Variáveis globais na RAM principal (DP = 0). Nomes provisórios.
constexpr uint32_t kGameMode = 0xC8C5;        ///< Modo/estado (&0xF; 1 = sem watchdog de quadro na INT0).
constexpr uint32_t kControlShadow = 0xC8C7;   ///< Cópia do registrador 0x994000.
constexpr uint32_t kVar_C939 = 0xC939;  ///< != 0 → `0x4E1B` abre o menu de teste.
constexpr uint32_t kVar_C8CE = 0xC8CE;        ///< Recebe o marcador 0x5A5A5A5A da ROM.
constexpr uint32_t kVar_C8C9 = 0xC8C9;  ///< Amostra bruta anterior (IN1<<16 | IN0, ativa em 0).
constexpr uint32_t kVar_C8E8 = 0xC8E8;  ///< Caso da atração/partida; o RESET grava −1.
constexpr uint32_t kVar_E5E8 = 0xE5E8;  ///< x da barra animada de "carregando" (`0xA3A5`); o RESET começa em 0xF0.
constexpr uint32_t kVar_E5E9 = 0xE5E9;  ///< Passo da barra de "carregando" (±1, inverte entre x = 240 e 300).
constexpr uint32_t kVar_E5CB = 0xE5CB;  ///< != 0: `0xA2F9` descomprime tudo de uma vez.
constexpr uint32_t kVar_A3A4 = 0xA3A4;  ///< != 0: chama `0xA3A5` (animação) no modo bloqueante.
constexpr uint32_t kFloatBlockPtr = 0x4F07;   ///< Ponteiro (= 0xC947) para 48 floats zerados.
constexpr uint32_t kMagicPtr = 0xB08E;        ///< 0x5A5A5A5A.
} // namespace

uint32_t ResetRoutine::run(Tms32031State& cpu) {
    // 04AD5  LDI $0002,IOF        — XF0 como saída, nível 0.
    cpu.iof = 0x0002;
    // 04AD6  SUBI R0,R0           — R0 = 0.
    cpu.r[0] = 0;
    // 04AD7  LDIU $0080,DP / STI R0,@($99DF) — zera a máscara IE salva.
    cpu.dp = 0x80;
    stDirect(cpu, kSavedIeMask, cpu.r[0]);
    // 04ADA  LDI $1068,R0 / STI R0,@($8064)  — wait states do barramento primário.
    cpu.r[0] = 0x1068;
    stDirect(cpu, 0x8064, cpu.r[0]);
    // 04ADC  LDIU $0098,DP / LDI 8,R0 / STI R0,@($0080) — registrador de vídeo 0x980080 = 8.
    cpu.dp = 0x98;
    cpu.r[0] = 0x0008;
    stDirect(cpu, 0x0080, cpu.r[0]);
    // 04ADF  LDI $0800,ST         — liga o cache de instruções (CE); GIE = 0.
    cpu.st = Tms32031State::kStCacheEnable;
    // 04AE0  LDIU 0,DP / LDI @($0063),SP — SP inicial (0x809C00, início do bloco RAM1 interno).
    cpu.dp = 0x00;
    cpu.sp = ldDirect(cpu, kInitialSpPtr);
    // 04AE2–04AE7  desliga interrupções.
    disableInterrupts(cpu);
    // 04AE8  R0 = 0 / STI R0,@($995020) — liga a proteção de escrita da CMOS.
    cpu.r[0] = 0;
    cpu.dp = 0x99;
    stDirect(cpu, 0x5020, cpu.r[0]);
    // 04AEB  AR0 = 0x809800; RPTS $7FF: STI R0,*AR0++ — zera 0x800 palavras da RAM interna.
    cpu.ar[0] = (0x0080u << 16) | 0x9800u;
    for (uint32_t i = 0; i <= 0x7FF; ++i) bus_.writeWord(cpu.ar[0]++, cpu.r[0]);
    // 04AF0  LDI @($991050),R0 / STI R0,@($8099E1) — guarda uma porta de entrada.
    cpu.dp = 0x99;
    cpu.r[0] = ldDirect(cpu, 0x1050);
    cpu.dp = 0x80;
    stDirect(cpu, kPort1050Copy, cpu.r[0]);
    // 04AF4  0x994000 = 0xFF       — LED, watchdog e reset do DCS em 1.
    cpu.dp = 0x99;
    cpu.r[0] = 0x00FF;
    stDirect(cpu, 0x4000, cpu.r[0]);
    // 04AF7  0x980040 = 0x200; 04AFA 0x980041 = 0x10E — controle de página de vídeo.
    cpu.dp = 0x98;
    cpu.r[0] = 0x0200;
    stDirect(cpu, 0x0040, cpu.r[0]);
    cpu.r[0] = 0x010E;
    stDirect(cpu, 0x0041, cpu.r[0]);
    // 04AFD  CALL $4ECC            — copia a tabela de timing de vídeo (12 regs em 0x980020).
    call(0x4ECC, cpu);
    // 04AFE  SUBI DP,DP / CALL $62D5
    cpu.dp = 0;
    call(0x62D5, cpu);
    // 04B00  AR1 = 0; AR2 = 0x4000<<8 = 0x400000; RC = 0x1000<<4 = 0x10000
    // 04B05  RPTB: R0 = *AR1++; *AR2++ = R0  (RPTB executa RC+1 vezes)
    cpu.ar[1] = 0x0000;
    cpu.ar[2] = 0x4000u << 8;
    cpu.rc = 0x1000u << 4;
    for (uint32_t i = 0; i <= cpu.rc; ++i) {
        cpu.r[0] = bus_.readWord(cpu.ar[1]++);
        bus_.writeWord(cpu.ar[2]++, cpu.r[0]);
    }
    cpu.rc = 0xFFFFFFFFu; // RC termina em −1 após o bloco.
    // 04B08–04B0E  NOPs + LDI $0022,IOF — XF0 e XF1 como saídas, nível 0.
    cpu.iof = 0x0022;
    // 04B0F  bus control = 0x470.
    cpu.dp = 0x80;
    cpu.r[0] = 0x0470;
    stDirect(cpu, 0x8064, cpu.r[0]);
    // 04B13  zera de novo a máscara IE salva.
    cpu.r[0] = 0;
    stDirect(cpu, kSavedIeMask, cpu.r[0]);
    // 04B16  SUBI DP,DP / CALL $6381
    cpu.dp = 0;
    call(0x6381, cpu);
    // 04B18  bus control = 0x470 (repetido pelo código original).
    cpu.dp = 0x80;
    cpu.r[0] = 0x0470;
    stDirect(cpu, 0x8064, cpu.r[0]);
    cpu.dp = 0;
    // 04B1C–04B23  desliga interrupções; recarrega SP.
    disableInterrupts(cpu);
    cpu.sp = ldDirect(cpu, kInitialSpPtr);
    // 04B24  R2 = 0; AR2 = 0x29; CALL $99F4
    cpu.r[2] = 0;
    cpu.ar[2] = 0x0029;
    call(0x99F4, cpu);
    // 04B27  BU $4B2B — pula 04B28–04B2A (R0 = 8 → @($8099E1)), que só é
    //        alcançado pelas rotinas de recuperação (ver runFrom4B28).
    return runFrom4B2B(cpu);
}

uint32_t ResetRoutine::runFrom4B28(Tms32031State& cpu) {
    // 04B28  R0 = 8; DP = 0x80; @($99E1) = 8 — finge a porta 0x991050 com o
    //        bit 3 ligado: no fim (04B96) o contador de auditoria 0x78 NÃO é
    //        incrementado numa reentrada.
    cpu.r[0] = 0x0008;
    cpu.dp = 0x80;
    stDirect(cpu, kPort1050Copy, cpu.r[0]);
    return runFrom4B2B(cpu);
}

uint32_t ResetRoutine::runFrom4B2B(Tms32031State& cpu) {
    // 04B2B  bus control = 0x470; desliga interrupções; recarrega SP.
    cpu.dp = 0x80;
    cpu.r[0] = 0x0470;
    stDirect(cpu, 0x8064, cpu.r[0]);
    cpu.dp = 0;
    disableInterrupts(cpu);
    cpu.sp = ldDirect(cpu, kInitialSpPtr);
    // 04B37  AR1 = 0x40; AR2 = (−0x4000 << 8) + 0x40 = 0xC00040 (barramento de 24 bits)
    // 04B3B  RC = 0x10000; RPTB: R0 = *AR2++; *AR1++ = R0
    //        Recopia o programa da ROM para a RAM, preservando os vetores 0x00–0x3F.
    cpu.ar[1] = 0x0040;
    cpu.ar[2] = ((static_cast<uint32_t>(-0x4000) << 8) + 0x40u) & 0xFFFFFFu;
    cpu.rc = 0x1000u << 4;
    for (uint32_t i = 0; i <= cpu.rc; ++i) {
        cpu.r[0] = bus_.readWord(cpu.ar[2]++);
        bus_.writeWord(cpu.ar[1]++, cpu.r[0]);
    }
    cpu.rc = 0xFFFFFFFFu;
    // 04B40  R0 = 0; paleta[0] (0x9E0000) = 0 — cor 0 = preto.
    cpu.r[0] = 0;
    cpu.dp = 0x9E;
    stDirect(cpu, 0x0000, cpu.r[0]);
    // 04B43  DP = 0; @($C939) = 0
    cpu.dp = 0;
    stDirect(cpu, kVar_C939, cpu.r[0]);
    // 04B45  CALL $4E20 / CALL $4E35
    call(0x4E20, cpu);
    call(0x4E35, cpu);
    // 04B47  @($C8C5) = 1 (modo); @($C8C7) = 0xFF (cópia do registrador de controle).
    cpu.r[0] = 0x0001;
    stDirect(cpu, kGameMode, cpu.r[0]);
    cpu.r[0] = 0x00FF;
    stDirect(cpu, kControlShadow, cpu.r[0]);
    // 04B4B  CALL $5029 / $4F32 / $4F7D
    call(0x5029, cpu);
    call(0x4F32, cpu);
    call(0x4F7D, cpu);
    // 04B4E  @($E5E8) = 0xF0; @($E5E9) = 1
    cpu.r[0] = 0x00F0;
    stDirect(cpu, kVar_E5E8, cpu.r[0]);
    cpu.r[0] = 0x0001;
    stDirect(cpu, kVar_E5E9, cpu.r[0]);
    // 04B52  Timer 0: controle = 0x201 (clock interno = CPU/2; ainda parado);
    //        período = 0xFFFFFFFF (contador livre usado como cronômetro).
    cpu.dp = 0x80;
    cpu.r[0] = 0x0201;
    stDirect(cpu, 0x8020, cpu.r[0]);
    cpu.r[0] = 0xFFFFFFFFu;
    stDirect(cpu, 0x8028, cpu.r[0]);
    cpu.dp = 0;
    // 04B58  LDF 0.0,R0; AR0 = @($4F07) (= 0xC947); RPTS $2F: STF R0,*AR0++
    //        48 floats zerados. Zero no formato TMS = 0x80000000 (expoente −128).
    const uint32_t zero_f = doubleToTms(0.0);
    cpu.r[0] = zero_f;
    cpu.ar[0] = ldDirect(cpu, kFloatBlockPtr);
    for (uint32_t i = 0; i <= 0x2F; ++i) bus_.writeWord(cpu.ar[0]++, zero_f);
    // 04B5C  @($C8CE) = @($B08E) (= 0x5A5A5A5A, marcador)
    cpu.r[0] = ldDirect(cpu, kMagicPtr);
    stDirect(cpu, kVar_C8CE, cpu.r[0]);
    // 04B5E  inicialização de subsistemas.
    call(0x95F5, cpu);
    call(0x95CB, cpu);
    call(0x4CEF, cpu);
    call(0x4E1B, cpu);
    call(0x4EE3, cpu);
    call(0x7F06, cpu);
    call(0x4EF9, cpu);   // liga o timer 0 (GO | HLD).
    // 04B65–04B6A  desliga interrupções.
    disableInterrupts(cpu);
    // 04B6B  CALL $9131; CALL $4C33 (liga interrupções); CALL $4EE3
    call(0x9131, cpu);
    enableInterrupts(cpu);
    call(0x4EE3, cpu);
    // 04B6E  IE = INT0 | INT3 (VBLANK + A/D); limpa IF pendentes.
    cpu.ie = Tms32031State::kIeInt0 | Tms32031State::kIeInt3;
    cpu.if_ = 0;
    // 04B70  CALL $4C33 (liga interrupções); CALL $7F18; CALL $4530
    enableInterrupts(cpu);
    call(0x7F18, cpu);
    call(0x4530, cpu);
    // 04B73  R2 = 0; AR2 = 0x29; CALL $99F4
    cpu.r[2] = 0;
    cpu.ar[2] = 0x0029;
    call(0x99F4, cpu);
    // 04B76  Cinco chamadas a $A3C2 com AR2 = @($B08F…$B093); as três últimas
    //        também ligam @($A3A4). Entre elas, CALL $4F8F e $4F9B.
    cpu.r[0] = 1;
    stDirect(cpu, kVar_E5CB, cpu.r[0]);
    cpu.ar[2] = ldDirect(cpu, 0xB08F);
    call(0xA3C2, cpu);
    cpu.r[0] = 1;
    stDirect(cpu, kVar_E5CB, cpu.r[0]);
    cpu.ar[2] = ldDirect(cpu, 0xB090);
    call(0xA3C2, cpu);
    call(0x4F8F, cpu);
    for (uint32_t ptr : {0xB091u, 0xB092u}) {
        cpu.r[0] = 1;
        stDirect(cpu, kVar_E5CB, cpu.r[0]);
        stDirect(cpu, kVar_A3A4, cpu.r[0]);
        cpu.ar[2] = ldDirect(cpu, ptr);
        call(0xA3C2, cpu);
    }
    call(0x4F9B, cpu);
    cpu.r[0] = 1;
    stDirect(cpu, kVar_E5CB, cpu.r[0]);
    stDirect(cpu, kVar_A3A4, cpu.r[0]);
    cpu.ar[2] = ldDirect(cpu, 0xB093);
    call(0xA3C2, cpu);
    // 04B8F  R2 = 0; AR2 = 0xF7; CALL $99F4
    cpu.r[2] = 0;
    cpu.ar[2] = 0x00F7;
    call(0x99F4, cpu);
    // 04B92  AR2 = 8; CALL $99E9; R1 = R0; CALL $910F
    cpu.ar[2] = 0x0008;
    call(0x99E9, cpu);
    cpu.r[1] = cpu.r[0];
    call(0x910F, cpu);
    // 04B96  se (@($8099E1) & 8) == 0: AR2 = 0x78; CALL $99DA
    cpu.dp = 0x80;
    cpu.r[0] = ldDirect(cpu, kPort1050Copy);
    cpu.dp = 0;
    cpu.r[0] &= 0x0008;
    if (cpu.r[0] == 0) {
        cpu.ar[2] = 0x0078;
        call(0x99DA, cpu);
    }
    // 04B9D  @($C8C9) = 0; @($C8E8) = −1
    cpu.r[0] = 0;
    stDirect(cpu, kVar_C8C9, cpu.r[0]);
    cpu.ar[2] = 0xFFFFFFFFu;
    stDirect(cpu, kVar_C8E8, cpu.ar[2]);
    // 04BA1  CALL $9503; CALL $4EE3
    call(0x9503, cpu);
    call(0x4EE3, cpu);
    // 04BA3  IE = INT0 | INT3 | máscara salva; limpa IF; liga interrupções.
    cpu.dp = 0x80;
    cpu.ie = (Tms32031State::kIeInt0 | Tms32031State::kIeInt3) | ldDirect(cpu, kSavedIeMask);
    cpu.dp = 0;
    cpu.if_ = 0;
    enableInterrupts(cpu);
    // 04BA9  AR2 = 0xFA; CALL $99E9; CMPI $28,R0; BNE $5006
    cpu.ar[2] = 0x00FA;
    call(0x99E9, cpu);
    if (cpu.r[0] != 0x28) return kExitCmosCheckFailed;
    // 04BAD  CALL $995C; BLO $4FEF
    call(0x995C, cpu);
    if (cpu.carry()) return kExitLowerThan;
    return kExitFallThrough;
}

} // namespace rea::core::boot
