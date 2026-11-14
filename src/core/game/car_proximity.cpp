/**
 * @file car_proximity.cpp
 * @brief Tradução de `0x21D0`–`0x221E`, `0x238C`–`0x239A` e `0x25E8`–`0x2632`.
 */
#include "core/game/car_proximity.hpp"

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

using namespace car_vars;

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
/// Lê o float TMS em @p a.
double f(MemoryBus& bus, uint32_t a) { return tmsToDouble(bus.readWord(a)); }
/// Raio de colisão do objeto @p obj (`+0x1D`).
double radius(MemoryBus& bus, uint32_t obj) { return static_cast<double>(s(bus.readWord(obj + 0x1D))); }

/// Distância² no plano x/z entre os nós @p a e @p b.
double dist2(MemoryBus& bus, uint32_t a, uint32_t b) {
    const double dx = f(bus, b + 1) - f(bus, a + 1);
    const double dz = f(bus, b + 3) - f(bus, a + 3);
    return dz * dz + dx * dx;
}

/// true se o objeto @p obj ignora colisão.
bool noCollide(MemoryBus& bus, uint32_t obj) { return (bus.readWord(obj + 0xE) & kNoCollide) != 0; }

/// 02863  colisão de caixas: carry = 1 se colidem
bool boxes(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    subs.call(0x2863, cpu, bus);
    return cpu.carry();
}

/// 025E8  carro em foco × carros visíveis
void focusVsCars(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[0] = bus.readWord(kFocusCar);
    cpu.ar[1] = bus.readWord(kCarCellPtr) - kNext;
    cpu.ir0 = 3;
    for (;;) {
        cpu.ar[1] = bus.readWord(cpu.ar[1] + kNext);  // 02608
        if (cpu.ar[1] == 0) return;
        if (noCollide(bus, cpu.ar[1])) continue;
        const double r = radius(bus, cpu.ar[1]) + radius(bus, cpu.ar[0]);
        if (dist2(bus, cpu.ar[0], cpu.ar[1]) > r * r) continue;
        if (!boxes(cpu, bus, subs)) continue;  // BHS 02604
        // 025F9  fator do impacto pela soma das velocidades (+0x26 dos dados do carro)
        cpu.ar[4] = bus.readWord(cpu.ar[0] + 0x1B);
        cpu.ar[5] = bus.readWord(cpu.ar[1] + 0x1B);
        double v = f(bus, cpu.ar[4] + 0x26) + f(bus, cpu.ar[5] + 0x26);
        if (v > 100.0) v = 100.0;
        v = (100.0 - v) * shortFloat(0xA23D) + 1.0;
        cpu.r[0] = doubleToTms(v);
        bus.writeWord(kBumpFactor, cpu.r[0]);
        subs.call(0x2645, cpu, bus);  // BU 02645
        return;
    }
}

/// 021F2  cada carro de [E404] × a lista AR1
void carsVsObjects(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = bus.readWord(kCarList);
    for (cpu.ar[0] = cpu.r[0]; cpu.ar[0] != 0; cpu.ar[0] = bus.readWord(cpu.ar[0] + kNext)) {
        cpu.r[0] = bus.readWord(cpu.ar[0] + 0xF);
        if (cpu.r[0] == 0x270) continue;
        cpu.r[1] = bus.readWord(cpu.ar[0] + 0x1C);
        if (s(cpu.r[1]) < 0) continue;
        cpu.r[2] = 0x61A8 * 2;
        if (s(cpu.r[1]) > s(cpu.r[2])) continue;
        const uint32_t ar1 = cpu.ar[1], ar0 = cpu.ar[0];
        carVsObjects(cpu, bus, subs);
        cpu.ar[0] = ar0;
        cpu.ar[1] = ar1;
    }
}

