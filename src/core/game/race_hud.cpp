/**
 * @file race_hud.cpp
 * @brief Tradução de `0x9CF5`–`0x9DF5`, `0x9E22`–`0x9E7A`, `0xAF48`–`0xAF53`, `0x6099`–`0x61C0`,
 *        `0x1F56`–`0x1F85` e `0x86C5`–`0x86FB`.
 */
#include "core/game/race_hud.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
/// Reinterpreta @p v como palavra sem sinal.
inline uint32_t u(int32_t v) { return static_cast<uint32_t>(v); }

constexpr uint32_t kHudShift = 0xE60F;  ///< Deslocamento da HUD (150 = fora da tela).

/// Campos do modelo de quad `[EA34..EA46]` (enviado pela `0xAED6`).
constexpr uint32_t kQMode = 0xEA34;  ///< Modo do quad.
constexpr uint32_t kQPal = 0xEA35;   ///< Base de paleta.
constexpr uint32_t kQX0 = 0xEA36;    ///< x do vértice 0.
constexpr uint32_t kQY0 = 0xEA37;    ///< y do vértice 0.
constexpr uint32_t kQX1 = 0xEA39;    ///< x do vértice 1.
constexpr uint32_t kQY0b = 0xEA3A;   ///< y do vértice 1.
constexpr uint32_t kQX1b = 0xEA3C;   ///< x do vértice 2.
constexpr uint32_t kQY1b = 0xEA3D;   ///< y do vértice 2.
constexpr uint32_t kQX0b = 0xEA3F;   ///< x do vértice 3.
constexpr uint32_t kQY1 = 0xEA40;    ///< y do vértice 3.
constexpr uint32_t kQTex = 0xEA46;   ///< Página de textura.

/// Deslocamento atual da HUD (`[E60F]`).
int32_t shift(ProcCtx& c) { return s(c.rd(kHudShift)); }
/// Deslocamento da HUD como double.
double shiftF(ProcCtx& c) { return static_cast<double>(shift(c)); }

/// x dos vértices do quad: 0 e 3 = @p x0, 1 e 2 = @p x1.
void quadX(ProcCtx& c, uint32_t x0, uint32_t x1) {
    c.wr(kQX0, x0);
    c.wr(kQX0b, x0);
    c.wr(kQX1, x1);
    c.wr(kQX1b, x1);
}
/// y dos vértices do quad: 0 e 1 = @p y0, 2 e 3 = @p y1.
void quadY(ProcCtx& c, uint32_t y0, uint32_t y1) {
    c.wr(kQY0, y0);
    c.wr(kQY0b, y0);
    c.wr(kQY1, y1);
    c.wr(kQY1b, y1);
}

/// `LDI img,AR2 / LDI x,R2 / LDI y,R3 / LDI pal,R4 / CALL fn` (`0xAF13` ou `0xAF48`).
void sprite(ProcCtx& c, uint32_t fn, uint32_t img, int32_t x, int32_t y, uint32_t pal = 0x900) {
    c.cpu.ar[2] = img;
    c.cpu.r[2] = u(x);
    c.cpu.r[3] = u(y);
    c.cpu.r[4] = pal;
    c.call(fn);
}

/// `LDI txt,AR2 / FLOAT x,R2 / FLOAT y,R3 / LDI 1,RC / CALL fn` (+ estilo).
void text(ProcCtx& c, uint32_t fn, uint32_t txt, double x, double y, uint32_t style, uint32_t rc = 1) {
    c.cpu.ar[2] = txt;
    c.cpu.r[2] = doubleToTms(x);
    c.cpu.r[3] = doubleToTms(y);
    c.cpu.rc = rc;
    c.call(fn);
    if (style != 0) c.call(style);
}

/// `0x60BD`: R0 quadros → R2 minutos (÷3300), R1 segundos (÷55) e R0 centésimos, limitados a
/// 99/59/99 (escalas `[60BA..60BC]`). Preserva R3.
void splitTime(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r3 = cpu.r[3];
    auto part = [&](uint32_t scaleAddr, int32_t max) {
        int32_t v = s(tmsFix(static_cast<double>(s(cpu.r[0])) * c.rf(scaleAddr)));
        if (v > max) v = max;
        if (v < 0) v = 0;
        return v;
    };
    int32_t m = part(0x60BA, 0x63);
    cpu.r[2] = u(m);
    int32_t r0 = s(cpu.r[0]) - m * 0xCE4;
    cpu.r[0] = u(r0 < 0 ? 0 : r0);
    int32_t sec = part(0x60BB, 0x3B);
    cpu.r[1] = u(sec);
    r0 = s(cpu.r[0]) - sec * 0x37;
    cpu.r[0] = u(r0 < 0 ? 0 : r0);
    cpu.r[0] = u(part(0x60BC, 0x63));
    cpu.r[3] = r3;
}

