/**
 * @file lzw_loader.cpp
 * @brief Tradução do descompressor LZW cooperativo (`0xA2B0`–`0xA3D6`).
 */
#include "core/gfx/lzw_loader.hpp"

#include "core/vunit_hw.hpp"

namespace rea::core::gfx {

using namespace lzw_vars;

namespace {

/// Bits de `[0x980082]` testados antes de mexer na RAM de texturas.
constexpr uint32_t kDmaBusy0 = 1u;
constexpr uint32_t kDmaBusy1 = 2u;  ///< Bit 1: segundo canal de DMA/blitter ocupado.

/**
 * @brief Laço `LDI @($0082),R0 / AND mask,R0 / BNE` com DP = 0x98.
 * @details O DMA não é emulado e a porta lê 0, então sai na 1ª leitura.
 *          Há um limite de segurança: o original esperaria para sempre.
 */
void waitDmaIdle(Tms32031State& cpu, MemoryBus& bus, uint32_t mask) {
    cpu.dp = 0x98;
    for (int guard = 0; guard < (1 << 20); ++guard) {
        cpu.r[0] = bus.readWord(vunit::kVideoStatus) & mask;
        if (cpu.r[0] == 0) break;
    }
    cpu.dp = 0;  // SUBI DP,DP
}

/**
 * @brief `0x9042`: salva AR0–AR7 e R0–R7 (PUSH/PUSHF).
 * @details A rotina tira o endereço de retorno da pilha com `POP BK`, por
 *          isso BK não é salvo e termina valendo o endereço de retorno.
 *          No port, a cópia local substitui a pilha.
 * @param return_pc Endereço seguinte ao `CALL $9042`.
 * @param cpu Registradores do TMS320C31 (entrada e saída da rotina).
 */
Tms32031State pushAll(Tms32031State& cpu, uint32_t return_pc) {
    cpu.bk = return_pc;
    return cpu;
}

/**
 * @brief `0x905C`: restaura AR0–AR7 e R0–R7; BK = endereço de retorno.
 * @note Os registradores de 40 bits só são guardados pela parte de 32 bits
 *       (ver Tms32031State); PUSH + PUSHF do mesmo Rn restauram esse valor.
 */
void popAll(Tms32031State& cpu, const Tms32031State& saved, uint32_t return_pc) {
    cpu.ar = saved.ar;
    cpu.r = saved.r;
    cpu.bk = return_pc;
}

/**
 * @brief Sequência inline `LSH R3,R0 / OR R0,IR1 / ADDI 8,R3 / CMPI 32,R3 / CALLGE $A2C9`.
 * @details Empacota o byte R0 na posição R3 da palavra IR1 (LSB primeiro).
 */
void outputByte(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[0] = tmsLsh(cpu.r[0], cpu.r[3]);
    cpu.ir1 |= cpu.r[0];
    cpu.r[3] += 8;
    if (static_cast<int32_t>(cpu.r[3]) >= 0x20) flushOutput(cpu, bus);
}

} // namespace

void readCode(Tms32031State& cpu, MemoryBus& bus) {
    // 0A2B0  ADDI3 AR7,AR6,R0 / CMPI 31,R0 / BGT $A2BA
    cpu.r[0] = cpu.ar[7] + cpu.ar[6];
    if (static_cast<int32_t>(cpu.r[0]) <= 0x1F) {
        // 0A2B3  R0 = *AR0 << AR7 (descarta os bits já lidos)
        cpu.r[0] = tmsLsh(bus.readWord(cpu.ar[0]), cpu.ar[7]);
        // 0A2B5  R1 = AR6 − 32; R0 >>= 32 − AR6 (fica com os AR6 bits do topo)
        cpu.r[1] = cpu.ar[6] - 0x20;
        cpu.r[0] = tmsLsh(cpu.r[0], cpu.r[1]);
        // 0A2B8  AR7 += AR6
        cpu.ar[7] += cpu.ar[6];
        return;
    }
    // 0A2BA  R1 = *AR0++ << AR7 (parte alta do código)
    cpu.r[1] = tmsLsh(bus.readWord(cpu.ar[0]++), cpu.ar[7]);
    // 0A2BC  R0 = 32 − AR7 (bits restantes na palavra); R2 = AR6 − R0 (bits na próxima)
    cpu.r[0] = 0x20 - cpu.ar[7];
    cpu.r[2] = cpu.ar[6] - cpu.r[0];
    cpu.ar[7] = cpu.r[2];
    // 0A2C1  R0 = *AR0 >> (32 − R2) (parte baixa)
    cpu.r[0] = bus.readWord(cpu.ar[0]);
    cpu.r[2] -= 0x20;
    cpu.r[0] = tmsLsh(cpu.r[0], cpu.r[2]);
    // 0A2C4  R1 >>= 32 − AR6; R0 |= R1
    cpu.r[2] = cpu.ar[6] - 0x20;
    cpu.r[1] = tmsLsh(cpu.r[1], cpu.r[2]);
    cpu.r[0] |= cpu.r[1];
}

void flushOutput(Tms32031State& cpu, MemoryBus& bus) {
    // 0A2C9  AR2 = [A2AF] + R7; R7++; *AR2 = IR1; IR1 = R3 = 0
    cpu.ar[2] = bus.readWord(cpu.direct(kBufferPtr)) + cpu.r[7];
    cpu.r[7] += 1;
    bus.writeWord(cpu.ar[2], cpu.ir1);
    cpu.ir1 = 0;
    cpu.r[3] = 0;
    // 0A2CF  CMPI 64,R7 / RETSLT
    if (static_cast<int32_t>(cpu.r[7]) < static_cast<int32_t>(lzw::kBufferWords)) return;

    // 0A2D1  [E5CD] += 64; R7 = 0
    cpu.r[0] = bus.readWord(cpu.direct(kSliceWords)) + lzw::kBufferWords;
    bus.writeWord(cpu.direct(kSliceWords), cpu.r[0]);
    cpu.r[7] = 0;
    // 0A2D5  PUSH AR4; AR4 = [A2AF]; AR2 = 0; PUSH ST, RC, RE, RS, R7, IE
    const uint32_t ar4 = cpu.ar[4];
    cpu.ar[4] = bus.readWord(cpu.direct(kBufferPtr));
    cpu.ar[2] = 0;
    const uint32_t st = cpu.st, rc = cpu.rc, re = cpu.re, rs = cpu.rs, r7 = cpu.r[7], ie = cpu.ie;
    // 0A2DE  IE = [0x8099DF]; DP = 0
    cpu.dp = 0x80;
    cpu.ie = bus.readWord(kSavedIeMask);
    cpu.dp = 0;
    // 0A2E1  R0 = 0x1000; R1 = 0x470; [0x808064] = R0 (wait states para a RAM de texturas)
    cpu.r[0] = 0x1000;
    cpu.r[1] = 0x0470;
    cpu.dp = 0x80;
    bus.writeWord(vunit::kBusControl, cpu.r[0]);
    // 0A2E5  RC = 63; R7 = −16; RPTB $A2EB (64×):
    //        *AR1++ = *AR4; *AR1++ = *AR4++ >> 16
    cpu.rc = 0x3F;
    cpu.r[7] = static_cast<uint32_t>(-16);
    cpu.rs = 0xA2E8;
    cpu.re = 0xA2EB;
    for (;;) {
        cpu.r[2] = bus.readWord(cpu.ar[4]);
        bus.writeWord(cpu.ar[1]++, cpu.r[2]);
        cpu.r[2] = tmsLsh(bus.readWord(cpu.ar[4]++), cpu.r[7]);
        bus.writeWord(cpu.ar[1]++, cpu.r[2]);
        if (static_cast<int32_t>(--cpu.rc) < 0) break;
    }
    // 0A2EC  R2 = *AR4 (leitura sem uso); [0x808064] = R1; DP = 0
    cpu.r[2] = bus.readWord(cpu.ar[4]);
    bus.writeWord(vunit::kBusControl, cpu.r[1]);
    cpu.dp = 0;
    // 0A2EF  POP IE, R7, RS, RE, RC, ST
    cpu.ie = ie;
    cpu.r[7] = r7;
    cpu.rs = rs;
    cpu.re = re;
    cpu.rc = rc;
    cpu.st = st;
    // 0A2F5  BUD $4C33 (RETIU); atrasadas: POP AR4, IR1 = 0, R3 = 0
    cpu.ar[4] = ar4;
    cpu.ir1 = 0;
    cpu.r[3] = 0;
    cpu.st |= Tms32031State::kStGie;  // RETIU religa as interrupções.
}

void saveContext(Tms32031State& cpu, MemoryBus& bus) {
    // 0A374  DP = 0; [E5CF] = AR0; AR0 = [A373]
    cpu.dp = 0;
    bus.writeWord(kSavedAr0, cpu.ar[0]);
    cpu.ar[0] = bus.readWord(kContextPtr);
    // 0A377  *AR0++ = AR1…AR7, R1…R7, RC, RE, RS, IR0, IR1, BK
    for (int i = 1; i <= 7; ++i) bus.writeWord(cpu.ar[0]++, cpu.ar[i]);
    for (int i = 1; i <= 7; ++i) bus.writeWord(cpu.ar[0]++, cpu.r[i]);
    for (uint32_t v : {cpu.rc, cpu.re, cpu.rs, cpu.ir0, cpu.ir1, cpu.bk}) bus.writeWord(cpu.ar[0]++, v);
}

void restoreContext(Tms32031State& cpu, MemoryBus& bus) {
    // 0A38C  AR0 = [A373]; AR1…AR7, R1…R7, RC, RE, RS, IR0, IR1, BK = *AR0++
    cpu.ar[0] = bus.readWord(cpu.direct(kContextPtr));
    for (int i = 1; i <= 7; ++i) cpu.ar[i] = bus.readWord(cpu.ar[0]++);
    for (int i = 1; i <= 7; ++i) cpu.r[i] = bus.readWord(cpu.ar[0]++);
    for (uint32_t* reg : {&cpu.rc, &cpu.re, &cpu.rs, &cpu.ir0, &cpu.ir1, &cpu.bk}) *reg = bus.readWord(cpu.ar[0]++);
    // 0A3A1  DP = 0; AR0 = [E5CF]
    cpu.dp = 0;
    cpu.ar[0] = bus.readWord(kSavedAr0);
}

void startDecompression(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 0A2F9  CALL $9042
    const Tms32031State saved = pushAll(cpu, 0xA2FA);
    // 0A2FA  espera [0x980082] & 1 == 0 e & 2 == 0; CALL $4EE3
    waitDmaIdle(cpu, bus, kDmaBusy0);
    waitDmaIdle(cpu, bus, kDmaBusy1);
    subs.call(0x4EE3, cpu, bus);
    // 0A305  [E5CA] = 1; [E5CC] = 0
    cpu.r[0] = 1;
    bus.writeWord(cpu.direct(kActive), cpu.r[0]);
    cpu.r[0] = 0;
    bus.writeWord(cpu.direct(kE5CC), cpu.r[0]);
    // 0A309  contexto inicial
    cpu.ar[0] = cpu.ar[4];                               // entrada (comprimida)
    cpu.ar[1] = cpu.ar[5];                               // saída (RAM de texturas)
    cpu.r[7] = 0;                                        // índice no buffer
    cpu.ar[3] = bus.readWord(cpu.direct(kDictPtr));      // dicionário
    cpu.ir0 = lzw::kSuffixOffset;
    cpu.ar[7] = 0;                                       // bits consumidos
    cpu.ir1 = 0;                                         // palavra em montagem
    cpu.r[3] = 0;                                        // posição do byte
    // 0A311  CALL $A374
    saveContext(cpu, bus);
    // 0A312  se [E5CB] != 0: CALL $A323 (descomprime tudo agora)
    cpu.r[0] = bus.readWord(cpu.direct(kBlocking));
    if (cpu.r[0] != 0) subs.call(0xA323, cpu, bus);
    // 0A315  CALL $905C; RETS
    popAll(cpu, saved, 0xA316);
}

void resumeDecompression(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 0A323  LDI @($E5CA),R0 / RETSEQ
    cpu.r[0] = bus.readWord(cpu.direct(kActive));
    if (cpu.r[0] == 0) return;
    // 0A325  espera o DMA; CALL $4EE3; [E5CD] = 0
    waitDmaIdle(cpu, bus, kDmaBusy0);
    waitDmaIdle(cpu, bus, kDmaBusy1);
    subs.call(0x4EE3, cpu, bus);
    cpu.r[0] = 0;
    bus.writeWord(cpu.direct(kSliceWords), cpu.r[0]);
    // 0A332  CALL $9042; CALL $A38C
    const Tms32031State saved = pushAll(cpu, 0xA333);
    restoreContext(cpu, bus);

    for (;;) {
        // 0A334  dicionário vazio: BK = 0x103, AR6 = 9, [E5CE] = 0x1FF
        cpu.bk = lzw::kFirstCode;
        cpu.ar[6] = lzw::kInitialBits;
        cpu.r[0] = 0x01FF;
        bus.writeWord(cpu.direct(kMaxCode), cpu.r[0]);
        // 0A338  primeiro código: R5 = anterior, R6 = primeiro byte da cadeia
        readCode(cpu, bus);
        cpu.r[5] = cpu.r[0];
        if (cpu.r[5] == lzw::kEnd) break;  // BEQ $A36E
        cpu.r[6] = cpu.r[5];
        outputByte(cpu, bus);

        bool cleared = false;
        while (!cleared) {
            // 0A342  R4 = próximo código
            readCode(cpu, bus);
            cpu.r[4] = cpu.r[0];
            if (cpu.r[4] == lzw::kEnd) break;  // BEQ $A36E
            if (cpu.r[4] == lzw::kClear) {
                // 0A317  CALL $5029 (watchdog)
                subs.call(0x5029, cpu, bus);
                // 0A318  [E5CD] < 0x800: limpa e continua (BLT $A334)
                cpu.r[0] = bus.readWord(cpu.direct(kSliceWords));
                if (static_cast<int32_t>(cpu.r[0]) < static_cast<int32_t>(lzw::kSliceLimit)) {
                    cleared = true;
                    continue;
                }
                // 0A31B  modo bloqueante: animação (se [A3A4]) e continua
                cpu.r[0] = bus.readWord(cpu.direct(kBlocking));
                if (cpu.r[0] != 0) {
                    cpu.r[0] = bus.readWord(cpu.direct(kProgressFlag));
                    if (cpu.r[0] != 0) subs.call(0xA3A5, cpu, bus);
                    cleared = true;
                    continue;
                }
                // 0A320  cede a vez: CALL $A374; CALL $905C; RETS
                saveContext(cpu, bus);
                popAll(cpu, saved, 0xA322);
                return;
            }
            if (cpu.r[4] == lzw::kGrow) {
                // 0A34A  AR6++ (código 1 bit mais largo)
                cpu.ar[6] += 1;
                continue;
            }
            // 0A34C  CMPI BK,R4 / BLTD $A355; atrasadas: DP = 0, AR4 = pilha, RC = 0
            const bool known = static_cast<int32_t>(cpu.r[4]) < static_cast<int32_t>(cpu.bk);
            cpu.dp = 0;
            cpu.ar[4] = bus.readWord(kStackPtr);
            cpu.rc = 0;
            if (known) {
                cpu.ar[5] = cpu.r[4];                    // 0A355
            } else {
                // 0A351  caso KwKwK (código ainda não definido): empilha o
                //        primeiro byte e expande o código anterior.
                bus.writeWord(cpu.ar[4]++, cpu.r[6]);
                cpu.rc = 1;
                cpu.ar[5] = cpu.r[5];
            }
            // 0A356  enquanto AR5 > 0xFF: empilha sufixo[AR5]; AR5 = prefixo[AR5]
            while (static_cast<int32_t>(cpu.ar[5]) > 0xFF) {
                cpu.ar[5] += cpu.ar[3];
                cpu.r[0] = bus.readWord(cpu.ar[5] + cpu.ir0);
                bus.writeWord(cpu.ar[4]++, cpu.r[0]);
                cpu.rc += 1;
                cpu.ar[5] = bus.readWord(cpu.ar[5]);
            }
            // 0A35F  R6 = R0 = AR5 (literal inicial); RPTB $A367 (RC + 1 vezes):
            //        emite R0; R0 = *--AR4 (a última volta lê 1 abaixo da pilha)
            cpu.r[6] = cpu.ar[5];
            cpu.r[0] = cpu.ar[5];
            cpu.rs = 0xA362;
            cpu.re = 0xA367;
            for (;;) {
                outputByte(cpu, bus);
                cpu.r[0] = bus.readWord(--cpu.ar[4]);
                if (static_cast<int32_t>(--cpu.rc) < 0) break;
            }
            // 0A368  prefixo[BK] = R5; sufixo[BK] = R6; BK++; R5 = R4
            cpu.ar[2] = cpu.ar[3] + cpu.bk;
            bus.writeWord(cpu.ar[2], cpu.r[5]);
            bus.writeWord(cpu.ar[2] + cpu.ir0, cpu.r[6]);
            cpu.bk += 1;
            cpu.r[5] = cpu.r[4];
        }
        if (!cleared) break;  // saiu por 0x100
    }
    // 0A36E  fim: [E5CA] = [E5CB] = 0; CALL $905C; RETS
    //        (bytes ainda no buffer, < 64 palavras, não são copiados, como no original)
    cpu.r[0] = 0;
    bus.writeWord(cpu.direct(kActive), cpu.r[0]);
    bus.writeWord(cpu.direct(kBlocking), cpu.r[0]);
    popAll(cpu, saved, 0xA372);
}

void loadAsset(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 0A3C2  PUSH AR4, AR5
    const uint32_t ar4 = cpu.ar[4], ar5 = cpu.ar[5];
    // 0A3C4  LDI @($E5CA),R0 / BEQ $A3D1
    cpu.r[0] = bus.readWord(cpu.direct(kActive));
    if (cpu.r[0] != 0) {
        // 0A3C6  ocupado: PUSH R2, AR0, AR4; AR4 = descritor; AR2 = [B12A] (0xA3D7);
        //        R2 = 0x820; CALL $A844 (cria o processo que espera e repete o pedido)
        const uint32_t r2 = cpu.r[2], ar0 = cpu.ar[0], ar4b = cpu.ar[4];
        cpu.ar[4] = cpu.ar[2];
        cpu.ar[2] = bus.readWord(cpu.direct(kQueueProcPtr));
        cpu.r[2] = 0x0820;
        subs.call(0xA844, cpu, bus);
        cpu.ar[4] = ar4b;
        cpu.ar[0] = ar0;
        cpu.r[2] = r2;
    } else {
        // 0A3D1  AR4 = *AR2++ (origem); AR5 = *AR2++ (destino); CALL $A2F9
        cpu.ar[4] = bus.readWord(cpu.ar[2]++);
        cpu.ar[5] = bus.readWord(cpu.ar[2]++);
        subs.call(0xA2F9, cpu, bus);
    }
    // 0A3D4  POP AR5, AR4; RETS
    cpu.ar[5] = ar5;
    cpu.ar[4] = ar4;
}

void registerRoutines(SubroutineDispatcher& d) {
    d.add(0xA2B0, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { readCode(c, b); });
    d.add(0xA2C9, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { flushOutput(c, b); });
    d.add(0xA374, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { saveContext(c, b); });
    // 0A3A5  animação de "carregando": apaga a barra vertical em x = [E5E8] (y 0x6F..0x74, cor 0),
    //        avança x por [E5E9] e redesenha com a cor 3; quica entre 240 e 300.
    d.add(0xA3A5, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        saveContext(c, b);
        auto bar = [&](uint32_t color) {
            c.r[6] = b.readWord(0xE5E8);
            c.ar[2] = c.r[6];
            c.r[3] = c.r[6];
            c.r[2] = 0x6F;
            c.rc = 0x74;
            c.rs = color;
            s.call(0xAC23, c, b);
        };
        bar(0);
        b.writeWord(0xE5E8, b.readWord(0xE5E8) + b.readWord(0xE5E9));
        bar(3);
        const int32_t x = static_cast<int32_t>(b.readWord(0xE5E8));
        if (!(x > 0xF0)) b.writeWord(0xE5E9, 1);
        if (!(x < 0x12C)) b.writeWord(0xE5E9, 0xFFFFFFFFu);
        restoreContext(c, b);
    });
    d.add(0xA38C, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { restoreContext(c, b); });
    d.add(0xA2F9, startDecompression);
    d.add(0xA323, resumeDecompression);
    d.add(0xA3C2, loadAsset);
}

} // namespace rea::core::gfx
