/**
 * @file ground_height.cpp
 * @brief Tradução de `0x1F8B`–`0x1FB4`, `0x20B4`–`0x20D3`, `0x20FF`–`0x2184` e `0x9466`.
 */
#include "core/track/ground_height.hpp"

#include <cmath>

#include "core/tms_float.hpp"

namespace rea::core::track {

namespace {

constexpr uint32_t kRoadList  = 0xE403; ///< Lista de trechos de estrada (tipo 0x300).
constexpr uint32_t kOtherList = 0xE406; ///< Lista de objetos tipo 0x900.
constexpr uint32_t kTriPtrs   = 0x1F86; ///< `[1F86]` = 0xE577: ponteiros dos vértices a, b, c.
constexpr uint32_t kNormal    = 0x1F87; ///< `[1F87]` = 0xE57B: normal do polígono.
constexpr uint32_t kVerts     = 0x004F; ///< `[0x4F]`: vértices transformados.
constexpr uint32_t kOffset    = 0x004C; ///< `[0x4C]`: centro do objeto − ponto (em −1..+1).
constexpr uint32_t kRawVert   = 0x0051; ///< `[0x51]`: vértice lido (em −1..+1).

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

/// 020FF  ponto R2 dentro de algum polígono do objeto AR2 (projeção XZ)? Carry = 1 se sim.
void pointInMesh(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    const uint32_t obj = cpu.ar[2];
    uint32_t mdl = bus.readWord(obj + 0xD) + 1;
    const uint32_t mtx = obj + 4;
    const uint32_t out = bus.readWord(kVerts);
    const uint32_t off = bus.readWord(kOffset);
    const uint32_t raw = bus.readWord(kRawVert);
    const uint32_t pt = cpu.r[2];
    const double ox = rf(bus, obj + 1) - rf(bus, pt);
    const double oy = rf(bus, obj + 2) - rf(bus, pt + 1);
    const double oz = rf(bus, obj + 3) - rf(bus, pt + 2);
    wf(bus, off - 1, ox);
    wf(bus, off, oy);
    wf(bus, off + 1, oz);
    double m[9];
    for (int i = 0; i < 9; ++i) m[i] = rf(bus, mtx + i);

    const uint32_t hdr = bus.readWord(mdl++);
    uint32_t o = out;
    for (uint32_t n = (hdr & 0xFFu) + 1; n != 0; --n) {
        const uint32_t xy = bus.readWord(mdl++);
        const double x = static_cast<double>(static_cast<int16_t>(xy & 0xFFFFu));
        const double y = static_cast<double>(static_cast<int16_t>(xy >> 16));
        const double z = fl(bus.readWord(mdl++));
        wf(bus, raw - 1, x);
        wf(bus, raw, y);
        wf(bus, raw + 1, z);
        wf(bus, o++, x * m[0] + y * m[1] + z * m[2] + ox);
        wf(bus, o++, x * m[3] + y * m[4] + z * m[5] + oy);
        wf(bus, o++, x * m[6] + y * m[7] + z * m[8] + oz);
    }

    // 02134  faces
    mdl += 1;
    auto vx = [&](uint32_t i) { return rf(bus, out + i); };
    auto vz = [&](uint32_t i) { return rf(bus, out + i + 2); };
    // Aresta p→q (SUBF3 a,b,d = a − b): fora se x_p·z_q − z_p·x_q > |x_p − x_q|.
    auto outside = [&](uint32_t p, uint32_t q) {
        const double r0 = vz(q) - vz(p);
        const double r1 = vx(p) - vx(q);
        return vx(p) * r0 + vz(p) * r1 > std::fabs(r1);
    };
    for (uint32_t n = (hdr >> 16) + 1; n != 0; --n) {
        const uint32_t f = bus.readWord(mdl);
        mdl += 5;
        const uint32_t a = (f & 0xFFu) * 3;
        const uint32_t b = ((f >> 8) & 0xFFu) * 3;
        const uint32_t c = ((f >> 16) & 0xFFu) * 3;
        const uint32_t d = (f >> 24) * 3;
        if (c != d && outside(c, d)) continue;
        if (outside(d, a) || outside(a, b) || outside(b, c)) continue;
        // 02175
        bus.writeWord(0xE577, out + a);
        bus.writeWord(0xE578, out + b);
        bus.writeWord(0xE579, out + c);
        setCarry(cpu, true);
        return;
    }
    setCarry(cpu, false);
}

/// 09466  normal do triângulo [AR2] = (a, b, c) em AR0.
void triangleNormal(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    const uint32_t a = bus.readWord(cpu.ar[2]);
    const uint32_t b = bus.readWord(cpu.ar[2] + 1);
    const uint32_t c = bus.readWord(cpu.ar[2] + 2);
    // SUBF3 a,b,d = a − b: R0..R2 = a − b, R3..R5 = c − b.
    const double r0 = rf(bus, a) - rf(bus, b);
    const double r1 = rf(bus, a + 1) - rf(bus, b + 1);
    const double r2 = rf(bus, a + 2) - rf(bus, b + 2);
    const double r3 = rf(bus, c) - rf(bus, b);
    const double r4 = rf(bus, c + 1) - rf(bus, b + 1);
    const double r5 = rf(bus, c + 2) - rf(bus, b + 2);
    cpu.ar[2] = c + 3;
    cpu.r[0] = doubleToTms(r0);
    cpu.r[1] = doubleToTms(r1);
    cpu.r[2] = doubleToTms(r2);
    cpu.r[3] = doubleToTms(r3);
    cpu.r[4] = doubleToTms(r4);
    cpu.r[5] = doubleToTms(r5);
    wf(bus, cpu.ar[0], r5 * r1 - r4 * r2);
    wf(bus, cpu.ar[0] + 1, r3 * r2 - r5 * r0);
    const double z = r4 * r0 - r3 * r1;
    cpu.r[6] = doubleToTms(z);
    wf(bus, cpu.ar[0] + 2, z);
}

/// 020B4  altura do plano do objeto AR2 sob o ponto AR4 (R0, carry = 1) ou carry = 0.
void planeHeight(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[2] = cpu.ar[4];
    subs.call(0x20FF, cpu, bus);
    if (!cpu.carry()) return;
    const uint32_t r4 = cpu.r[4], r5 = cpu.r[5], ar2 = cpu.ar[2];
    const uint32_t r6 = cpu.r[6], r7 = cpu.r[7];
    cpu.ar[2] = bus.readWord(kTriPtrs);
    cpu.ar[0] = bus.readWord(kNormal);
    subs.call(0x9466, cpu, bus);
    const uint32_t a = bus.readWord(bus.readWord(kTriPtrs));
    const uint32_t n = cpu.ar[0];
    const double dot = rf(bus, n) * rf(bus, a) + rf(bus, n + 1) * rf(bus, a + 1) +
                       rf(bus, n + 2) * rf(bus, a + 2);
    cpu.ar[0] = n + 2;
    cpu.ar[1] = a + 3;
    cpu.r[1] = doubleToTms(-rf(bus, n + 1));
    cpu.r[0] = doubleToTms(-dot);
    subs.call(0xA509, cpu, bus);
    cpu.r[7] = r7;
    cpu.r[6] = r6;
    cpu.ar[2] = ar2;
    cpu.r[5] = r5;
    cpu.r[4] = r4;
    setCarry(cpu, true);
}

/// 01F8F  procura na lista R0 um objeto cujo polígono contenha o ponto AR4.
void searchList(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[2] = cpu.r[0];
    while (cpu.ar[2] != 0) {
        const double dx = rf(bus, cpu.ar[2] + 1) - rf(bus, cpu.ar[4]);
        const double dz = rf(bus, cpu.ar[2] + 3) - rf(bus, cpu.ar[4] + 2);
        const double r = fl(bus.readWord(cpu.ar[2] + 0x1D));
        if (dx * dx + dz * dz < r * r) {
            subs.call(0x20B4, cpu, bus);
            if (cpu.carry()) return;
        }
        cpu.ar[2] = bus.readWord(cpu.ar[2] + 0x20);
    }
    cpu.ir1 = 3;
    cpu.r[0] = doubleToTms(0.0);
    setCarry(cpu, false);
}

/// 01F8B  altura do chão sob AR4: `[E403]`, depois `[E406]`.
void groundHeight(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = bus.readWord(kRoadList);
    subs.call(0x1F8F, cpu, bus);
    if (cpu.carry()) return;
    cpu.r[0] = bus.readWord(kOtherList);
    subs.call(0x1F8F, cpu, bus);
}

} // namespace

void registerGroundHeight(SubroutineDispatcher& d) {
    d.add(0x1F8B, groundHeight);
    d.add(0x1F8F, searchList);
    d.add(0x1FB0, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[4] += 1;
        s.call(0x1F8B, c, b);
        c.ar[4] -= 1;
    });
    d.add(0x20B4, planeHeight);
    d.add(0x20FF, pointInMesh);
    d.add(0x9466, triangleNormal);
}

} // namespace rea::core::track