/// `0x6099`: texto AR2 = "M" + `[6095]` + "SS" + `[6095]` + "CC" do tempo R0 (quadros).
/// Preserva R0–R2 e AR0–AR2.
void formatTime(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], ar0 = cpu.ar[0], ar1 = cpu.ar[1], ar2 = cpu.ar[2];
    splitTime(c);
    const uint32_t hund = cpu.r[0], sec = cpu.r[1];
    c.call(0xA762);
    cpu.ar[0] = cpu.ar[2];
    cpu.ar[1] = c.rd(0x6095);
    c.call(0x77E7);
    for (uint32_t v : {sec, hund}) {
        cpu.r[2] = v;
        cpu.ar[2] = c.rd(0x6098);
        c.call(0xA74E);
        cpu.ar[1] = cpu.ar[2];
        c.call(0x77E7);
        if (v == sec) {
            cpu.ar[1] = c.rd(0x6095);
            c.call(0x77E7);
        }
    }
    cpu.ar[2] = ar2;
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
    cpu.r[2] = r2;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}

/// `0x9E59` (modo 0x2008, sem paleta) / `0x9E63` (modo do chamador): retângulo (R0, R1) de
/// tamanho R2 × R3 pela `0xAED6`. Preserva R0–R4.
void hudRect(ProcCtx& c, bool setMode) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3], r4 = cpu.r[4];
    if (setMode) {
        c.wr(kQMode, 0x2008);
        c.wr(kQPal, 0);
    }
    quadX(c, r0, r0 + r2);
    quadY(c, r1, r1 + r3);
    c.wr(kQTex, 0);
    c.call(0xAED6);
    cpu.r[0] = r0;
    cpu.r[1] = r1;
    cpu.r[2] = r2;
    cpu.r[3] = r3;
    cpu.r[4] = r4;
}

/// `0x9E22`: conta-giros (22 barras de `[B1C4]`, apagadas acima de rotação·0,458) e luz da marcha.
void tachometer(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[0] = c.rd(0xB1C4);
    cpu.ar[1] = c.rd(0xB1C5);
    for (uint32_t i = 0; i < 0x16; ++i) c.wr(cpu.ar[1] + i, c.rd(cpu.ar[0] + i));
    cpu.ar[0] = c.rd(0xE878);
    int32_t lit = s(tmsFix(c.rf(cpu.ar[0] + 0x39) * c.k(0x9E2A)));
    if (lit > 0x16) lit = 0x16;
    if (lit < 0) lit = 0;
    cpu.r[0] = u(lit);
    cpu.ar[1] = c.rd(0xB1C6) + cpu.r[0];
    cpu.r[1] = 0;
    for (int32_t i = 0; i <= 0x16 - lit; ++i) c.wr(cpu.ar[1] + u(i), 0);  // RPTS RC: RC + 1 escritas
    cpu.ar[2] = 0x40;
    c.call(0x9ECD);
    const uint32_t slot = cpu.r[0];
    cpu.r[2] = slot + 0xEA;
    cpu.ar[2] = c.rd(0xB1C7);
    cpu.r[3] = 0x16;
    c.call(0x9F5C);
    cpu.ar[0] = c.rd(0xE878);
    const uint32_t gear = c.rd(cpu.ar[0] + 0x38);
    cpu.r[1] = 0;
    if (gear == 1) cpu.r[1] = 4;
    if (gear == 2) cpu.r[1] = 3;
    if (gear == 3) cpu.r[1] = 2;
    if (gear == 4) cpu.r[1] = 1;
    cpu.ar[2] = c.rd(0xB1C8) + cpu.r[1];
    cpu.r[2] = slot + 1;
    cpu.r[3] = 4;
    c.call(0x9F5C);
}