/// 0238C  lista [E8EC]
void animList(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = bus.readWord(kAnimList);
    for (cpu.ar[2] = cpu.r[0]; cpu.ar[2] != 0; cpu.ar[2] = cpu.r[0]) {
        const bool idle = bus.readWord(cpu.ar[2] + 0x1F) == 0;
        const bool kind = (bus.readWord(cpu.ar[2] + 0xE) & 0x3000u) == 0x2000u;
        cpu.r[0] = bus.readWord(cpu.ar[2] + kNext);
        if (idle && kind) subs.call(0x7100, cpu, bus);
    }
}

} // namespace

void carVsObjects(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 02207  R2/R3 = posição do carro; R5 = raio²
    cpu.ar[1] -= kNext;
    cpu.ir0 = 3;
    double r = radius(bus, cpu.ar[0]);
    for (;;) {
        cpu.ar[1] = bus.readWord(cpu.ar[1] + kNext);  // 02218
        if (cpu.ar[1] == 0) return;
        if (dist2(bus, cpu.ar[0], cpu.ar[1]) > r * r) continue;
        cpu.r[0] = bus.readWord(cpu.ar[1] + 0xE);
        if (cpu.r[0] & kNoCollide) continue;
        subs.call(0x221F, cpu, bus);
        r = radius(bus, cpu.ar[0]);  // 02213: relê a posição e o raio (podem ter mudado)
    }
}

void carVsCars(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = bus.readWord(kCarList);
    cpu.ar[0] = cpu.ar[1] = cpu.r[0];
    while (cpu.ar[0] != 0) {
        // 02621
        if (!noCollide(bus, cpu.ar[0])) {
            cpu.ir0 = 3;
            for (;;) {
                cpu.ar[1] = bus.readWord(cpu.ar[1] + kNext);  // 02628
                if (cpu.ar[1] == 0) break;
                if (noCollide(bus, cpu.ar[1])) continue;
                const double r = radius(bus, cpu.ar[1]) + radius(bus, cpu.ar[0]);
                if (dist2(bus, cpu.ar[0], cpu.ar[1]) > r * r) continue;
                if (boxes(cpu, bus, subs)) {
                    subs.call(0x2645, cpu, bus);  // BLO 02645 (sai da 0x260E)
                    return;
                }
            }
        }
        // 0262D  próximo AR0; AR1 recomeça nele
        cpu.r[0] = bus.readWord(cpu.ar[0] + kNext);
        cpu.ar[1] = cpu.ar[0] = cpu.r[0];
    }
}

void carSimulation(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = bus.readWord(0xC8C5) & 0xFu;
    if (cpu.r[0] == 2) {
        if (bus.readWord(0xC8C5) & 0x20u) return;
        cpu.ar[7] = bus.readWord(kProcAr7Ptr);
        for (uint32_t a : {0x260Eu, 0x21F1u, 0x21EFu, 0x238Cu}) subs.call(a, cpu, bus);
        return;
    }
    if (cpu.r[0] != 4) return;
    cpu.ar[7] = bus.readWord(kProcAr7Ptr);
    for (uint32_t a : {0x25E8u, 0x260Eu, 0x21ECu, 0x21E9u, 0x21F1u, 0x21EFu, 0x691Bu, 0x238Cu}) subs.call(a, cpu, bus);
}

void registerCarProximity(SubroutineDispatcher& d) {
    d.add(0x21D0, carSimulation);
    d.add(0x2207, carVsObjects);
    d.add(0x260E, carVsCars);
    d.add(0x25E8, focusVsCars);
    d.add(0x238C, animList);
    d.add(0x21E9, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[0] = b.readWord(kFocusCar);
        c.ar[1] = b.readWord(kObjCell2Ptr);
        carVsObjects(c, b, s);
    });
    d.add(0x21EC, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[0] = b.readWord(kFocusCar);
        c.ar[1] = b.readWord(kObjCellPtr);
        carVsObjects(c, b, s);
    });
    d.add(0x21EF, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[1] = b.readWord(kObjCell2Ptr);
        carsVsObjects(c, b, s);
    });
    d.add(0x21F1, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[1] = b.readWord(kObjCellPtr);
        carsVsObjects(c, b, s);
    });
}

} // namespace rea::core::game
