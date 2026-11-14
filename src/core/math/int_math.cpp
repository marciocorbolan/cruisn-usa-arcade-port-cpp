/**
 * @file int_math.cpp
 * @brief Tradução de `0xA531`, `0xA5A9` e `0x8EB7`.
 */
#include "core/math/int_math.hpp"

#include "core/tms_float.hpp"

namespace rea::core::math {

namespace {
/// ABSI (0x80000000 fica igual)
uint32_t absU(uint32_t v) { return static_cast<int32_t>(v) < 0 ? 0u - v : v; }
} // namespace

uint32_t divI(uint32_t a, uint32_t b) {
    if (b == 0) return 0;
    const uint32_t q = absU(a) / absU(b);
    return static_cast<int32_t>(a ^ b) < 0 ? 0u - q : q;
}

uint32_t modI(uint32_t a, uint32_t b) {
    if (b == 0) return 0;
    const uint32_t r = absU(a) % absU(b);
    return static_cast<int32_t>(a) < 0 ? 0u - r : r;
}

void random(Tms32031State& cpu, MemoryBus& bus) {
    // 08EB7  PUSH R1; R0 = [C8CE]; R1 = R0; R0 <<= 1; R1 ^= R0; R1 < 0 → R0 |= 1
    const uint32_t r1 = cpu.r[1];
    cpu.r[0] = bus.readWord(cpu.direct(kRandomSeed));
    cpu.r[1] = cpu.r[0];
    cpu.r[0] <<= 1;
    cpu.r[1] ^= cpu.r[0];
    if (static_cast<int32_t>(cpu.r[1]) < 0) cpu.r[0] |= 1u;
    cpu.r[1] = r1;
    // 08EBF  R0 = MPYI(R0, 0x794F); [C8CE] = R0
    cpu.r[0] = tmsMpyi(cpu.r[0], 0x794F);
    bus.writeWord(cpu.direct(kRandomSeed), cpu.r[0]);
}

void randomBelow(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 08EE9  R0 = ((aleatório >> 16) · 1000) >> 16; R0 < AR2 (sem sinal) → carry, senão R0 = 0
    subs.call(0x8EB7, cpu, bus);
    cpu.r[0] >>= 16;
    cpu.r[0] = tmsMpyi(cpu.r[0], 1000);
    cpu.r[0] >>= 16;
    if (cpu.r[0] < cpu.ar[2]) {
        cpu.st |= Tms32031State::kStCarry;
        return;
    }
    cpu.st &= ~Tms32031State::kStCarry;
    cpu.r[0] = 0;
}

void registerRoutines(SubroutineDispatcher& d) {
    d.add(0x8EE9, randomBelow);
    d.add(0xA531, [](Tms32031State& c, MemoryBus&, ISubroutines&) { c.r[0] = divI(c.r[0], c.r[1]); });
    d.add(0xA5A9, [](Tms32031State& c, MemoryBus&, ISubroutines&) {
        // 0A5A9  AR0 = dividendo; ... 0A5C0  R1 = −|resto|; R0 = resto (sinal do dividendo); CMPI 0,R0
        const uint32_t a = c.r[0];
        c.r[0] = modI(a, c.r[1]);
        c.ar[0] = a;
        c.r[1] = 0u - (static_cast<int32_t>(c.r[0]) < 0 ? 0u - c.r[0] : c.r[0]);
    });
    d.add(0x8EB7, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { random(c, b); });
    // 08EDB  R0 = ((aleatório >> 16) · AR2) >> 16  (0 ≤ R0 < AR2)
    d.add(0x8EDB, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        s.call(0x8EB7, c, b);
        c.r[0] = tmsMpyi(c.r[0] >> 16, c.ar[2]) >> 16;
    });
    // 08EE0  R0 = 1..AR2
    d.add(0x8EE0, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        s.call(0x8EDB, c, b);
        c.r[0] += 1;
    });
    // 08EE3  R0 = −AR2..+AR2 (AR2 preservado)
    d.add(0x8EE3, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t ar2 = c.ar[2];
        c.ar[2] = (ar2 << 1) + 1;
        s.call(0x8EDB, c, b);
        c.ar[2] = ar2;
        c.r[0] -= ar2;
    });
    // 08EC2  R0 = R0 · aleatório(0..10000) · 0,0001 (float). Preserva R1 e AR2.
    d.add(0x8EC2, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t ar2 = c.ar[2], r1 = c.r[1];
        const double x = tmsToDouble(c.r[0]);
        c.ar[2] = 0x2710;
        s.call(0x8EDB, c, b);
        const double k = shortFloat(static_cast<uint16_t>(b.readWord(0x8EC9)));
        const double kk = shortFloat(static_cast<uint16_t>(b.readWord(0x8ECA)));
        c.r[0] = doubleToTms(static_cast<double>(static_cast<int32_t>(c.r[0])) * k * kk * x);
        c.r[1] = r1;
        c.ar[2] = ar2;
    });
    // 08ED1  R0 = aleatório em [−R0, +R0) (float). Preserva R1.
    d.add(0x8ED1, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const double x = tmsToDouble(c.r[0]);
        c.r[0] = doubleToTms(x * 2.0);
        s.call(0x8EC2, c, b);
        c.r[0] = doubleToTms(tmsToDouble(c.r[0]) - x);
    });
}

} // namespace rea::core::math
