/**
 * @file profiler_routines.cpp
 * @brief Tradução de `0x4F08`–`0x4F31` e `0xA585`–`0xA5A8`.
 */
#include "core/main/profiler_routines.hpp"

#include <cmath>

#include "core/tms_float.hpp"
#include "core/vunit_hw.hpp"

namespace rea::core::prof {

using namespace prof_vars;

namespace {
/// Endereço do contador do timer 0 (`@($8024)` com DP = 0x80).
constexpr uint32_t kTimer0Counter = 0x808024;
} // namespace

void sample(Tms32031State& cpu, MemoryBus& bus) {
    // 04F27  PUSH DP, AR0; DP = 0x80; FLOAT @($8024),R0; DP = 0
    const uint32_t dp = cpu.dp, ar0 = cpu.ar[0];
    const auto counter = static_cast<int32_t>(bus.readWord(kTimer0Counter));
    cpu.r[0] = doubleToTms(static_cast<double>(counter));
    // 04F2C  AR0 = [C945]; STF R0,*AR0++; [C945] = AR0
    cpu.ar[0] = bus.readWord(kCursor);
    bus.writeWord(cpu.ar[0]++, cpu.r[0]);
    bus.writeWord(kCursor, cpu.ar[0]);
    // 04F2F  POP AR0, DP
    cpu.ar[0] = ar0;
    cpu.dp = dp;
}

void endIteration(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 04F08  PUSH DP; CALL $4EF9 (religa o timer 0); DP = 0
    const uint32_t dp = cpu.dp;
    subs.call(0x4EF9, cpu, bus);
    cpu.dp = 0;
    // 04F0B  [C945] = [4F07] (= 0xC947): reinicia o cursor das amostras
    cpu.ar[0] = bus.readWord(kBlockPtr);
    bus.writeWord(kCursor, cpu.ar[0]);
    // 04F0D  se [C946] != 0 → zera tudo (04F1E)
    cpu.r[0] = bus.readWord(kResetRequest);
    if (cpu.r[0] != 0) {
        cpu.r[0] = 0;
        bus.writeWord(kResetRequest, cpu.r[0]);
        cpu.r[0] = doubleToTms(0.0);
        bus.writeWord(kIterations, cpu.r[0]);
        cpu.ar[0] = bus.readWord(kBlockPtr);
        for (uint32_t i = 0; i <= 0x2F; ++i) bus.writeWord(cpu.ar[0]++, cpu.r[0]);  // RPTS $2F
        cpu.dp = dp;
        return;
    }
    // 04F0F  [C944] += 1.0; CALL $A585 → R0 = 1/[C944]
    cpu.r[0] = doubleToTms(tmsToDouble(bus.readWord(kIterations)) + 1.0);
    bus.writeWord(kIterations, cpu.r[0]);
    subs.call(0xA585, cpu, bus);
    const double inv = tmsToDouble(cpu.r[0]);
    // 04F13  AR0 = 0xC947; RC = 15; RPTB (16×):
    //        R1 = amostra[i]; R1 += soma[i]; soma[i] = R1; R1 *= R0; média[i] = R1
    cpu.ar[0] = bus.readWord(kBlockPtr);
    bus.writeWord(kCursor, cpu.ar[0]);
    cpu.rc = 0x000F;
    for (uint32_t i = 0; i <= 0x0F; ++i) {
        double r1 = tmsToDouble(bus.readWord(cpu.ar[0]++));          // LDF *AR0++(1),R1
        r1 += tmsToDouble(bus.readWord(cpu.ar[0] + 0x0F));            // ADDF *+AR0(0Fh),R1
        bus.writeWord(cpu.ar[0] + 0x0F, doubleToTms(r1));            // STF R1,*+AR0(0Fh)
        cpu.r[1] = doubleToTms(r1 * inv);                             // MPYF R0,R1
        bus.writeWord(cpu.ar[0] + 0x1F, cpu.r[1]);                    // STF R1,*+AR0(1Fh)
    }
    cpu.rc = 0xFFFFFFFFu;
    // 04F1C  POP DP; RETS
    cpu.dp = dp;
}

void reciprocal(Tms32031State& cpu) {
    const double a = tmsToDouble(cpu.r[0]);
    if (a == 0.0) return;  // o original não trata 1/0 (resultado indefinido); mantém R0.
    const double inv = 1.0 / std::fabs(a);
    // 0A5A3  NEGF R0,R1 (R1 = −|1/a|); 0A5A6 LDFLT R1,R0 (sinal do original)
    cpu.r[1] = doubleToTms(-inv);
    cpu.r[0] = doubleToTms(a < 0.0 ? -inv : inv);
}

void registerRoutines(SubroutineDispatcher& d) {
    d.add(0x4F27, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { sample(c, b); });
    d.add(0x4F08, endIteration);
    d.add(0xA585, [](Tms32031State& c, MemoryBus&, ISubroutines&) { reciprocal(c); });
}

} // namespace rea::core::prof