/// `0x60DF`: radar.
void radar(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar4 = cpu.ar[4];
    cpu.ar[4] = c.rd(0xE876);
    const int32_t sh = shift(c);
    c.wr(kQMode, 0x2008);
    quadX(c, u(0x1CC + sh), u(0x1F4 + sh));
    quadY(c, 0x64, 0xC8);
    c.wr(kQTex, 0);
    c.wr(kQPal, 0);
    c.call(0xAED6);
    cpu.r[2] = c.rd(0x809807);
    cpu.dp = 0;
    cpu.ar[2] = c.rd(0x5A);
    c.call(0x938A);
    cpu.ar[0] = c.rd(0x57) - 0x20;
    for (;;) {
        cpu.r[0] = c.rd(cpu.ar[0] + 0x20);
        if (cpu.r[0] == 0) break;
        cpu.ar[0] = cpu.r[0];
        c.wr(kQMode, c.rd(cpu.ar[0] + 0xF) != 0x216 ? 0xCu : 3u);
        c.call(0x9414);
        c.wf(cpu.ar[2], (c.rf(cpu.ar[0] + 1) - c.rf(cpu.ar[4] + 1)) * c.rf(0xC614));
        c.wf(cpu.ar[2] + 2, (c.rf(cpu.ar[0] + 3) - c.rf(cpu.ar[4] + 3)) * c.rf(0xC615));
        cpu.r[2] = c.rd(0x5A);
        cpu.r[3] = cpu.ar[2];
        c.call(0x941E);
        int32_t x = s(tmsFix(c.rf(cpu.ar[2]))) + 0x1DE;
        const auto yOf = [&]() { return -s(tmsFix(c.rf(cpu.ar[2] + 2) * c.k(0x6128))) + 0x94; };
        if (c.rd(0xC8E3) != 0 && cpu.ar[0] == c.rd(0xE592)) {
            // 0611A  adversário do link: preso na borda, seta conforme a frente
            bool clamped = false;
            if (x < 0x1CC) { x = 0x1CC; clamped = true; }
            if (x > 0x1F4) { x = 0x1F4; clamped = true; }
            x += shift(c);
            quadX(c, u(x), u(x + 3));
            int32_t y = yOf();
            if (y < 0x64) { y = 0x64; clamped = true; }
            if (y > 0xC8) { y = 0xC8; clamped = true; }
            quadY(c, u(y), u(y + 3));
            cpu.ar[2] = c.rd(0xC616);
            c.call(0x9EDB);
            c.wr(kQPal, cpu.r[0]);
            c.wr(kQMode, 0x900);
            const bool left = (c.rd(0xC8CC) & 1u) != 0;
            c.wr(kQTex, c.rd(left ? 0xC617 : 0xC618));
            for (uint32_t a : {kQY0, kQY0b}) c.wr(a, c.rd(a) - 0x10);
            for (uint32_t a : {kQX0, kQX0b}) c.wr(a, c.rd(a) - 0xD);
            static constexpr uint32_t kUvL[] = {0x38, 0x41, 0xD41, 0xD38};
            static constexpr uint32_t kUvR[] = {0x41, 0x4B, 0xD4B, 0xD41};
            for (uint32_t i = 0; i < 4; ++i) c.wr(0xEA42 + i, left ? kUvL[i] : kUvR[i]);
            if (clamped) {
                cpu.ar[1] = c.rd(cpu.ar[0] + 0x1B);
                cpu.r[0] = c.rd(cpu.ar[1] + 0x57);
                cpu.ar[2] = c.rd(c.rd(0xE878) + 0x37);
                cpu.r[1] = c.rd(cpu.ar[2] + 0x1E);
                const uint32_t base = s(cpu.r[0]) < s(cpu.r[1]) ? 0xC8u : 0x64u;
                c.wr(kQY1b, base + 0xA);
                c.wr(kQY1, base + 0xA);
                c.wr(kQY0, base + 0xA - 0x14);
                c.wr(kQY0b, base + 0xA - 0x14);
            }
            c.call(0xAED6);
            c.wr(kQPal, 0);
            continue;
        }
        // 06189
        if (x < 0x1CC || x > 0x1F4) continue;
        x += shift(c);
        quadX(c, u(x), u(x + 3));
        const int32_t y = yOf();
        if (y < 0x64 || y > 0xC8) continue;
        quadY(c, u(y), u(y + 3));
        c.call(0xAED6);
    }
    // 061A3  o jogador no centro
    c.wr(kQMode, 1);
    quadX(c, u(0x1DE + shift(c)), u(0x1E1 + shift(c)));
    quadY(c, 0x94, 0x97);
    c.call(0xAED6);
    cpu.ar[4] = ar4;
    if (c.rd(0xC8E3) != 0) {
        cpu.ar[0] = c.rd(c.rd(0xE592) + 0x1B);
        cpu.ar[1] = c.rd(0xE878);
        cpu.r[0] = c.rd(cpu.ar[0] + 0x57);
        cpu.r[1] = c.rd(cpu.ar[1] + 0x57);
        cpu.r[0] = s(cpu.r[1]) > s(cpu.r[0]) ? 1u : 0u;
        c.wr(0xE867, cpu.r[0]);
    }
}

