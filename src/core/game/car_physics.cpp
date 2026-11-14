/**
 * @file car_physics.cpp
 * @brief Tradução de `0x2C39`–`0x2C45`, `0x1FF6`–`0x20B3`, `0x20D4`–`0x20FE` e
 *        `0x67E3`–`0x6821`.
 */
#include "core/game/car_physics.hpp"

#include <cmath>

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

constexpr uint32_t kTriPtrs = 0x1F86; ///< `[1F86]` = 0xE577: ponteiros dos 3 pontos.
constexpr uint32_t kNormal  = 0x1F87; ///< `[1F87]` = 0xE57B: normal.
constexpr uint32_t kBasis   = 0x1F8A; ///< `[1F8A]` = 0xE584: base 3×3.
constexpr uint32_t kOffsets = 0x47;   ///< D + 0x47: offsets locais dos pontos.
constexpr uint32_t kPoints  = 5;  ///< Pontos de contato do carro.

/// Lê o float TMS em @p a e o converte para double.
double rf(MemoryBus& b, uint32_t a) { return tmsToDouble(b.readWord(a)); }
/// Converte @p v para float TMS e grava em @p a.
void wf(MemoryBus& b, uint32_t a, double v) { b.writeWord(a, doubleToTms(v)); }
/// Converte o inteiro com sinal @p v para double (o `FLOAT` do C31).
double fl(uint32_t v) { return static_cast<double>(static_cast<int32_t>(v)); }

/// Liga ou desliga o bit C (carry) de ST, que a rotina original usa como retorno de sucesso ou falha.
void setCarry(Tms32031State& c, bool on) {
    if (on) c.st |= Tms32031State::kStCarry;
    else c.st &= ~Tms32031State::kStCarry;
}

/// 02074  procura o chão dos 5 pontos de D (AR6) nos objetos da lista R0 perto do carro AR4.
void scanList(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t d = cpu.ar[6];
    const uint32_t car = cpu.ar[4];
    for (uint32_t o = cpu.r[0]; o != 0; o = bus.readWord(o + 0x20)) {
        cpu.ar[2] = o;
        const double dx = rf(bus, o + 1) - rf(bus, car + 1);
        const double dz = rf(bus, o + 3) - rf(bus, car + 3);
        const double r = fl(bus.readWord(o + 0x1D)) + fl(bus.readWord(car + 0x1D));
        if (!(dx * dx + dz * dz < r * r)) continue;
        // 0208B
        for (uint32_t i = 0; i < kPoints; ++i) {
            const uint32_t p = d + 6 * i;
            cpu.ar[4] = p;
            cpu.ar[2] = o;
            bool take = false;
            if (bus.readWord(p + 5) == 0) {
                subs.call(0x20B4, cpu, bus);
                take = cpu.carry();
            } else {
                const double cur = rf(bus, p + 3);
                if (std::abs(cur) < 2000.0) continue;
                subs.call(0x20B4, cpu, bus);
                if (!cpu.carry()) continue;
                const double h = tmsToDouble(cpu.r[0]);
                if (cur >= 0.0) take = h >= 0.0 ? !(cur < h) : h > -1000.0;
                else take = h >= 0.0 || !(cur > h);
            }
            if (take) {
                wf(bus, p + 3, tmsToDouble(cpu.r[0]));
                bus.writeWord(p + 5, o);
            }
        }
        cpu.ar[4] = car;
    }
    cpu.ar[2] = 0;
    cpu.ir1 = 3;
}

/// 02041  posiciona os pontos de contato de D = R3 pelo objeto AR4 e procura o chão.
void placePoints(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t d = cpu.r[3];
    const uint32_t obj = cpu.ar[4];
    cpu.ar[6] = d;
    const double x = rf(bus, obj + 1), y = rf(bus, obj + 2), z = rf(bus, obj + 3);
    for (uint32_t i = 0; i < kPoints; ++i) {
        const uint32_t p = d + 6 * i;
        cpu.r[2] = obj + 4;
        cpu.ar[2] = d + kOffsets + 3 * i;
        cpu.r[3] = p;
        subs.call(0x941E, cpu, bus);
        wf(bus, p, rf(bus, p) + x);
        wf(bus, p + 1, rf(bus, p + 1) + y);
        wf(bus, p + 3, 0.0);
        bus.writeWord(p + 5, 0);
        wf(bus, p + 2, rf(bus, p + 2) + z);
    }
    bus.writeWord(d + 0x1E, 0);
    cpu.r[0] = bus.readWord(0xE403);
    subs.call(0x2074, cpu, bus);
    uint32_t any = 0;
    uint32_t free = 0;
    for (uint32_t i = 0; i < kPoints; ++i) {
        const uint32_t hit = bus.readWord(d + 6 * i + 5);
        any |= hit;
        if (hit == 0) free = i + 1;
    }
    bus.writeWord(d + 0x1E, any != 0 ? 1u : 0u);
    if (free != 0) {
        cpu.r[0] = bus.readWord(0xE406);
        subs.call(0x2074, cpu, bus);
    }
}

