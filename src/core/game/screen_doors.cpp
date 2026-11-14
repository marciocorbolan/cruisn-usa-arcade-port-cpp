/**
 * @file screen_doors.cpp
 * @brief Tradução de `0x5DE0`–`0x5EBF`, `0x5EC0`–`0x5FA0` e `0x5FA1`–`0x6094`.
 */
#include "core/game/screen_doors.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kPalA = 0x59EA;  ///< `[59EA]`: paleta de 2 dos painéis.
constexpr uint32_t kPalB = 0x59EB;  ///< `[59EB]`: paleta dos outros 2 painéis.

/// Lê a variável float @p n do processo (AR7 + @p n).
double V(ProcCtx& c, uint32_t n) { return c.rf(c.cpu.ar[7] + n); }
/// Grava a variável float @p n do processo (AR7 + @p n).
void setV(ProcCtx& c, uint32_t n, double v) { c.wf(c.cpu.ar[7] + n, v); }

/// `LDI [ptr],AR2 / CALL $9EDB` → R0 (slot da paleta).
uint32_t slotOf(ProcCtx& c, uint32_t ptr) {
    c.cpu.ar[2] = c.rd(ptr);
    c.call(0x9EDB);
    return c.cpu.r[0];
}

/// 05FAC  copia a paleta 0x22 para `[59EA]` e `[59EB]` e envia as duas.
void copyDoorPalette(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[0] = c.rd(kPalA);
    cpu.ar[1] = c.rd(kPalB);
    cpu.ar[3] = c.rd(c.rd(0x9E7B) + 0x22);
    cpu.r[0] = c.rd(cpu.ar[3]++);
    c.wr(cpu.ar[0]++, cpu.r[0]);
    c.wr(cpu.ar[1]++, cpu.r[0]);
    cpu.ar[2] = ((cpu.r[0] & 0xFFFu) >> 1) - 1;
    do {
        cpu.r[0] = c.rd(cpu.ar[3]++);
        c.wr(cpu.ar[0]++, cpu.r[0]);
        c.wr(cpu.ar[1]++, cpu.r[0]);
    } while (s(--cpu.ar[2]) >= 0);
    cpu.ar[2] = c.rd(kPalA);
    c.call(0x9F2F);
    cpu.ar[2] = c.rd(kPalB);
    c.call(0x9F2F);
}

/// Um canal de 5 bits escalado pela luz.
uint32_t shade(uint32_t ch, double light) {
    const uint32_t v = tmsFix(static_cast<double>(ch << 3) * light);
    return (v >> 3) & 0x1Fu;
}
/// Aplica a luz @p light aos 3 canais de 5 bits da cor @p c15.
uint32_t shade15(uint32_t c15, double light) {
    return shade(c15 & 0x1F, light) | (shade((c15 >> 5) & 0x1F, light) << 5) |
           (shade((c15 >> 10) & 0x1F, light) << 10);
}

/// Escala a paleta 0x22 pela luz @p lightAddr em `[dstPtr]` e reenvia se tiver slot.
void shadeInto(ProcCtx& c, uint32_t lightAddr, uint32_t dstPtr) {
    Tms32031State& cpu = c.cpu;
    const double light = c.rf(lightAddr);
    cpu.ar[0] = c.rd(dstPtr);
    cpu.ar[3] = c.rd(c.rd(0x9E7B) + 0x22);
    cpu.r[0] = c.rd(cpu.ar[3]++);
    c.wr(cpu.ar[0]++, cpu.r[0]);
    cpu.ar[5] = (cpu.r[0] << 1) >> 2;
    do {
        cpu.r[4] = c.rd(cpu.ar[3]++);
        cpu.r[0] = shade15(cpu.r[4], light) | (shade15(cpu.r[4] >> 16, light) << 16);
        c.wr(cpu.ar[0]++, cpu.r[0]);
    } while (s(--cpu.ar[5]) >= 0);
    cpu.ar[2] = c.rd(dstPtr);
    c.call(0x9EDB);
    if (cpu.r[0] != 0xFFFFFFFFu) {
        cpu.r[3] = c.rd(cpu.ar[2]++);
        cpu.r[2] = cpu.r[0];
        c.call(0x9F5C);
    }
}

/// 05FD2  luz das duas paletas das portas. Preserva R0–R6 e AR0–AR5.
void shadeDoors(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    uint32_t r[7], ar[6];
    for (int i = 0; i < 7; ++i) r[i] = cpu.r[i];
    for (int i = 0; i < 6; ++i) ar[i] = cpu.ar[i];
    shadeInto(c, 0xE85D, kPalB);
    shadeInto(c, 0xE85E, kPalA);
    for (int i = 0; i < 7; ++i) cpu.r[i] = r[i];
    for (int i = 0; i < 6; ++i) cpu.ar[i] = ar[i];
}