/// `0x9CFF`: HUD da corrida (um quadro).
void raceHud(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x9E22);
    c.call(0x60DF);
    if (c.item(0x34) != 0 && c.rd(0xEB4F) == 0 && c.rd(0xEB53) != 0) {
        text(c, 0x7651, c.rd(c.rd(0xB18C) + c.rd(0xEB54)), 256.0, 310.0, 0);
        c.orField(cpu.ar[0], 8, 0x100);
        c.orField(cpu.ar[1], 8, 0x100);
    }
    const uint32_t modeHigh = c.rd(0xC8C5) & ~0xFu;  // quirk: ANDN — nunca vale 4
    if (modeHigh == 4) {
        const int32_t tol = s(c.rd(0xE86A));
        if (tol < 0xA) {
            text(c, 0x7651, c.rd(tol <= 3 ? 0xB18D : 0xB18E), 256.0, 5.0, 0x76FA);
            c.orField(cpu.ar[0], 8, 0x100);
            c.orField(cpu.ar[1], 8, 0x100);
            cpu.r[2] = c.rd(0xE86A);
            cpu.ar[2] = c.rd(0xB18F);
            c.call(0xA762);
            text(c, 0x7651, cpu.ar[2], 256.0, 30.0, 0);
            c.orField(cpu.ar[0], 8, 0x100);
            c.orField(cpu.ar[1], 8, 0x100);
        }
    }
    // 09D45  molduras, marcha e logotipos
    sprite(c, 0xAF13, c.rd(0xB190), 0x1A4 + shift(c), 0x122);
    if (c.rd(0xE633) != 0) sprite(c, 0xAF13, c.rd(0xB191), 0x1CF + shift(c), 0x17C);
    else sprite(c, 0xAF13, c.rd(0xB192), 0x1D6 + shift(c), 0x17C);
    cpu.ar[0] = c.rd(0xE878);
    text(c, 0x7660, c.rd(c.rd(cpu.ar[0] + 0x38) + c.rd(0xB193)), 458.0 + shiftF(c), 360.0, 0x7676);
    sprite(c, 0xAF13, c.rd(0xB19E), 0 - shift(c), 0x14F);
    sprite(c, 0xAF13, c.rd(c.item(0x22) != 0 ? 0xB19F : 0xB1A0), 0x32 - shift(c), 0x181);
    sprite(c, 0xAF48, c.rd(0xB1A1), 0xF2, 9 - shift(c));
    // tempo restante
    cpu.r[2] = c.rd(0xE604);
    cpu.ar[2] = c.rd(0x9CE0);
    c.call(0xA762);
    text(c, 0x7660, cpu.ar[2], 256.0, 24.0 - shiftF(c), 0x76A7);
    c.orField(cpu.ar[0], 8, 0x100);
    const uint32_t timeText = cpu.ar[0];
    cpu.ar[2] = c.rd(0xB1A2);
    c.call(0x9EDB);
    c.wr(timeText + 0xC, cpu.r[0]);
    if ((c.rd(0xC8C5) & 0xFu) == 4) {
        const int32_t t = s(c.rd(0xE604));
        if (t <= 0xA && s(c.rd(0xE610)) != t) {
            c.wr(0xE610, u(t));
            if (t == 0xA || t <= 3) {
                cpu.ar[2] = t == 0xA ? 0x13Eu : 0x240u;
                c.call(0x91E0);
            }
        }
    }
    // 09DAA  velocímetro
    if (c.item(0x22) != 0) cpu.r[2] = tmsFix(static_cast<double>(s(c.rd(0xE605))) * c.k(0x9DAF));
    else cpu.r[2] = c.rd(0xE605);
    cpu.ar[2] = c.rd(0x9CE1);
    c.call(0xA762);
    text(c, 0x7651, cpu.ar[2], 66.0 - shiftF(c), 346.0, 0x7698);
    c.orField(cpu.ar[0], 8, 0x200);
    c.orField(cpu.ar[1], 8, 0x200);
    const uint32_t sp0 = cpu.ar[0], sp1 = cpu.ar[1];
    cpu.ar[2] = c.rd(0xB1A3);
    c.call(0x9EDB);
    c.wr(sp0 + 0xC, cpu.r[0]);
    c.wr(sp1 + 0xC, cpu.r[0]);
    cpu.r[0] = 8;
    cpu.r[1] = u(0x13 - shift(c));
    cpu.r[2] = 0x6A;
    cpu.r[3] = 0x1E;
    hudRect(c, true);
    sprite(c, 0xAF48, c.rd(0xB1A4), 0xA, 0x14 - shift(c));
    // tempo decorrido
    cpu.r[0] = c.rd(0xE85F);
    cpu.ar[2] = c.rd(0x9CE2);
    formatTime(c);
    text(c, 0x7651, cpu.ar[2], 20.0, 33.0 - shiftF(c), 0x7680);
    // posição
    cpu.ar[4] = (c.rd(0xE607) - 1) * 3 + c.rd(0xB1A5);
    const uint32_t img1 = c.rd(cpu.ar[4]++);
    sprite(c, 0xAF48, img1, 0x19F + s(c.rd(cpu.ar[4] + 1)), 0x14 - shift(c));
    const uint32_t img2 = c.rd(cpu.ar[4]++);
    sprite(c, 0xAF48, img2, 0x1C2, 0x14 - shift(c));
    // 09DED  odômetro e tempo de prova
    const double dt = static_cast<double>(s(c.rd(0xC92F)));
    c.wf(0xE47E, static_cast<double>(s(c.rd(0xE605))) * c.rf(0xE87E) * dt + c.rf(0xE47E));
    c.wf(0xE608, dt + c.rf(0xE608));
}