/// 020D4  atitude do objeto AR4 pelos 3 pontos `[E577..E579]`; D = AR6.
void attitude(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[2] = bus.readWord(kTriPtrs);
    cpu.ar[0] = bus.readWord(kNormal);
    subs.call(0x9466, cpu, bus);
    const uint32_t n = cpu.ar[0];
    cpu.ar[2] = n;
    subs.call(0x943B, cpu, bus);
    const uint32_t m = bus.readWord(kBasis);
    const double nx = rf(bus, n), ny = rf(bus, n + 1), nz = rf(bus, n + 2);
    wf(bus, m + 3, nx);
    wf(bus, m + 4, ny);
    wf(bus, m + 5, nz);
    wf(bus, m, ny);
    wf(bus, m + 1, -nx);
    wf(bus, m + 2, 0.0);
    cpu.ar[2] = m;
    subs.call(0x943B, cpu, bus);
    const double ax = rf(bus, m), ay = rf(bus, m + 1), az = rf(bus, m + 2);
    const double bx = rf(bus, m + 3), by = rf(bus, m + 4), bz = rf(bus, m + 5);
    wf(bus, m + 6, bz * ay - by * az);
    wf(bus, m + 7, bx * az - bz * ax);
    wf(bus, m + 8, by * ax - bx * ay);
    cpu.ar[2] = m + 6;
    subs.call(0x943B, cpu, bus);
    cpu.r[2] = m;
    cpu.ar[2] = cpu.ar[4] + 4;
    subs.call(0x93FD, cpu, bus);
    const uint32_t d = cpu.ar[6];
    wf(bus, cpu.ar[4] + 2, rf(bus, d + 1) - rf(bus, d + 0x48));
}

/// 01FF6  física vertical do objeto AR4 com D = R3. Preserva R4, R5, AR3–AR6.
void verticalPhysics(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r4 = cpu.r[4], r5 = cpu.r[5];
    const uint32_t ar3 = cpu.ar[3], ar4 = cpu.ar[4], ar5 = cpu.ar[5], ar6 = cpu.ar[6];
    subs.call(0x2041, cpu, bus);
    cpu.ar[4] = ar4;
    const uint32_t d = cpu.ar[6];
    if (const uint32_t g = bus.readWord(d + 5); g != 0) bus.writeWord(d + 0x1E, bus.readWord(g + 0xF) & 0xFF0u);
    const double grav = static_cast<double>(static_cast<int32_t>(bus.readWord(0xC92F))) * 4.0;
    for (uint32_t i = 0; i < kPoints; ++i) {
        const uint32_t p = d + 6 * i;
        const double h = rf(bus, p + 3);
        if (!(h > -9.0)) {
            wf(bus, p + 1, h - 9.0 + rf(bus, p + 1));
            wf(bus, p + 4, 0.0);
            continue;
        }
        double v = rf(bus, p + 4) + grav;
        if (const uint32_t g = bus.readWord(p + 5); g != 0 && (bus.readWord(g + 0xF) & 0xFF0u) != 0x330u)
            v += grav * 4.0;
        if (!(h > v)) v = h;
        wf(bus, p + 4, v);
        wf(bus, p + 1, v + rf(bus, p + 1));
    }
    bus.writeWord(d + 0x20, rf(bus, d + 3) < 72.0 ? 0u : 1u);
    bus.writeWord(d + 0x1F, (rf(bus, d + 9) < 72.0 || rf(bus, d + 0xF) < 72.0) ? 0u : 1u);
    const uint32_t t = bus.readWord(kTriPtrs);
    bus.writeWord(t, d);
    bus.writeWord(t + 1, d + 6);
    bus.writeWord(t + 2, d + 12);
    subs.call(0x20D4, cpu, bus);
    cpu.ar[6] = ar6; cpu.ar[5] = ar5; cpu.ar[4] = ar4; cpu.ar[3] = ar3;
    cpu.r[5] = r5; cpu.r[4] = r4;
}

