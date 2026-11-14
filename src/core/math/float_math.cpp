/**
 * @file float_math.cpp
 * @brief Tradução de `0xA509`, `0xA5F1`, `0x9308`–`0x9340`, `0x93C4`, `0x93E8`, `0x93FD`, `0x943B`,
 *        `0x9414`, `0x941E`, `0x94B4` e `0x94C9`.
 */
#include "core/math/float_math.hpp"

#include <cmath>

#include "core/tms_float.hpp"

namespace rea::core::math {

namespace {

/// Expoente do formato curto de 32 bits do TMS (0,0 → −128).
int tmsExponent(double v) { return static_cast<int8_t>(static_cast<uint8_t>(doubleToTms(v) >> 24)); }
/// `RND`: arredonda (ao mais próximo) para a precisão de 32 bits.
double rnd(double v) { return tmsRound(v); }
/// Lê o float TMS em @p a e o converte para double.
double rd(MemoryBus& bus, uint32_t a) { return tmsToDouble(bus.readWord(a)); }

constexpr uint32_t kAtanTable  = 0x9340; ///< `[9340]` = 0xC10ED3: 129 entradas de atan(i/128).
constexpr uint32_t kOctants    = 0x9341; ///< `[9341]` = 0xC10EC3: (multiplicador, soma) por octante.
constexpr uint32_t kToTurn     = 0x9306; ///< 65536 / 2π.
constexpr uint32_t kFromTurn   = 0x9307; ///< 2π / 65536.
constexpr uint32_t kScratchVec = 0x005D; ///< `[0x5D]`: vetor de rascunho.

/// Quociente por Newton-Raphson (`0xA509`) e o R1 de saída.
double nrDivide(double num, double den, double& r1Out) {
    const double r = std::fabs(den);
    double x = std::ldexp(1.0, -tmsExponent(r) - 1);
    for (int i = 0; i < 4; ++i) x *= 2.0 - r * x;
    x = rnd(x);
    x = rnd(x + x * (1.0 - r * x));
    const double q = num * x;
    r1Out = -q;
    return den < 0.0 ? -q : q;
}

} // namespace

void floatDivide(Tms32031State& cpu) {
    double r1 = 0.0;
    const double q = nrDivide(tmsToDouble(cpu.r[0]), tmsToDouble(cpu.r[1]), r1);
    cpu.r[0] = doubleToTms(q);
    cpu.r[1] = doubleToTms(r1);
}

void floatSqrt(Tms32031State& cpu) {
    // 0A5F1  R2 ≤ 0 → R0 = R2
    cpu.r[0] = cpu.r[2];
    const double x = tmsToDouble(cpu.r[2]);
    if (x <= 0.0) return;
    double y = std::ldexp(1.0, -(tmsExponent(x * 2.0) >> 1));
    const double h = x * 2.0 * 0.25;
    for (int i = 0; i < 5; ++i) y = rnd(y * (1.5 - y * y * h));
    cpu.r[0] = doubleToTms(y * x);
}

void atan2Rom(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    const uint32_t r1 = cpu.r[1], r4 = cpu.r[4], ar0 = cpu.ar[0];
    const double a = tmsToDouble(cpu.r[2]), b = tmsToDouble(cpu.r[3]);
    uint32_t octant = (b < 0.0 ? 4u : 0u) + (a < 0.0 ? 2u : 0u);
    double num, den;
    if (std::fabs(b) < std::fabs(a)) {
        num = b;
        den = a;
    } else {
        num = a;
        den = b;
        octant += 1;
    }
    double dummy = 0.0;
    double q = std::fabs(rnd(nrDivide(num, den, dummy))) * 128.0;
    const uint32_t idx = tmsFix(q);
    const double t = rd(bus, bus.readWord(kAtanTable) + idx);
    const uint32_t k = bus.readWord(kOctants) + (octant << 1);
    cpu.r[0] = doubleToTms(t * rd(bus, k) + rd(bus, k + 1));
    cpu.ar[0] = ar0;
    cpu.r[4] = r4;
    cpu.r[1] = r1;
}

void wrapAngle(Tms32031State& cpu, MemoryBus& bus, bool signedRange) {
    // 09308  R2 · 65536/2π → 16 bits; 0x7FFF..0xFFFF viram negativos; volta para radianos
    uint32_t t = tmsFix(rnd(tmsToDouble(cpu.r[2]) * rd(bus, kToTurn))) & 0xFFFFu;
    int32_t v = static_cast<int32_t>(t);
    if (signedRange && v >= 0x7FFF) v -= 0x10000;
    cpu.r[2] = doubleToTms(static_cast<double>(v) * rd(bus, kFromTurn));
}

void matVec(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t m = cpu.r[2], v = cpu.ar[2];
    double out[3];
    for (uint32_t row = 0; row < 3; ++row) {
        double acc = 0.0;
        for (uint32_t col = 0; col < 3; ++col) acc += rd(bus, m + row * 3 + col) * rd(bus, v + col);
        out[row] = acc;
    }
    for (uint32_t row = 0; row < 3; ++row) bus.writeWord(cpu.r[3] + row, doubleToTms(out[row]));
}

void transpose(Tms32031State& cpu, MemoryBus& bus) {
    uint32_t w[9];
    for (uint32_t i = 0; i < 9; ++i) w[i] = bus.readWord(cpu.r[2] + i);
    for (uint32_t r = 0; r < 3; ++r)
        for (uint32_t c = 0; c < 3; ++c) bus.writeWord(cpu.ar[2] + r * 3 + c, w[c * 3 + r]);
}

/// `0x94B4`: R0 = (a·x + b·y + c) / √(a² + b²), reta [AR0] = (a, b, c) e ponto [AR1] = (x, y).
/// Preserva R1 e R2.
static void lineDistance(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r1 = cpu.r[1], r2 = cpu.r[2];
    const double a = rd(bus, cpu.ar[0]), b = rd(bus, cpu.ar[0] + 1);
    const double num = rnd(rnd(a * rd(bus, cpu.ar[1])) + rnd(b * rd(bus, cpu.ar[1] + 1))) + rd(bus, cpu.ar[0] + 2);
    cpu.r[2] = doubleToTms(rnd(a * a) + rnd(b * b));
    subs.call(0xA5F1, cpu, bus);
    cpu.r[1] = cpu.r[0];
    cpu.r[0] = doubleToTms(num);
    subs.call(0xA509, cpu, bus);
    cpu.r[1] = r1;
    cpu.r[2] = r2;
}

/// `0x94C9`: reta [AR2] = (dx, dy, −(x₁·dx + y₁·dy)) com d = [AR0] − [AR1] e (x₁, y₁) = [AR1].
/// Preserva R0..R3.
static void lineThrough(Tms32031State& cpu, MemoryBus& bus) {
    const double dx = rnd(rd(bus, cpu.ar[0]) - rd(bus, cpu.ar[1]));
    const double dy = rnd(rd(bus, cpu.ar[0] + 1) - rd(bus, cpu.ar[1] + 1));
    const double c = -(rnd(rd(bus, cpu.ar[1]) * dx) + rnd(rd(bus, cpu.ar[1] + 1) * dy));
    bus.writeWord(cpu.ar[2], doubleToTms(dx));
    bus.writeWord(cpu.ar[2] + 1, doubleToTms(dy));
    bus.writeWord(cpu.ar[2] + 2, doubleToTms(c));
}

void registerFloatMath(SubroutineDispatcher& d) {
    d.add(0x94B4, lineDistance);
    d.add(0x94C9, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { lineThrough(c, b); });
    d.add(0xA509, [](Tms32031State& c, MemoryBus&, ISubroutines&) { floatDivide(c); });
    d.add(0xA5F1, [](Tms32031State& c, MemoryBus&, ISubroutines&) { floatSqrt(c); });
    d.add(0x931A, atan2Rom);
    d.add(0x9308, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { wrapAngle(c, b, true); });
    d.add(0x9313, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { wrapAngle(c, b, false); });
    d.add(0x941E, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { matVec(c, b); });
    d.add(0x93FD, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { transpose(c, b); });
    // 093C4  rotação em Z em AR2: [0] = [4] = cos, [1] = sen, [3] = −sen, [8] = 1. Preserva R0.
    d.add(0x93C4, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t r0 = c.r[0];
        s.call(0x92DF, c, b);
        b.writeWord(c.ar[2], c.r[0]);
        b.writeWord(c.ar[2] + 4, c.r[0]);
        s.call(0x92E2, c, b);
        b.writeWord(c.ar[2] + 1, c.r[0]);
        b.writeWord(c.ar[2] + 3, doubleToTms(-tmsToDouble(c.r[0])));
        b.writeWord(c.ar[2] + 8, doubleToTms(1.0));
        for (uint32_t i : {2u, 5u, 6u, 7u}) b.writeWord(c.ar[2] + i, doubleToTms(0.0));
        c.r[0] = r0;
    });
    // 093E8  R0 = |[AR2]| (R2 = soma dos quadrados, R1 = z²)
    d.add(0x93E8, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        const double x = rd(b, c.ar[2]), y = rd(b, c.ar[2] + 1), z = rd(b, c.ar[2] + 2);
        c.r[1] = doubleToTms(z * z);
        c.r[2] = doubleToTms(x * x + y * y + z * z);
        floatSqrt(c);
    });
    // 0943B  normaliza o vetor [AR2] (0x93E8 + 0xA585). Preserva R0 e R1.
    d.add(0x943B, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t r0 = c.r[0], r1 = c.r[1];
        s.call(0x93E8, c, b);
        s.call(0xA585, c, b);
        const double k = tmsToDouble(c.r[0]);
        for (uint32_t i = 0; i < 3; ++i) b.writeWord(c.ar[2] + i, doubleToTms(k * rd(b, c.ar[2] + i)));
        c.r[0] = r0;
        c.r[1] = r1;
    });
    // 09414  zera o vetor [0x5D]; AR2 = [0x5D]. Preserva R0.
    d.add(0x9414, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.ar[2] = b.readWord(kScratchVec);
        for (uint32_t i = 0; i < 3; ++i) b.writeWord(c.ar[2] + i, doubleToTms(0.0));
    });
}

} // namespace rea::core::math