/// 05FC2  luz pelos ângulos dos painéis D/E: (1 − (θ + π/2)·k)·0,7 + 0,3.
void lightFromAngles(ProcCtx& c) {
    auto f = [&](uint32_t n) {
        return (1.0 - (V(c, n) + c.k(0x5FC3)) * c.rf(0x5FC1)) * c.k(0x5FC6) + c.k(0x5FC7);
    };
    c.wf(0xE85D, f(0x13));
    c.wf(0xE85E, f(0x14));
    c.call(0x5FD2);
}

/// 05FA1  libera os slots das paletas das portas.
void freeDoorPalettes(ProcCtx& c) {
    for (uint32_t p : {kPalB, kPalA}) {
        const uint32_t slot = slotOf(c, p);
        if (!c.cpu.carry()) continue;  // BHS: sem slot
        c.cpu.ar[2] = slot;
        c.call(0x9EEC);
    }
}

/// Cria os 4 painéis dos modelos `[models..models+3]` em `*+AR7(C..F)`.
void makePanels(ProcCtx& c, uint32_t models) {
    Tms32031State& cpu = c.cpu;
    copyDoorPalette(c);
    const uint32_t flags = (0x800u << 16) | 0x21u;
    const uint32_t palB = slotOf(c, kPalB);
    const uint32_t palA = slotOf(c, kPalA);
    for (uint32_t i = 0; i < 4; ++i) {
        cpu.ar[2] = c.rd(models + i);
        c.call(0x700A);
        c.wr(cpu.ar[7] + 0xC + i, cpu.ar[0]);
        c.wr(cpu.ar[0] + 0x10, (i & 1) ? palA : palB);
        c.wr(cpu.ar[0] + 0xE, c.rd(cpu.ar[0] + 0xE) | flags | 0x400u);
        cpu.ar[2] = cpu.ar[0];
        c.call(0x7070);
    }
}

/// Inclina os painéis: matriz de rotação X de `*+AR7(17)` em `*+AR7(18)` (`0x9377`).
void tilt(ProcCtx& c) {
    c.cpu.r[2] = c.rd(c.cpu.ar[7] + 0x17);
    c.cpu.ar[2] = c.cpu.ar[7] + 0x18;
    c.call(0x9377);
}

/// rotY(@p angle) em @p dst e `dst = M(AR7+18) × dst` (`0x9494`).
void spinInto(ProcCtx& c, double angle, uint32_t dst) {
    c.cpu.r[2] = doubleToTms(angle);
    c.cpu.ar[2] = dst;
    c.call(0x938A);
    c.cpu.r[2] = c.cpu.ar[7] + 0x18;
    c.cpu.r[3] = c.cpu.ar[2];
    c.call(0x9494);
}

/// Põe o objeto @p o no pivô (`*+AR7(10..12)`).
void placeAtPivot(ProcCtx& c, uint32_t o) {
    for (uint32_t i = 0; i < 3; ++i) c.wr(o + 1 + i, c.rd(c.cpu.ar[7] + 0x10 + i));
}

/// Painel C/F a @p arm no eixo `[0x5A]`, mais o pivô.
void armPanel(ProcCtx& c, uint32_t o, double arm) {
    Tms32031State& cpu = c.cpu;
    c.call(0x9414);
    c.wf(cpu.ar[2], arm);
    cpu.r[2] = c.rd(0x5A);
    cpu.r[3] = o + 1;
    c.call(0x941E);
    for (uint32_t i = 0; i < 3; ++i) c.wf(o + 1 + i, c.rf(o + 1 + i) + V(c, 0x10 + i));
}

/// Posiciona os 4 painéis; @p sign = +1 fechando, −1 abrindo (passo dos ângulos).
void panels(ProcCtx& c, double sign) {
    Tms32031State& cpu = c.cpu;
    const double stepB = c.rf(0x5DDF), stepA = c.rf(0x5DDE);
    // D
    cpu.ar[4] = c.rd(cpu.ar[7] + 0xD);
    placeAtPivot(c, cpu.ar[4]);
    setV(c, 0x14, V(c, 0x14) + sign * stepB);
    spinInto(c, V(c, 0x14), cpu.ar[4] + 4);
    // E
    cpu.ar[4] = c.rd(cpu.ar[7] + 0xE);
    placeAtPivot(c, cpu.ar[4]);
    setV(c, 0x15, V(c, 0x15) + sign * stepA);
    spinInto(c, V(c, 0x15), cpu.ar[4] + 4);
    // C, no eixo de D
    spinInto(c, V(c, 0x14), c.rd(0x5A));
    cpu.ar[4] = c.rd(cpu.ar[7] + 0xC);
    armPanel(c, cpu.ar[4], -127.0);
    setV(c, 0x13, V(c, 0x13) + sign * stepA);
    spinInto(c, V(c, 0x13), cpu.ar[4] + 4);
    // F, no eixo de E
    spinInto(c, V(c, 0x15), c.rd(0x5A));
    cpu.ar[4] = c.rd(cpu.ar[7] + 0xF);
    armPanel(c, cpu.ar[4], 128.0);
    setV(c, 0x16, V(c, 0x16) + sign * stepB);
    spinInto(c, V(c, 0x16), cpu.ar[4] + 4);
}