/// 067F8  R0 = distância com sinal do carro AR4 à reta do nó `[AR5+0x37]`; carry se < 0.
void lateralDistance(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3];
    const uint32_t ar0 = cpu.ar[0], ar2 = cpu.ar[2], ar4 = cpu.ar[4], ar5 = cpu.ar[5];
    const uint32_t node = bus.readWord(cpu.ar[5] + 0x37);
    const uint32_t next = bus.readWord(node + 0x21);
    bool neg = true;
    if (next != 0) {
        const double a = rf(bus, node + 3) - rf(bus, next + 3);
        const double b = rf(bus, next + 1) - rf(bus, node + 1);
        const double c = -a * rf(bus, node + 1) + -b * rf(bus, node + 3);
        cpu.r[2] = doubleToTms(a * a + b * b);
        subs.call(0xA5F1, cpu, bus);
        cpu.r[1] = cpu.r[0];
        cpu.r[0] = doubleToTms(a * rf(bus, ar4 + 1) + b * rf(bus, ar4 + 3) + c);
        subs.call(0xA509, cpu, bus);
        neg = tmsToDouble(cpu.r[0]) < 0.0;
    }
    setCarry(cpu, neg);
    cpu.ar[5] = ar5; cpu.ar[4] = ar4; cpu.ar[2] = ar2; cpu.ar[0] = ar0;
    cpu.r[3] = r3; cpu.r[2] = r2; cpu.r[1] = r1;
}

/// 02185  caixa do modelo `[AR4+0xD]` → offsets dos pontos de contato e limites em D = `[AR4+0x1B]`.
///        Preserva R3–R6 e AR0.
void modelBox(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    uint32_t a = bus.readWord(cpu.ar[4] + 0xD) + 1;
    const uint32_t n = bus.readWord(a++) & 0xFFu;
    auto vertex = [&](double& x, double& y, double& z) {
        const uint32_t w = bus.readWord(a++);
        x = static_cast<double>(static_cast<int16_t>(w & 0xFFFFu));
        y = static_cast<double>(static_cast<int16_t>(w >> 16));
        z = fl(bus.readWord(a++));
    };
    double maxX, maxY, maxZ;
    vertex(maxX, maxY, maxZ);
    double minX = maxX, minY = maxY, minZ = maxZ;
    for (uint32_t i = 0; i < n; ++i) {
        double x, y, z;
        vertex(x, y, z);
        if (x > maxX) maxX = x;
        if (x < minX) minX = x;
        if (y > maxY) maxY = y;
        if (y < minY) minY = y;
        if (z > maxZ) maxZ = z;
        if (z < minZ) minZ = z;
    }
    const uint32_t d = bus.readWord(cpu.ar[4] + 0x1B);
    const double off[5][3] = {{0.0, maxY, 0.0}, {maxX, maxY, maxZ}, {minX, maxY, maxZ}, {minX, maxY, minZ},
                              {maxX, maxY, minZ}};
    for (uint32_t i = 0; i < kPoints; ++i)
        for (uint32_t k = 0; k < 3; ++k) wf(bus, d + kOffsets + 3 * i + k, off[i][k]);
    wf(bus, d + 0x3B, minX + 25.0);
    wf(bus, d + 0x3D, minY);
    wf(bus, d + 0x3F, minZ);
    wf(bus, d + 0x3A, maxX - 25.0);
    wf(bus, d + 0x3C, maxY);
    wf(bus, d + 0x3E, maxZ);
    cpu.r[0] = doubleToTms(minX + 25.0);
    cpu.r[1] = doubleToTms(minY);
    cpu.r[2] = doubleToTms(minZ);
}

/// 02C39  física do carro AR4 (D = AR5): pontos de contato, rumo, matriz, distância lateral.
void carPhysics(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[3] = bus.readWord(cpu.ar[4] + 0x1B);
    subs.call(0x1FF6, cpu, bus);
    cpu.r[2] = bus.readWord(cpu.ar[5] + 0x2C);
    bus.writeWord(cpu.ar[4] + 0x15, cpu.r[2]);
    cpu.ar[2] = bus.readWord(0x5A);
    subs.call(0x938A, cpu, bus);
    cpu.r[2] = cpu.ar[4] + 4;
    cpu.r[3] = cpu.r[2];
    subs.call(0x9494, cpu, bus);
    subs.call(0x67E3, cpu, bus);
    bus.writeWord(cpu.ar[5] + 0x46, cpu.r[0]);
}

} // namespace

void registerCarPhysics(SubroutineDispatcher& d) {
    d.add(0x2C39, carPhysics);
    d.add(0x2185, modelBox);
    d.add(0x1FF6, verticalPhysics);
    d.add(0x2041, placePoints);
    d.add(0x2074, scanList);
    d.add(0x20D4, attitude);
    d.add(0x67E3, lateralDistance);
    d.add(0x67EC, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t ar4 = c.ar[4], ar5 = c.ar[5];
        c.ar[4] = b.readWord(0xE876);
        if (c.ar[4] == 0) {
            c.st |= Tms32031State::kStCarry;
        } else {
            c.ar[5] = b.readWord(c.ar[4] + 0x1B);
            lateralDistance(c, b, s);
        }
        c.ar[4] = ar4;
        c.ar[5] = ar5;
    });
}

} // namespace rea::core::game
