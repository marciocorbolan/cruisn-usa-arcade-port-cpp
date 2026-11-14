/**
 * @file sky_renderer.cpp
 * @brief Tradução de `0x822B`–`0x831A` e `0x839B`–`0x83D0`.
 */
#include "core/video/sky_renderer.hpp"

#include "core/tms_float.hpp"

namespace rea::core::video {

namespace {

constexpr uint32_t kDmaFifo   = 0x600000;  ///< Fila de DMA de quads.
constexpr uint32_t kDmaStatus = 0x980082;  ///< Status do vídeo; aqui se testa o bit 2 (fila de quads ocupada).
constexpr uint32_t kDmaGo     = 0x980083;  ///< Ler esta porta dispara o desenho do quad da fila (`0x980082` só informa o status).
constexpr uint32_t kYaw       = 0x809807;  ///< Rumo da câmera (página 0x80 do DP).
constexpr uint32_t kSkyPoints = 0x831B;  ///< `[831B]` = 0x831C: pontos do céu (x, y, z).
constexpr uint32_t kSkyTex    = 0x8382;  ///< `[8382]` = 0x8383: (textura, palavra final) por quad.
constexpr uint32_t kProjected = 0x004F;  ///< `[0x4F]` = 0x809D00.

/// Lê o float TMS em @p a e o converte para double.
double rf(MemoryBus& b, uint32_t a) { return tmsToDouble(b.readWord(a)); }
/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
int32_t si(uint32_t v) { return static_cast<int32_t>(v); }
/// Lê o float TMS em @p a e o converte para inteiro (`FIX`).
uint32_t fixAt(MemoryBus& b, uint32_t a) { return tmsFix(rf(b, a)); }

/// Espera a fila de quads esvaziar (`[0x980082] & 4`).
void waitDma(Tms32031State& cpu, MemoryBus& bus) {
    do {
        cpu.r[0] = bus.readWord(kDmaStatus) & 4u;
    } while (cpu.r[0] != 0);
}

/// Dispara o quad (leitura de 0x980083) e a leitura descartada de *AR1.
void fire(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[0] = bus.readWord(kDmaGo);
    cpu.r[0] = bus.readWord(cpu.ar[1]);
}

/// Os 4 cantos do quad (pontos 0, 1, 3, 2 a partir de AR6); AR6 += 6.
void corners(Tms32031State& cpu, MemoryBus& bus, int32_t dx, int32_t dy) {
    const uint32_t p = cpu.ar[6];
    bus.writeWord(cpu.ar[5], fixAt(bus, p) - static_cast<uint32_t>(dx));
    bus.writeWord(cpu.ar[5], fixAt(bus, p + 1) - static_cast<uint32_t>(dy));
    bus.writeWord(cpu.ar[5], fixAt(bus, p + 3));
    bus.writeWord(cpu.ar[5], fixAt(bus, p + 4));
    bus.writeWord(cpu.ar[5], fixAt(bus, p + 9));
    bus.writeWord(cpu.ar[5], fixAt(bus, p + 0xA));
    bus.writeWord(cpu.ar[5], fixAt(bus, p + 6) - static_cast<uint32_t>(dx));
    bus.writeWord(cpu.ar[5], fixAt(bus, p + 7) - static_cast<uint32_t>(dy));
    cpu.ar[6] += 6;
}

/// 0839B  projeta AR4 + 1 pontos de AR5 em AR6 (R6 = rolagem; R7 = [E3FD]).
void projectPoints(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[7] = bus.readWord(0xE3FD);
    do {
        cpu.ar[2] = bus.readWord(0x5D);
        double x = rf(bus, cpu.ar[5]++) + tmsToDouble(cpu.r[6]);
        if (!(x < rf(bus, 0x8226))) x -= rf(bus, 0x8226);
        bus.writeWord(cpu.ar[2], doubleToTms(x));
        bus.writeWord(cpu.ar[2] + 1, doubleToTms(rf(bus, cpu.ar[5]++) + tmsToDouble(cpu.r[7])));
        bus.writeWord(cpu.ar[2] + 2, bus.readWord(cpu.ar[5]++));
        cpu.r[3] = cpu.ar[6];
        cpu.r[2] = bus.readWord(0x5A);
        subs.call(0x941E, cpu, bus);
        cpu.r[0] = doubleToTms(rf(bus, cpu.ar[6]) + rf(bus, 0x53));
        bus.writeWord(cpu.ar[6]++, cpu.r[0]);
        cpu.r[0] = doubleToTms(rf(bus, cpu.ar[6]) * shortFloat(0x0052) + rf(bus, 0x54));
        bus.writeWord(cpu.ar[6], cpu.r[0]);
        cpu.ar[6] += 2;
        cpu.ar[4] -= 1;
    } while (si(cpu.ar[4]) >= 0);
}

/// 08279..082CB  quad de céu recortado pela linha do chão (modo 4). @return false → sem recorte.
bool clippedQuad(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t ar0 = cpu.ar[0], ar1 = cpu.ar[1], bk = cpu.bk;
    cpu.ar[0] = cpu.ar[6] + 9;
    cpu.ar[1] = cpu.ar[6] + 6;
    cpu.ar[2] = bus.readWord(0x5D);
    subs.call(0x94C9, cpu, bus);
    cpu.ar[0] = cpu.ar[2];
    cpu.ar[1] = bus.readWord(0x5F);
    bus.writeWord(cpu.ar[1], bus.readWord(0xE3FA));
    bus.writeWord(cpu.ar[1] + 1, bus.readWord(0xE3F9));
    subs.call(0x94B4, cpu, bus);
    cpu.bk = bk;
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
    if (tmsToDouble(cpu.r[0]) < 0.0) return false;
    cpu.r[3] = tmsFix(tmsToDouble(cpu.r[0]));
    const uint32_t p = cpu.ar[6];
    const double dx = rf(bus, p + 3) - rf(bus, p), dy = rf(bus, p + 4) - rf(bus, p + 1);
    if (dx == 0.0 || dy == 0.0) {
        cpu.r[0] = doubleToTms(0.0);
    } else {
        cpu.r[0] = doubleToTms(dx);
        cpu.r[1] = doubleToTms(dy);
        subs.call(0xA509, cpu, bus);
    }
    cpu.r[4] = tmsFix(tmsToDouble(cpu.r[0]));
    corners(cpu, bus, si(cpu.r[4]), si(cpu.r[3]));
    // 082B0  u/v com o v deslocado pelo recorte
    cpu.r[4] = static_cast<uint32_t>(-0x200);
    auto shifted = [&](uint32_t v) { return (((v >> 8) - cpu.r[3]) << 8) | (v & 0xFFu); };
    bus.writeWord(cpu.ar[5], shifted(cpu.r[4]));
    bus.writeWord(cpu.ar[5], cpu.r[5]);
    bus.writeWord(cpu.ar[5], cpu.r[6]);
    bus.writeWord(cpu.ar[5], shifted(cpu.r[7]));
    return true;
}

/// 081D9  ponto mais alto do horizonte: entre os nós `[E46F]` (link +0x21) com `(+E & 0x3000) ==
/// 0x1000`, projeta (pos − câmera `[809800..2]`) pelas linhas 0 e 1 da matriz `[0x47]`, com a escala
/// de profundidade `[[0x52] + (+1C >> 4)]` e o centro `[0x53]`/`[0x54]`; guarda o menor y com x em
/// 0..512. `[E3F9]` = y + `[E3FB]` (mínimo 0, ou 512 sem candidatos) e `[E3FA]` = x.
/// Preserva R0–R3, R6, R7 e AR2.
void horizonPoint(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3], r6 = cpu.r[6],
                   r7 = cpu.r[7], ar2 = cpu.ar[2];
    double y6 = 512.0, x7 = 0.0;
    cpu.r[6] = doubleToTms(y6);
    bus.writeWord(0xE3F9, cpu.r[6]);
    cpu.ar[2] = bus.readWord(0xE46F);
    if (cpu.ar[2] != 0) {
        do {
            const uint32_t n = cpu.ar[2];
            if ((bus.readWord(n + 0xE) & 0x3000u) != 0x1000u) continue;
            cpu.ar[1] = bus.readWord(0x5D);
            double v[3];
            for (uint32_t i = 0; i < 3; ++i) {
                v[i] = rf(bus, n + 1 + i) - rf(bus, 0x809800 + i);
                bus.writeWord(cpu.ar[1] + i, doubleToTms(v[i]));
            }
            const uint32_t m = bus.readWord(0x47);
            double x = 0.0, y = 0.0;
            for (uint32_t i = 0; i < 3; ++i) {
                x += rf(bus, m + i) * v[i];
                y += rf(bus, m + 3 + i) * v[i];
            }
            int32_t depth = si(bus.readWord(n + 0x1C));
            if (depth <= 0) continue;
            depth = static_cast<int32_t>(static_cast<uint32_t>(depth) >> 4);
            if (depth >= 0x1387) continue;
            cpu.ar[1] = static_cast<uint32_t>(depth) + bus.readWord(0x52);
            const double scale = rf(bus, cpu.ar[1]);
            const double sx = x * scale + rf(bus, 0x53);
            if (sx < 0.0 || sx > 512.0) continue;
            const double sy = y * scale + rf(bus, 0x54);
            if (y6 > sy) {
                y6 = sy;
                x7 = sx;
            }
        } while ((cpu.ar[2] = bus.readWord(cpu.ar[2] + 0x21)) != 0);
        y6 += rf(bus, 0xE3FB);
        if (y6 < 0.0) y6 = 0.0;
        bus.writeWord(0xE3F9, doubleToTms(y6));
        bus.writeWord(0xE3FA, doubleToTms(x7));
    }
    cpu.ar[2] = ar2;
    cpu.r[7] = r7;
    cpu.r[6] = r6;
    cpu.r[3] = r3;
    cpu.r[2] = r2;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}

void skyScroll(Tms32031State& cpu, MemoryBus& bus);

/// 0822B  desenha o céu e a faixa de chão.
void drawSky(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = bus.readWord(0xC8C5);
    if (cpu.r[0] & 0x400u) return;
    if ((cpu.r[0] & 0xFu) == 4) subs.call(0x81D9, cpu, bus);
    // 08231  yaw em [0, 2π)
    cpu.r[2] = bus.readWord(kYaw);
    const double yaw = tmsToDouble(cpu.r[2]);
    if (yaw < 0.0 || !(yaw < rf(bus, 0x9304))) {
        subs.call(0x9313, cpu, bus);
        bus.writeWord(kYaw, cpu.r[2]);
    }
    bus.writeWord(0xE3F8, cpu.r[2]);
    skyScroll(cpu, bus);
    // 08246  [5A] = câmera × rotY(−yaw)
    cpu.ar[2] = bus.readWord(0x5A);
    cpu.r[2] = doubleToTms(-rf(bus, 0xE3F8));
    subs.call(0x938A, cpu, bus);
    cpu.r[3] = cpu.ar[2];
    cpu.r[2] = bus.readWord(0x47);
    subs.call(0x9494, cpu, bus);
    // 0824C  linha de recorte do chão
    double cut = rf(bus, 0xE3F9) + 25.0 - (rf(bus, 0xE3FD) + rf(bus, 0x54));
    if (cut < 0.0) cut = 0.0;
    if ((bus.readWord(0xC8C5) & 0xFu) != 4) cut = 0.0;
    bus.writeWord(0xE3FC, doubleToTms(cut));
    cpu.ar[5] = bus.readWord(kSkyPoints);
    cpu.ar[6] = bus.readWord(kProjected);
    cpu.ar[4] = 0x21;
    subs.call(0x839B, cpu, bus);
    // 0825B  12 quads de céu
    cpu.ar[2] = 5;
    subs.call(0x9ECD, cpu, bus);
    cpu.ar[2] = cpu.r[0];
    cpu.ar[5] = kDmaFifo;
    cpu.ar[0] = bus.readWord(kSkyTex);
    cpu.r[4] = static_cast<uint32_t>(-0x200);
    cpu.r[5] = 0;
    cpu.r[6] = 0xFF;
    cpu.r[7] = static_cast<uint32_t>(-0x101);
    cpu.r[3] = tmsFix(rf(bus, 0xE3FC));
    cpu.ar[4] = 0xB;
    cpu.ar[6] = bus.readWord(kProjected);
    cpu.ar[1] = 0;
    do {
        waitDma(cpu, bus);
        bus.writeWord(cpu.ar[5], 0x100);
        cpu.ar[2] = bus.readWord(cpu.ar[0]++);
        subs.call(0x9ECD, cpu, bus);
        bus.writeWord(cpu.ar[5], cpu.r[0]);
        const uint32_t mode = bus.readWord(0xC8C5);
        const bool clip = !(mode & 0x80u) && (mode & 0xFu) == 4 && clippedQuad(cpu, bus, subs);
        if (!clip) {
            // 082CD
            corners(cpu, bus, 0, 0);
            bus.writeWord(cpu.ar[5], cpu.r[4]);
            bus.writeWord(cpu.ar[5], cpu.r[5]);
            bus.writeWord(cpu.ar[5], cpu.r[6]);
            bus.writeWord(cpu.ar[5], cpu.r[7]);
        }
        bus.writeWord(cpu.ar[5], bus.readWord(cpu.ar[0]++));
        fire(cpu, bus);
        cpu.ar[4] -= 1;
    } while (si(cpu.ar[4]) >= 0);
    // 082EB  3 quads de chão (slot 5), pontos a partir do 26º
    cpu.ar[2] = 5;
    subs.call(0x9ECD, cpu, bus);
    cpu.ar[2] = cpu.r[0];
    cpu.ar[0] = bus.readWord(kSkyTex);
    cpu.ar[4] = 2;
    cpu.ar[6] = bus.readWord(kProjected) + 0x4E;
    cpu.ar[1] = 0;
    do {
        waitDma(cpu, bus);
        bus.writeWord(cpu.ar[5], 0x1E);
        bus.writeWord(cpu.ar[5], cpu.ar[2]);
        corners(cpu, bus, 0, 0);
        bus.writeWord(cpu.ar[5], cpu.r[4]);
        bus.writeWord(cpu.ar[5], cpu.r[5]);
        bus.writeWord(cpu.ar[5], cpu.r[6]);
        bus.writeWord(cpu.ar[5], cpu.r[7]);
        bus.writeWord(cpu.ar[5], bus.readWord(cpu.ar[0]++));
        fire(cpu, bus);
        cpu.ar[4] -= 1;
    } while (si(cpu.ar[4]) >= 0);
    cpu.r[0] = bus.readWord(0xC8C5);
    if (cpu.r[0] & 0x80u) subs.call(0x83D1, cpu, bus);
}

/// Rolagem horizontal pelo yaw [E3F8] (08231..08245 / 083D1..083DC) → R6.
void skyScroll(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[2] = tmsFix(rf(bus, 0xE3F8) * rf(bus, 0x8224));
    if (si(cpu.r[2]) >= si(bus.readWord(0x8228))) cpu.r[2] -= bus.readWord(0x8228);
    if (!(si(cpu.r[2]) > si(bus.readWord(0x8227)))) cpu.r[2] += bus.readWord(0x8228);
    cpu.r[6] = doubleToTms(static_cast<double>(si(cpu.r[2])));
}

/// 083D1  segunda camada (silhueta do horizonte): 12 quads da tabela [8430] até y = 0x190.
void drawHorizon(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    skyScroll(cpu, bus);
    cpu.ar[2] = bus.readWord(0x5A);
    cpu.r[2] = doubleToTms(-rf(bus, 0xE3F8));
    const uint32_t ar2 = cpu.ar[2];
    subs.call(0x938A, cpu, bus);
    cpu.ar[2] = ar2;
    cpu.r[3] = cpu.ar[2];
    cpu.r[2] = bus.readWord(0x47);
    subs.call(0x9494, cpu, bus);
    // 083E6  R7 (sobrescrito pela 0x839B; mantido como no original). R0 entra com [C8C5].
    cpu.r[7] = doubleToTms(rf(bus, 0x80983C) * 64.0 * 8.0);
    cpu.r[2] = bus.readWord(0x809810);
    cpu.r[1] = bus.readWord(0x80980D);
    cpu.r[0] = doubleToTms(tmsToDouble(cpu.r[0]) * rf(bus, 0x822A));
    subs.call(0xA509, cpu, bus);
    cpu.r[0] = doubleToTms(tmsToDouble(cpu.r[0]) * rf(bus, 0x8229));
    cpu.r[7] = doubleToTms(-tmsToDouble(cpu.r[0]));
    cpu.ar[5] = bus.readWord(0x8430);
    cpu.ar[6] = bus.readWord(kProjected);
    cpu.ar[4] = 0x1A;
    subs.call(0x839B, cpu, bus);
    // 083F9
    cpu.ar[2] = 0x31;
    subs.call(0x9ECD, cpu, bus);
    cpu.ar[2] = cpu.r[0];
    cpu.ar[5] = kDmaFifo;
    cpu.r[4] = static_cast<uint32_t>(-0x5300);
    cpu.r[5] = 0x100;
    cpu.r[6] = 0x1FF;
    cpu.r[7] = static_cast<uint32_t>(-0x5201);
    cpu.ar[4] = 0xB;
    cpu.ar[6] = bus.readWord(kProjected);
    cpu.ar[1] = 0;
    do {
        waitDma(cpu, bus);
        const uint32_t p = cpu.ar[6];
        for (uint32_t w : {0x100u, cpu.ar[2], fixAt(bus, p), 0x190u, fixAt(bus, p + 3), fixAt(bus, p + 4),
                           fixAt(bus, p + 9), fixAt(bus, p + 0xA), fixAt(bus, p + 6), 0x190u, cpu.r[4], cpu.r[5],
                           cpu.r[6], cpu.r[7], 0x25F9u})
            bus.writeWord(cpu.ar[5], w);
        cpu.ar[6] += 6;
        fire(cpu, bus);
        cpu.ar[4] -= 1;
    } while (si(cpu.ar[4]) >= 0);
}

} // namespace

void registerSkyRenderer(SubroutineDispatcher& d) {
    d.add(0x81D9, horizonPoint);
    d.add(0x822B, drawSky);
    d.add(0x839B, projectPoints);
    d.add(0x83D1, drawHorizon);
}

} // namespace rea::core::video