// --- 0x5DE0: fechar ------------------------------------------------------------------------------

/// Um quadro do fechamento: painéis e luz das paletas (`0x5FD2`).
void closeStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wf(0xE85E, c.rf(0xE85E) + c.k(0x5E2B));
    c.wf(0xE85D, c.rf(0xE85D) + c.k(0x5E2E));
    c.call(0x5FD2);
    const bool last = cpu.ar[5] == 0;
    const double z = V(c, 0x12);
    setV(c, 0x12, last ? 368.0 : (368.0 - z) * c.k(0x5E34) + z);
    setV(c, 0x17, last ? 0.0 : V(c, 0x17) * c.k(0x5E3B));
    tilt(c);
    panels(c, 1.0);
    c.sleep(1, 0x5EAB);
}

/// `0x5DE0`: começa o fechamento (painéis abertos, longe).
void closeStart(ProcCtx& c) {
    makePanels(c, 0xC60C);
    setV(c, 0x13, c.k(0x5E13));
    setV(c, 0x14, c.k(0x5E15));
    setV(c, 0x15, c.k(0x5E17));
    setV(c, 0x16, c.k(0x5E19));
    setV(c, 0x10, 0.0);
    setV(c, 0x11, 0.0);
    setV(c, 0x12, 3368.0);
    setV(c, 0x17, c.k(0x5E20));
    tilt(c);
    c.wf(0xE85E, c.k(0x5E25));
    c.wf(0xE85D, c.k(0x5E27));
    c.cpu.ar[5] = 0x1D;
    closeStep(c);
}

/// Fim do fechamento: paleta 0x22 (`0x9EC2`) e limpa o bit 0x400 dos painéis.
void closeEnd(ProcCtx& c) {
    c.cpu.r[0] = 0x22;
    c.cpu.r[1] = 0x22;
    c.call(0x9EC2);
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t o = c.rd(c.cpu.ar[7] + 0xC + i);
        c.cpu.r[0] = c.rd(o + 0xE) & ~0x400u;
        c.wr(o + 0xE, c.cpu.r[0]);
    }
    c.die();
}

// --- 0x5EC0: abrir -------------------------------------------------------------------------------

/// Um quadro da abertura.
void openStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x5FC2);
    (void)slotOf(c, kPalB);
    (void)slotOf(c, kPalA);
    const double ax = V(c, 0x21) * c.k(0x5F0E);
    setV(c, 0x21, ax);
    setV(c, 0x10, V(c, 0x10) + ax);
    const double ay = V(c, 0x22) * c.k(0x5F14);
    setV(c, 0x22, ay);
    setV(c, 0x11, V(c, 0x11) - ay);
    const bool last = cpu.ar[5] == 0;
    const double z = V(c, 0x12);
    setV(c, 0x12, last ? 3368.0 : (3368.0 - z) * c.k(0x5F1C) + z);
    const double t = V(c, 0x17);
    setV(c, 0x17, last ? c.k(0x5F27) : (c.k(0x5F23) - t) * c.k(0x5F25) + t);
    tilt(c);
    panels(c, -1.0);
    c.sleep(1, 0x5F96);
}

/// `0x5EC0`: começa a abertura (som 0x84).
void openStart(ProcCtx& c) {
    c.sound(0x84);
    makePanels(c, 0xC610);
    for (uint32_t n = 0x13; n <= 0x16; ++n) setV(c, n, 0.0);
    setV(c, 0x10, 0.0);
    setV(c, 0x11, 0.0);
    setV(c, 0x12, 368.0);
    setV(c, 0x17, 0.0);
    tilt(c);
    setV(c, 0x21, 1.0);
    setV(c, 0x22, 1.0);
    c.cpu.ar[5] = 0x1D;
    openStep(c);
}

/// Fim da abertura: solta os 4 painéis (`0x7100`).
void openEnd(ProcCtx& c) {
    for (uint32_t i = 0; i < 4; ++i) {
        c.cpu.ar[2] = c.rd(c.cpu.ar[7] + 0xC + i);
        c.call(0x7100);
    }
    c.call(0x5FA1);
    c.die();
}

} // namespace

void registerScreenDoors(SubroutineDispatcher& d) {
    addProc(d, 0x5FAC, copyDoorPalette);
    addProc(d, 0x5FD2, shadeDoors);
    addProc(d, 0x5FC2, lightFromAngles);
    addProc(d, 0x5FA1, freeDoorPalettes);
    addProc(d, 0x5DE0, closeStart);
    addProc(d, 0x5EAB, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return closeStep(c);  // DBU AR5
        closeEnd(c);
    });
    addProc(d, 0x5EC0, openStart);
    addProc(d, 0x5F96, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return openStep(c);
        openEnd(c);
    });
}

} // namespace rea::core::game
