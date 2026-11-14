/**
 * @file matrix.cpp
 * @brief Tradução de `0x947F`, `0x9481`, `0x93F1` e `0x9494`.
 */
#include "core/math/matrix.hpp"

#include "core/tms_float.hpp"

namespace rea::core::math {

void mul3x3(MemoryBus& bus, uint32_t a, uint32_t b, uint32_t out) {
    auto f = [&](uint32_t addr) { return tmsToDouble(bus.readWord(addr)); };
    // 09484  RPTB: para cada linha i, out[i][j] = a[i][0]·b[0][j] + a[i][1]·b[1][j] + a[i][2]·b[2][j]
    for (uint32_t i = 0; i < 3; ++i) {
        const double a0 = f(a + 3 * i), a1 = f(a + 3 * i + 1), a2 = f(a + 3 * i + 2);
        for (uint32_t j = 0; j < 3; ++j) {
            const double v = a0 * f(b + j) + a1 * f(b + 3 + j) + a2 * f(b + 6 + j);
            bus.writeWord(out + 3 * i + j, doubleToTms(v));
        }
    }
}

void matMul(Tms32031State& cpu, MemoryBus& bus) {
    // 0947F  AR0 = R2; AR1 = R3; IR1 = 5; IR0 = 3; RC = 2
    mul3x3(bus, cpu.r[2], cpu.ar[2], cpu.r[3]);
    cpu.ar[0] = cpu.r[2] + 9;
    cpu.ar[1] = cpu.r[3] + 9;
    cpu.ir0 = 3;
    cpu.ir1 = 5;
    cpu.rc = 0xFFFFFFFFu;
}

void matMulRegs(Tms32031State& cpu, MemoryBus& bus) {
    // 09481  mesma multiplicação da 0x947F com AR0/AR1 já carregados pelo chamador
    mul3x3(bus, cpu.ar[0], cpu.ar[2], cpu.ar[1]);
    cpu.ar[0] += 9;
    cpu.ar[1] += 9;
    cpu.ir0 = 3;
    cpu.ir1 = 5;
    cpu.rc = 0xFFFFFFFFu;
}

void matCopy(Tms32031State& cpu, MemoryBus& bus) {
    // 093F4  AR0 = R2; RPTS 7: *AR2++ = *AR0++; STF *AR2--(8)
    for (uint32_t i = 0; i < 9; ++i) bus.writeWord(cpu.ar[2] + i, bus.readWord(cpu.r[2] + i));
    cpu.rc = 0xFFFFFFFFu;
}

void matMulInPlace(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 09494  PUSH AR0, AR1, AR0 (no lugar de R0), R1, R2, R3, R3
    const uint32_t ar0 = cpu.ar[0], ar1 = cpu.ar[1], r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3];
    cpu.r[3] = bus.readWord(kMatrixTempPtr);
    subs.call(0x947F, cpu, bus);
    // 0949D  POP AR2 (= R3 de entrada); R2 = [0x4A]; CALL 93F1
    cpu.ar[2] = r3;
    cpu.r[2] = bus.readWord(kMatrixTempPtr);
    subs.call(0x93F1, cpu, bus);
    cpu.r[3] = r3;
    cpu.r[2] = r2;
    cpu.r[1] = r1;
    cpu.r[0] = ar0;
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
}

void eulerMatrix(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2];
    const uint32_t ar1 = cpu.ar[1], ar2 = cpu.ar[2], ar3 = cpu.ar[3];
    // 0934C  AR1 = [0x9342]: cossenos em [0..2], senos em [3..5]
    const uint32_t sc = bus.readWord(kEulerScratchPtr);
    for (uint32_t i = 0; i < 3; ++i) {
        cpu.r[2] = bus.readWord(r2 + i);
        subs.call(0x92E2, cpu, bus);
        bus.writeWord(sc + 3 + i, cpu.r[0]);
        subs.call(0x92DF, cpu, bus);
        bus.writeWord(sc + i, cpu.r[0]);
    }
    auto f = [&](uint32_t k) { return tmsToDouble(bus.readWord(sc + k)); };
    const double c0 = f(0), c1 = f(1), c2 = f(2), s0 = f(3), s1 = f(4), s2 = f(5);
    const double m[9] = {c1 * c2,
                         c1 * s2,
                         -s1,
                         s0 * s1 * c2 - s2 * c0,
                         s0 * s1 * s2 + c2 * c0,
                         c1 * s0,
                         s2 * s0 + c0 * c2 * s1,
                         c0 * s2 * s1 - c2 * s0,
                         c1 * c0};
    for (uint32_t i = 0; i < 9; ++i) bus.writeWord(ar2 + i, doubleToTms(m[i]));
    // 0936D  POP com a ordem trocada: R1 = R0, R2 = R1, R3 = R2 de entrada
    cpu.r[0] = doubleToTms(c2 * s0);
    cpu.r[1] = r0;
    cpu.r[2] = r1;
    cpu.r[3] = r2;
    cpu.ar[1] = ar1;
    cpu.ar[2] = ar2;
    cpu.ar[3] = ar3;
}

void identity(Tms32031State& cpu, MemoryBus& bus) {
    // 093D7  matriz identidade em AR0; preserva R0
    for (uint32_t i = 0; i < 9; ++i) bus.writeWord(cpu.ar[0] + i, doubleToTms(i % 4 == 0 ? 1.0 : 0.0));
}

void registerMatrix(SubroutineDispatcher& d) {
    d.add(0x9343, eulerMatrix);
    d.add(0x93D7, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { identity(c, b); });
    d.add(0x947F, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { matMul(c, b); });
    d.add(0x9481, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { matMulRegs(c, b); });
    d.add(0x93F1, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { matCopy(c, b); });
    d.add(0x9494, matMulInPlace);
}

} // namespace rea::core::math
