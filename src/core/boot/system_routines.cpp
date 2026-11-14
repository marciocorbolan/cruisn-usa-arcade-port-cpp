/**
 * @file system_routines.cpp
 * @brief Tradução das sub-rotinas de sistema chamadas pelo RESET.
 */
#include "core/boot/system_routines.hpp"

#include "core/boot/power_on_test.hpp"
#include "core/vunit_hw.hpp"

namespace rea::core::boot {

namespace {
constexpr uint32_t kVideoTimingPtr = 0x4ECB;  ///< [0x4ECB] = 0xC102DA (tabela na ROM).
constexpr uint32_t kSavedIeMask = 0x8099DF;   ///< Máscara IE extra (RAM interna).
constexpr uint32_t kControlShadow = 0xC8C7;   ///< Cópia de 0x994000.
constexpr uint32_t kVarC939 = 0xC939;  ///< != 0 → `0x4E1B` abre o menu de teste.
} // namespace

void loadVideoTiming(Tms32031State& cpu, MemoryBus& bus) {
    // 04ECC  PUSH AR0/AR1/R0/DP
    const uint32_t ar0 = cpu.ar[0], ar1 = cpu.ar[1], r0 = cpu.r[0], dp = cpu.dp;
    // 04ED0  AR0 = 0x980020; DP = 0; AR1 = @($4ECB)
    cpu.ar[0] = vunit::kVideoCtrlBase;
    cpu.dp = 0;
    cpu.ar[1] = bus.readWord(kVideoTimingPtr);
    // 04ED5  RC = 11; RPTB (12×): R0 = *AR1++; *AR0++ = R0
    cpu.rc = 0x000B;
    for (uint32_t i = 0; i <= 0x000B; ++i) {
        cpu.r[0] = bus.readWord(cpu.ar[1]++);
        bus.writeWord(cpu.ar[0]++, cpu.r[0]);
    }
    cpu.rc = 0xFFFFFFFFu;
    // 04ED9  POP DP/R0/AR1/AR0
    cpu.dp = dp;
    cpu.r[0] = r0;
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
}

void checkPendingC939(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 04E1B  LDI @($C939),R0 (DP = 0 nos chamadores)
    cpu.r[0] = bus.readWord(cpu.direct(kVarC939));
    // 04E1C  RETSEQ — retorna se zero.
    if (cpu.r[0] == 0) return;
    // 04E1D  BR $0699 — desvio sem retorno; o RETS de 0x0699 volta ao chamador
    //        original (o endereço de retorno ainda está na pilha). Equivale a um CALL.
    subs.call(0x0699, cpu, bus);
}

namespace {
/**
 * @brief Padrão `AR0 = [ptr_ini]; RC = [ptr_fim] − AR0 − 1; RPTB: *AR0++ = 0`.
 * @details Zera [início, fim). Os registradores usados são salvos pelo chamador
 *          original (PUSH/POP), portanto não há efeito colateral.
 */
void clearRange(MemoryBus& bus, uint32_t begin_ptr, uint32_t end_ptr) {
    const uint32_t begin = bus.readWord(begin_ptr);
    const uint32_t end = bus.readWord(end_ptr);
    for (uint32_t a = begin; a < end; ++a) bus.writeWord(a, 0);
}
} // namespace

void clearSystemVars(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 04E20  PUSH R0/AR0/RC/RS/RE; zera [@($4E1E)=0xC8C4, @($4E1F)=0xC8ED)
    const Tms32031State saved = cpu;
    clearRange(bus, 0x4E1E, 0x4E1F);
    // 04E2C  CALL $793A (ainda dentro do bloco salvo)
    subs.call(0x793A, cpu, bus);
    // 04E2D  POP RE/RS/RC/AR0/R0
    cpu.r[0] = saved.r[0];
    cpu.ar[0] = saved.ar[0];
    cpu.rc = saved.rc;
    cpu.rs = saved.rs;
    cpu.re = saved.re;
}

void clearBss(Tms32031State&, MemoryBus& bus) {
    // 04E35  PUSH R0/AR0/RC/RS/RE; zera [@($4E33)=0xC8EE, @($4E34)=0x1EFFF); POP. Sem efeito em registradores.
    clearRange(bus, 0x4E33, 0x4E34);
}

void kickWatchdog(Tms32031State&, MemoryBus& bus) {
    // 05029  PUSH DP/R0; salva o bus control e usa 0x470
    const uint32_t bus_ctrl = bus.readWord(vunit::kBusControl);
    bus.writeWord(vunit::kBusControl, 0x0470);
    // 05031  [C8C7] ^= 8 → 0x994000 (bit 3 = watchdog no MAME)
    const uint32_t v = bus.readWord(kControlShadow) ^ 0x0008u;
    bus.writeWord(kControlShadow, v);
    bus.writeWord(vunit::kControl, v);
    // 05036  restaura o bus control; POP R0/DP
    bus.writeWord(vunit::kBusControl, bus_ctrl);
}

void pulseControl(Tms32031State& cpu, MemoryBus& bus) {
    // 04EE3–04EE8  desliga interrupções (IE preservado)
    cpu.st &= ~Tms32031State::kStGie;
    // 04EE9  R0 = [C8C7]; R1 = R0 & ~1; 0x994000 = R1, depois R0 (pulso no bit 0)
    cpu.dp = 0;
    cpu.r[0] = bus.readWord(kControlShadow);
    cpu.r[1] = cpu.r[0] & ~0x0001u;
    bus.writeWord(vunit::kControl, cpu.r[1]);
    bus.writeWord(vunit::kControl, cpu.r[0]);
    // 04EF0  CALL $4C33 — liga interrupções
    cpu.st |= Tms32031State::kStGie;
    // 04EF1  0x980080 = 0xA, depois 0x8 (pulso no bit 1)
    cpu.r[0] = 0x000A;
    bus.writeWord(vunit::kVideoMisc, cpu.r[0]);
    cpu.r[0] = 0x0008;
    bus.writeWord(vunit::kVideoMisc, cpu.r[0]);
    // 04EF7  SUBI DP,DP
    cpu.dp = 0;
}

void startTimer0(Tms32031State&, MemoryBus& bus) {
    // 04EF9  PUSH DP/R0; @($808020) |= $C0 (GO | HLD); POP
    bus.writeWord(vunit::kTimer0Control, bus.readWord(vunit::kTimer0Control) | 0x00C0u);
}

void setupLinkBoard(Tms32031State& cpu, MemoryBus& bus) {
    // 07F06  PUSH DP; DP = 0; R1 = [C8CC]; [C8D0] = 0
    cpu.r[1] = bus.readWord(kDipSwitchesVar);
    cpu.r[0] = 0;
    bus.writeWord(0xC8D0, cpu.r[0]);
    // 07F0B  R0 = 0x3000; se Master (bit 0 = 0): R0 = −0x4000; R0 <<= 16
    cpu.r[0] = 0x3000;
    if ((cpu.r[1] & dip::kLinkSlave) == 0) cpu.r[0] = static_cast<uint32_t>(-0x4000);
    cpu.r[0] <<= 16;
    bus.writeWord(0x997000, cpu.r[0]);
    // 07F11  0x997001 = 0x2000 << 16
    cpu.r[0] = 0x2000u << 16;
    bus.writeWord(0x997001, cpu.r[0]);
    // 07F14  POP DP
}

void enableLinkInterrupt(Tms32031State& cpu, MemoryBus& bus) {
    // 07F18  DP = 0; R1 = [C8CC]
    cpu.dp = 0;
    cpu.r[1] = bus.readWord(kDipSwitchesVar);
    // 07F1A  se link ligado (bit 2 = 0) e Slave (bit 0 = 1): máscara salva = EINT2
    if ((cpu.r[1] & dip::kLinkingOff) == 0 && (cpu.r[1] & dip::kLinkSlave) != 0) {
        cpu.r[1] = Tms32031State::kIeInt2;
        bus.writeWord(kSavedIeMask, cpu.r[1]);
    }
    // 07F21  IE |= [0x8099DF]; DP = 0
    cpu.ie |= bus.readWord(kSavedIeMask);
    cpu.dp = 0;
}

void prepareAttract(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    subs.call(0x95CB, cpu, bus);                       // 05020
    subs.call(0x9E80, cpu, bus);                       // 05021
    cpu.ar[2] = bus.readWord(cpu.direct(0xB0AA));      // 05022  LDI @($B0AA),AR2
    subs.call(0x9F2F, cpu, bus);                       // 05023
    cpu.r[0] = 0x4210;                                 // 05024
    bus.writeWord(cpu.direct(0xC938), cpu.r[0]);       // 05025  cor de fundo
    cpu.r[0] = 0x0001;                                 // 05026
    bus.writeWord(cpu.direct(0xC8C5), cpu.r[0]);       // 05027  modo 1
}

void registerRoutines(SubroutineDispatcher& d) {
    d.add(0x5020, prepareAttract);
    d.add(0x4ECC, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { loadVideoTiming(c, b); });
    d.add(0x4E1B, checkPendingC939);
    d.add(0x4E20, clearSystemVars);
    d.add(0x4E35, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { clearBss(c, b); });
    d.add(0x5029, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { kickWatchdog(c, b); });
    d.add(0x4EE3, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { pulseControl(c, b); });
    d.add(0x4EF9, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { startTimer0(c, b); });
    d.add(0x7F06, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { setupLinkBoard(c, b); });
    d.add(0x7F18, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { enableLinkInterrupt(c, b); });
    registerPowerOnTest(d);  // 0x62D5 / 0x6381 e auxiliares
}

} // namespace rea::core::boot
