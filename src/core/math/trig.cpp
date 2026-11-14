/**
 * @file trig.cpp
 * @brief Tradução de `0x92DF`–`0x92FF` e `0x938A`.
 */
#include "core/math/trig.hpp"

#include "core/tms_float.hpp"

namespace rea::core::math {

namespace {
/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
/// Inverte o sinal do float TMS @p raw (`NEGF`).
uint32_t negf(uint32_t raw) { return doubleToTms(-tmsToDouble(raw)); }
} // namespace

void sinCos(Tms32031State& cpu, MemoryBus& bus, bool cosine) {
    auto f = [&](uint32_t a) { return tmsToDouble(bus.readWord(cpu.direct(a))); };
    // 092DF  R0 = R2 + π/2 (cosseno) | 092E2  R0 = R2
    double x = tmsToDouble(cpu.r[2]);
    if (cosine) x += f(kHalfPiPtr);
    const uint32_t ar0 = cpu.ar[0];
    // 092E4  negativo: R0 = −R0 + π
    if (x < 0.0) x = -x + f(kPiPtr);
    // 092E7  índice = FIX(R0 · 512/π + 0,5) & 0x3FF
    cpu.r[0] = tmsFix(x * f(kTrigScalePtr) + 0.5) & 0x03FFu;
    bool negate = false;
    if (s(cpu.r[0]) >= 0x200) {
        cpu.r[0] -= 0x200;
        negate = true;
    }
    if (s(cpu.r[0]) >= 0x100) cpu.r[0] = 0x200u - cpu.r[0];  // SUBI $200; NEGI
    cpu.ar[0] = cpu.r[0] + bus.readWord(cpu.direct(kSinTablePtr));
    cpu.r[0] = bus.readWord(cpu.ar[0]);
    if (negate) cpu.r[0] = negf(cpu.r[0]);
    cpu.ar[0] = ar0;
}

void rotationY(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r0 = cpu.r[0];
    sinCos(cpu, bus, true);
    bus.writeWord(cpu.ar[2] + 0, cpu.r[0]);
    bus.writeWord(cpu.ar[2] + 8, cpu.r[0]);
    sinCos(cpu, bus, false);
    bus.writeWord(cpu.ar[2] + 6, cpu.r[0]);
    cpu.r[0] = negf(cpu.r[0]);
    bus.writeWord(cpu.ar[2] + 2, cpu.r[0]);
    cpu.r[0] = doubleToTms(1.0);
    bus.writeWord(cpu.ar[2] + 4, cpu.r[0]);
    cpu.r[0] = doubleToTms(0.0);
    for (uint32_t i : {1u, 3u, 5u, 7u}) bus.writeWord(cpu.ar[2] + i, cpu.r[0]);
    cpu.r[0] = r0;
}

void rotationX(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r0 = cpu.r[0];
    sinCos(cpu, bus, true);
    bus.writeWord(cpu.ar[2] + 4, cpu.r[0]);
    bus.writeWord(cpu.ar[2] + 8, cpu.r[0]);
    sinCos(cpu, bus, false);
    bus.writeWord(cpu.ar[2] + 5, cpu.r[0]);
    bus.writeWord(cpu.ar[2] + 7, negf(cpu.r[0]));
    bus.writeWord(cpu.ar[2] + 0, doubleToTms(1.0));
    for (uint32_t i : {1u, 2u, 3u, 6u}) bus.writeWord(cpu.ar[2] + i, doubleToTms(0.0));
    cpu.r[0] = r0;
}

void sinCosPoly(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, bool cosine) {
    cpu.dp = 0;
    auto f = [&](uint32_t a) { return tmsToDouble(bus.readWord(a)); };
    const uint32_t r4 = cpu.r[4];
    const double x = tmsToDouble(cpu.r[2]);
    double sign = 1.0, ax = x < 0.0 ? -x : x, n = 0.0, red = 0.0;
    if (!cosine) {
        // 0B01B  R3 = ±1; R2 = |x|; RC = FIX(|x|/π + 0,5); R4 = FLOAT(RC)
        sign = x < 0.0 ? -1.0 : 1.0;
        cpu.rc = tmsFix(ax * f(0xC8BD) + 0.5);
        n = static_cast<double>(static_cast<int32_t>(cpu.rc));
        cpu.r[0] = cpu.rc;
        cpu.r[1] = 2;
        subs.call(0xA5A9, cpu, bus);
        if (cpu.r[0] != 0) sign = -sign;
        // 0B02A  f = (|x| − N·3,140625) − N·C2
        red = (ax - n * 3.140625) - n * f(0xC8BE);
    } else {
        // 0B043  R3 = |x|; RC = FIX((|x| + π/2)/π + 0,5); R2 = N − 0,5
        cpu.rc = tmsFix((ax + f(0xC8C3)) * f(0xC8BD) + 0.5);
        n = static_cast<double>(static_cast<int32_t>(cpu.rc)) - 0.5;
        cpu.r[0] = cpu.rc;
        cpu.r[1] = 2;
        subs.call(0xA5A9, cpu, bus);
        sign = cpu.r[0] != 0 ? -1.0 : 1.0;
        red = (ax - n * 3.140625) - n * f(0xC8BE);
    }
    const double g = red * red;
    double p = g * f(0xC8BF) + f(0xC8C0);
    p = p * g + f(0xC8C1);
    p = p * g + f(0xC8C2);
    p = p * g;
    const double r = (p * red + red) * sign;
    cpu.r[1] = bus.readWord(0xC8BF);
    cpu.r[2] = doubleToTms(g);
    cpu.r[3] = doubleToTms(cosine ? red : sign);
    cpu.r[0] = doubleToTms(r);
    cpu.r[4] = r4;
}

void rotationYPoly(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3], r4 = cpu.r[4];
    sinCosPoly(cpu, bus, subs, true);
    bus.writeWord(cpu.ar[2] + 0, cpu.r[0]);
    bus.writeWord(cpu.ar[2] + 8, cpu.r[0]);
    cpu.r[2] = r2;
    sinCosPoly(cpu, bus, subs, false);
    bus.writeWord(cpu.ar[2] + 6, cpu.r[0]);
    bus.writeWord(cpu.ar[2] + 2, negf(cpu.r[0]));
    bus.writeWord(cpu.ar[2] + 4, doubleToTms(1.0));
    for (uint32_t i : {1u, 3u, 5u, 7u}) bus.writeWord(cpu.ar[2] + i, doubleToTms(0.0));
    cpu.r[0] = r0;
    cpu.r[1] = r1;
    cpu.r[2] = r2;
    cpu.r[3] = r3;
    cpu.r[4] = r4;
}

void registerTrig(SubroutineDispatcher& d) {
    d.add(0xB01A, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { sinCosPoly(c, b, s, false); });
    d.add(0xB042, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { sinCosPoly(c, b, s, true); });
    d.add(0x939D, rotationYPoly);
    d.add(0x9377, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { rotationX(c, b); });
    d.add(0x92DF, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { sinCos(c, b, true); });
    d.add(0x92E2, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { sinCos(c, b, false); });
    d.add(0x938A, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { rotationY(c, b); });
}

} // namespace rea::core::math
