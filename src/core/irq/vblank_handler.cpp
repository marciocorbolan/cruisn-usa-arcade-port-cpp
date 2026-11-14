/**
 * @file vblank_handler.cpp
 * @brief Tradução linha a linha da INT0 (ROM `0x4C34`–`0x4CED`).
 */
#include "core/irq/vblank_handler.hpp"

#include <initializer_list>

#include "core/tms_float.hpp"
#include "core/vunit_hw.hpp"

namespace rea::core::irq {

using namespace vblank_vars;

namespace {
constexpr uint32_t kSavedIeMask = 0x8099DF;  ///< Máscara IE extra (RAM interna).
constexpr uint32_t kVarC941 = 0xC941;  ///< Se 0, a INT0 alterna `[C942]` e chama `0x8E4E`/`0x8E59` e `0x8E6D` a cada evento.
constexpr uint32_t kVarC942 = 0xC942;  ///< Alterna 0/1 a cada evento.
constexpr uint32_t kVarEB50 = 0xEB50;  ///< Decrementado a cada evento.
constexpr uint32_t kVarE860 = 0xE860;  ///< Se != 0, [E85F] conta quadros.
constexpr uint32_t kVarE85F = 0xE85F;  ///< Tempo decorrido da corrida em quadros; a INT0 incrementa enquanto `[E860]` != 0.
constexpr uint32_t kVarE871 = 0xE871;  ///< Índice somado a 0x91 para a auditoria do modo 4.

/// Soma dois floats TMS (`ADDF`). Precisão: double do host (o C31 usa 40 bits internamente).
uint32_t addf(uint32_t a, uint32_t b) { return doubleToTms(tmsToDouble(a) + tmsToDouble(b)); }
} // namespace

VblankHandler::Result VblankHandler::run(Tms32031State& cpu) {
    // Entrada pelo hardware: o C31 desliga GIE e empilha o PC.
    cpu.st &= ~Tms32031State::kStGie;

    // 04C34  PUSH ST; IE = INT1; ANDN 1,IF (reconhece a INT0); PUSH DP; PUSH R0
    // 04C39  IE |= [0x8099DF]  (Slave com link: INT2 continua habilitada)
    // 04C3C  PUSH bus control; bus control = 0x470
    // 04C40–04C5C  PUSH de todos os registradores
    const Tms32031State saved = cpu;
    cpu.ie = Tms32031State::kIeInt1;
    cpu.if_ &= ~Tms32031State::kIeInt0;
    cpu.ie |= rd(kSavedIeMask);
    const uint32_t saved_bus_ctrl = rd(vunit::kBusControl);
    wr(vunit::kBusControl, 0x0470);

    // 04C5D  DP = 0; CALL $9E97
    cpu.dp = 0;
    call(0x9E97, cpu);
    // 04C5F  paleta[0] (0x9E0000) = [C938]
    cpu.r[0] = rd(kBackdropColor);
    wr(vunit::kPaletteBase, cpu.r[0]);
    cpu.dp = 0;
    // 04C63  CALL $7F25; CALL $4C33 (religa interrupções: o resto é interrompível)
    call(0x7F25, cpu);
    cpu.st |= Tms32031State::kStGie;

    // 04C65  Watchdog de software (exceto no modo 1).
    cpu.r[0] = rd(kGameMode) & 0x000Fu;
    if (cpu.r[0] != 1) {
        cpu.r[0] = rd(kSoftWatchdog) + 1;
        if (static_cast<int32_t>(cpu.r[0]) > static_cast<int32_t>(kSoftWatchdogLimit)) {
            // 04C71  PUSH ST/R0; R0 = 0x800; CALL $AB1A; POP; BU $4AD5 (RESET)
            // (04C70 "BU $4C77" é código morto: nunca alcançado.)
            const uint32_t r0 = cpu.r[0];
            cpu.r[0] = 0x0800;
            call(0xAB1A, cpu);
            cpu.r[0] = r0;
            return Result::WatchdogReset;
        }
        // 04C78  [0x8099E0] = R0
        wr(kSoftWatchdog, cpu.r[0]);
    }

    // 04C7B  CALL $5029 — alimenta o watchdog de hardware.
    call(0x5029, cpu);

    // 04C7C  Evento pendente em [C93F]
    if (rd(kEventFlag) != 0) {
        cpu.r[0] = 0;
        wr(kEventFlag, cpu.r[0]);
        // 04C80  STI R0,@($C932): grava INTEIRO 0 num campo FLOAT. No formato TMS,
        //        0x00000000 vale 1.0 (não 0.0). Preservado como no original.
        wr(kFrameCountF, cpu.r[0]);
        wr(kVarEB50, rd(kVarEB50) - 1);
        if (rd(kVarC941) == 0) {
            cpu.r[0] = rd(kVarC942) ^ 0x0001u;
            wr(kVarC942, cpu.r[0]);
            if (cpu.r[0] != 0) {
                call(0x8E4E, cpu);  // 04C8D
            } else {
                call(0x8E59, cpu);  // 04C8A
            }
            call(0x8E6D, cpu);
        }
    }

    // 04C8F  se [E860] != 0: [E85F]++
    if (rd(kVarE860) != 0) wr(kVarE85F, rd(kVarE85F) + 1);

    // 04C94  Divisor de 57 quadros (~1 s a 57,928 Hz).
    cpu.r[0] = rd(kSecondDivider) + 1;
    wr(kSecondDivider, cpu.r[0]);
    if (static_cast<int32_t>(cpu.r[0]) >= 0x39) {
        cpu.r[0] = 0;
        wr(kSecondDivider, cpu.r[0]);
        // 04C9B  Auditorias de tempo na CMOS, conforme o modo.
        cpu.r[5] = rd(kGameMode) & 0x000Fu;
        if (cpu.r[5] != 1) {
            cpu.ar[2] = 0x0076;
            call(0x99DA, cpu);
            if (cpu.r[5] != 2) {
                cpu.ar[2] = 0x0077;
                call(0x99DA, cpu);
                if (cpu.r[5] == 4) {
                    cpu.ar[2] = rd(kVarE871) + 0x91;
                    call(0x99DA, cpu);
                }
            }
        }
        // 04CAA  [E86A] = max([E86A] − 1, 0)
        cpu.r[0] = rd(kSecondsTimerA) - 1;
        if (static_cast<int32_t>(cpu.r[0]) < 0) cpu.r[0] = 0;
        wr(kSecondsTimerA, cpu.r[0]);
        // 04CAE  se [E604] > 0 e ([C8C5] & 0x200): [E604]−−
        cpu.r[0] = rd(kSecondsTimerB);
        if (static_cast<int32_t>(cpu.r[0]) > 0) {
            cpu.r[1] = rd(kGameMode);
            if ((cpu.r[1] & 0x0200u) != 0) {
                cpu.r[0] -= 1;
                wr(kSecondsTimerB, cpu.r[0]);
            }
        }
    }

    // 04CB5  [C93A] += [B094] (1/3420 → minutos); [C930]++; [C932] += 1.0
    cpu.r[0] = addf(rd(kMinutesPerFrame), rd(kPlayMinutes));
    wr(kPlayMinutes, cpu.r[0]);
    cpu.r[0] = rd(kFrameCounter) + 1;
    wr(kFrameCounter, cpu.r[0]);
    cpu.r[0] = addf(rd(kFrameCountF), doubleToTms(1.0));
    wr(kFrameCountF, cpu.r[0]);

    // 04CBE  Rotinas de jogo por quadro.
    for (uint32_t addr : {0xAFC2u, 0x92B3u, 0x4CEFu, 0x8EB7u, 0x4E4Fu, 0x75F1u, 0x4FAEu, 0x49B1u})
        call(addr, cpu);

    // 04CC6  ANDN 1,IF
    const uint32_t if_now = cpu.if_ & ~Tms32031State::kIeInt0;
    // 04CC7–04CE7  POP de tudo; restaura o bus control.
    wr(vunit::kBusControl, saved_bus_ctrl);
    cpu = saved;
    cpu.if_ = if_now;
    // 04CE8  IE = INT0 | INT1 | INT3 | [0x8099DF]
    cpu.ie = (Tms32031State::kIeInt0 | Tms32031State::kIeInt1 | Tms32031State::kIeInt3) | rd(kSavedIeMask);
    // 04CEB  POP DP; POP ST; RETIU (RETI liga GIE)
    cpu.st |= Tms32031State::kStGie;
    return Result::Returned;
}

} // namespace rea::core::irq