// --- Processos ---------------------------------------------------------------------------------

/// `0x9CF5`/`0x9CF6`: leva a HUD para fora (+6 por quadro, 25 quadros) e fixa 150.
void hudOut(ProcCtx& c, bool start) {
    Tms32031State& cpu = c.cpu;
    if (start) cpu.ar[5] = 0x18;
    else if (s(--cpu.ar[5]) < 0) {  // 09CFB  DBU AR5
        c.wr(kHudShift, 0x96);
        return c.die();
    }
    c.wr(kHudShift, c.rd(kHudShift) + 6);
    c.sleep(1, 0x9CFB);
}

/// Um quadro do letreiro: a posição se aproxima de @p target (20% por quadro) e retoma em @p resume.
void bannerStep(ProcCtx& c, double target, uint32_t resume) {
    Tms32031State& cpu = c.cpu;
    double r6 = tmsToDouble(cpu.r[6]);
    r6 += (target - r6) * c.k(0x1F69);
    cpu.r[6] = doubleToTms(r6);
    c.wf(cpu.ar[5] + 3, r6);
    c.wf(cpu.ar[4] + 3, r6 + 3.0);
    c.sleep(1, resume);
}

/// `0x1F56`: letreiro do nome da pista `[[3953] + [E481]]`: entra de −100 até 256 (20% por quadro,
/// 21 quadros), fica 50 quadros e sai até −100.
void bannerStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    text(c, 0x7651, c.rd(c.rd(0xE481) + c.rd(0x3953)), 256.0, 360.0, 0, 0x154);
    c.orField(cpu.ar[0], 8, 0x100);
    c.orField(cpu.ar[1], 8, 0x100);
    cpu.ar[4] = cpu.ar[0];
    cpu.ar[5] = cpu.ar[1];
    cpu.r[6] = doubleToTms(-100.0);
    cpu.ar[6] = 0x14;
    bannerStep(c, 256.0, 0x1F71);
}

/// `0x86C5`: logotipo `[B166]` (item 0x25): entra até x = −200, anima 18 quadros de `[B167]`
/// quando `[E84C]` ≠ 0 e sai 32·6 para a esquerda.
void logoFrame(ProcCtx& c);

/// `0x86E2`: movimento do logotipo.
void logoMove(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(0xE84C) != 0) {
        cpu.ar[5] = 0x11;
        cpu.ar[6] = c.rd(0xB167);
        return logoFrame(c);
    }
    const double x = c.rf(cpu.ar[4] + 1);
    if (x < -200.0) c.wf(cpu.ar[4] + 1, x + 5.0);
    c.sleep(1, 0x86E2);
}

void logoFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (;;) {
        cpu.r[0] = c.rd(cpu.ar[6]++);
        if (s(cpu.r[0]) >= 0) break;
        cpu.ar[6] = c.rd(0xB167);
    }
    c.wr(cpu.ar[4] + 0xD, cpu.r[0]);
    c.sleep(2, 0x86EA);
}

/// Move o logotipo 6 para a esquerda e retoma em @p resume.
void logoSlide(ProcCtx& c, uint32_t resume) {
    c.wf(c.cpu.ar[4] + 1, c.rf(c.cpu.ar[4] + 1) - 6.0);
    c.sleep(1, resume);
}

} // namespace

void registerRaceHud(SubroutineDispatcher& d) {
    addProc(d, 0x9CFF, raceHud);
    addProc(d, 0x9E22, tachometer);
    addProc(d, 0x9E59, [](ProcCtx& c) { hudRect(c, true); });
    addProc(d, 0x9E63, [](ProcCtx& c) { hudRect(c, false); });
    // 0AF48  imagem com sombra: AR2 em (R2+2, R3+2) com a paleta 0xD00, depois em (R2, R3) com R4
    addProc(d, 0xAF48, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        const uint32_t r2 = cpu.r[2], r3 = cpu.r[3], r4 = cpu.r[4];
        cpu.r[2] = r2 + 2;
        cpu.r[3] = r3 + 2;
        cpu.r[4] = 0xD00;
        c.call(0xAF13);
        cpu.r[2] = r2;
        cpu.r[3] = r3;
        cpu.r[4] = r4;
        c.call(0xAF13);
    });
    addProc(d, 0x60DF, radar);
    addProc(d, 0x6099, formatTime);
    addProc(d, 0x60BD, splitTime);
    addProc(d, 0x9CF5, [](ProcCtx& c) { hudOut(c, true); });
    addProc(d, 0x9CFB, [](ProcCtx& c) { hudOut(c, false); });

    addProc(d, 0x1F56, bannerStart);
    addProc(d, 0x1F71, [](ProcCtx& c) {
        if (s(--c.cpu.ar[6]) >= 0) return bannerStep(c, 256.0, 0x1F71);  // DBU AR6
        c.cpu.r[6] = doubleToTms(256.0);
        c.wf(c.cpu.ar[5] + 3, 256.0);
        c.wf(c.cpu.ar[4] + 3, 259.0);
        c.sleep(0x32, 0x1F79);
    });
    addProc(d, 0x1F79, [](ProcCtx& c) {
        c.cpu.ar[6] = 0x14;
        bannerStep(c, -100.0, 0x1F84);
    });
    addProc(d, 0x1F84, [](ProcCtx& c) {
        if (s(--c.cpu.ar[6]) >= 0) return bannerStep(c, -100.0, 0x1F84);
        c.die();
    });

    addProc(d, 0x86C5, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        if (c.item(0x25) == 0) return c.die();
        c.call(0x7025);
        cpu.ar[4] = cpu.ar[0];
        c.wf(cpu.ar[4] + 1, -350.0);
        c.wf(cpu.ar[4] + 2, 40.0);
        c.wf(cpu.ar[4] + 3, 368.0);
        c.orField(cpu.ar[4], 0xE, 0x29);
        c.wr(cpu.ar[4] + 0xD, c.rd(0xB166));
        cpu.ar[2] = cpu.ar[4];
        c.call(0x7070);
        logoMove(c);
    });
    addProc(d, 0x86E2, logoMove);
    addProc(d, 0x86EA, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return logoFrame(c);  // DBU AR5
        c.sleep(5, 0x86ED);
    });
    addProc(d, 0x86ED, [](ProcCtx& c) {
        c.cpu.ar[5] = 0x10;
        logoSlide(c, 0x86F3);
    });
    addProc(d, 0x86F3, [](ProcCtx& c) { logoSlide(c, 0x86F8); });
    addProc(d, 0x86F8, [](ProcCtx& c) {
        if (s(--c.cpu.ar[5]) >= 0) return logoSlide(c, 0x86F3);
        c.cpu.ar[2] = c.cpu.ar[4];
        c.call(0x7100);
        c.die();
    });
}

} // namespace rea::core::game
