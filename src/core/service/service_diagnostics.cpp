/**
 * @file service_diagnostics.cpp
 * @brief Telas de diagnóstico do menu de serviço: chaves (`0x0724`), DIPs (`0x0933`), monitor
 *        (`0x06B3`), placa de som (`0x0B02`), volante (`0x0B65`), burn-in (`0x0BA5`), volume
 *        (`0x0D1F`) e segurança do movimento (`0x13D5`).
 */
#include "core/non_local_jump.hpp"
#include "core/service/service_internal.hpp"

namespace rea::core::service {

namespace {

/// Parâmetros de `0xAC23` (linha), `0xA7E5` (retângulo cheio) ou `0xABFC` (moldura).
struct Shape {
    uint16_t ar2;  ///< AR2 (x inicial).
    uint16_t r2;   ///< R2 (y inicial).
    uint16_t r3;   ///< R3 (x final).
    uint16_t rc;   ///< RC (y final).
    uint16_t rs;   ///< RS (cor).
};

/// Desenha cada forma de @p list com a rotina @p routine.
template <std::size_t N>
void drawShapes(Ctx& c, uint32_t routine, const Shape (&list)[N]) {
    for (const Shape& s : list) c.shape(routine, s.ar2, s.r2, s.r3, s.rc, s.rs);
}

/// Texto: x, y, ponteiro do texto (endereço que guarda o ponteiro).
struct Label {
    uint16_t x;     ///< Coluna.
    uint16_t y;     ///< Linha.
    uint32_t ptr;  ///< Endereço que guarda o ponteiro do texto.
};

/// Escreve cada texto de @p list.
template <std::size_t N>
void drawLabels(Ctx& c, const Label (&list)[N]) {
    for (const Label& l : list) c.text(l.x, l.y, c.rd(l.ptr));
}

/// `RPTS R0 / NOP` com R0 = 0x7FFF · 10: 327 671 ciclos.
constexpr uint32_t kShortPause = 0x7FFFu * 10u + 1u;

// --- 0x0724: teste de chaves ----------------------------------------------------------------

/// Molduras da tela do teste de chaves (`0x0724`).
constexpr Shape kSwitchFrames[] = {
    {0x0A, 0x3C, 0x1D1, 0x3C, 0xB},   {0x0B, 0x3D, 0x1D0, 0x3D, 0xD},  {0x0A, 0x3C, 0x0A, 0x96, 0xB},
    {0x0B, 0x3D, 0x0B, 0x95, 0xD},    {0x1D1, 0x3C, 0x1D1, 0x96, 0xB}, {0x1D0, 0x3D, 0x1D0, 0x95, 0xD},
    {0x0B, 0x96, 0x1D0, 0x96, 0xD},   {0x0C, 0x95, 0x1CF, 0x95, 0xB},  {0x0A, 0xA0, 0xAA, 0xA0, 0xB},
    {0x0B, 0xA1, 0xA9, 0xA1, 0xD},    {0x0A, 0xA0, 0x0A, 0x168, 0xB},  {0x0B, 0xA1, 0x0B, 0x167, 0xD},
    {0xAA, 0xA0, 0xAA, 0x168, 0xB},   {0xA9, 0xA1, 0xA9, 0x167, 0xD},  {0x0B, 0x168, 0xA9, 0x168, 0xD},
    {0x0C, 0x167, 0xA8, 0x167, 0xB},  {0xBE, 0xA0, 0x168, 0xA0, 0xB},  {0xBF, 0xA1, 0x167, 0xA1, 0xD},
    {0xBE, 0xA0, 0xBE, 0xFA, 0xB},    {0xBF, 0xA1, 0xBF, 0xF9, 0xD},   {0x168, 0xA0, 0x168, 0xFA, 0xB},
    {0x167, 0xA1, 0x167, 0xF9, 0xD},  {0xBF, 0xFA, 0x167, 0xFA, 0xD},  {0xC0, 0xF9, 0x166, 0xF9, 0xB},
};

/// Rótulos da tela do teste de chaves.
constexpr Label kSwitchLabels[] = {
    {0xCD, 0x1A, 0xC7E5},  {0x14, 0x64, 0xC7E6},  {0x14, 0x6E, 0xC7E7},  {0x14, 0x78, 0xC7E8},
    {0x14, 0xB4, 0xC7E9},  {0x14, 0xC8, 0xC7EA},  {0x14, 0xDC, 0xC7EB},  {0x14, 0xF0, 0xC7EC},
    {0x14, 0x104, 0xC7ED}, {0x14, 0x118, 0xC7EE}, {0x14, 0x12C, 0xC7EF}, {0x14, 0x140, 0xC7F0},
    {0x14, 0x154, 0xC7F1}, {0x28, 0x28, 0xC7F2},  {0xD7, 0x118, 0xC7F3}, {0xD7, 0x12C, 0xC7F4},
    {0xD7, 0x140, 0xC7F5}, {0xD7, 0x154, 0xC7F6}, {0xE1, 0x15E, 0xC7F7},
};

/// `0x08FF`: leituras do volante, do acelerador e do freio (redesenha só o que mudou).
void potReadings(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    struct Pot { uint32_t value, last; uint16_t y; };
    static constexpr Pot kPots[] = {{0xC8C8, 0xEBD0, 0x64}, {0xC93B, 0xEBD1, 0x6E}, {0xC93C, 0xEBD2, 0x78}};
    for (const Pot& p : kPots) {
        cpu.r[2] = c.rd(p.value);
        if (cpu.r[2] == c.rd(p.last)) continue;
        c.wr(p.last, cpu.r[2]);
        c.shape(0xA7E5, 0x96, p.y, 0xD7, p.y + 9u, 0);
        cpu.r[2] = c.rd(p.value);
        cpu.ar[2] = c.rd(vars::kNumberBuf);
        c.call(0xA762);
        cpu.r[2] = 0x96;
        cpu.r[3] = p.y;
        cpu.rc = 0xF;
        c.call(0xA7F4);
    }
}

/// `0x08BA`: câmbio (bits 0x3C00 de IN0, ativos em 0).
void shifterState(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = ~c.rd(vars::kKeys) & 0x3C00u;
    if (cpu.r[0] == c.rd(0xEBD4)) return;
    c.wr(0xEBD4, cpu.r[0]);
    cpu.rc = 0xF;
    static constexpr Label kShift[] = {
        {0xD7, 0xAA, 0xC7FB}, {0xD7, 0xF0, 0xC7FC}, {0x122, 0xAA, 0xC7FD}, {0x122, 0xF0, 0xC7FE}};
    drawLabels(c, kShift);
    const uint32_t r0 = cpu.r[0], ar2 = cpu.ar[2];
    c.shape(0xA7E5, 0xE6, 0xB4, 0x145, 0xE6, 0);
    cpu.ar[2] = ar2;
    cpu.r[0] = r0;
    struct Gear { uint32_t bit; Shape box; };
    static constexpr Gear kGears[] = {{0x2000, {0xE6, 0xB4, 0xFA, 0xC8, 3}},
                                      {0x1000, {0xE6, 0xD2, 0xFA, 0xE6, 3}},
                                      {0x0800, {0x131, 0xB4, 0x145, 0xC8, 3}},
                                      {0x0400, {0x131, 0xD2, 0x145, 0xE6, 3}}};
    for (const Gear& g : kGears) {
        cpu.r[0] = c.rd(vars::kKeys);
        if ((cpu.r[0] & g.bit) == 0) c.shape(0xA7E5, g.box.ar2, g.box.r2, g.box.r3, g.box.rc, g.box.rs);
    }
}

/// `0x0895`: câmbio e botões (tabela `[08B9]`: máscara, y, x por botão; −1 no fim).
void switchStates(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    shifterState(c);
    cpu.r[0] = c.rd(vars::kKeys);
    cpu.r[1] = c.rd(0xEBD3);
    if (cpu.r[0] == cpu.r[1]) return;
    c.wr(0xEBD3, cpu.r[0]);
    c.shape(0xA7E5, 0x82, 0xAF, 0x8C, 0x15E, 0);
    c.shape(0xA7E5, 0x147, 0x110, 0x161, 0x166, 0);
    cpu.ar[6] = c.rd(0x08B9);
    for (;;) {
        cpu.r[0] = c.rd(vars::kKeys);
        cpu.r[1] = c.rd(cpu.ar[6]);
        cpu.ar[6] += 3;
        if (cpu.r[1] == 0xFFFFFFFFu) return;
        cpu.r[0] &= cpu.r[1];
        if (cpu.r[0] != 0) continue;
        cpu.ar[2] = c.rd(cpu.ar[6] - 1);
        cpu.r[3] = cpu.ar[2] + 0xA;
        cpu.r[2] = c.rd(cpu.ar[6] - 2) - 2;
        cpu.rc = cpu.r[2] + 0xC;
        cpu.rs = 3;
        c.call(0xA7E5);
    }
}

/// `0x0724`: teste de chaves, potenciômetros e valores de calibração da CMOS.
void switchTest(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    clearScreen(c);
    cpu.r[0] = 0xFFFFFFFFu;
    for (uint32_t a : {0xEBD0u, 0xEBD1u, 0xEBD2u, 0xEBD3u, 0xEBDBu, 0xEBD4u}) c.wr(a, cpu.r[0]);
    bevelBox(c, 0x7D, 0xAA, 0x91, 0x163, false);
    bevelBox(c, 0x87, 0x5F, 0xDC, 0x87, false);
    bevelBox(c, 0xF5, 0x5F, 0x1A4, 0x87, false);
    drawShapes(c, 0xAC23, kSwitchFrames);
    cpu.rc = 0xF;
    drawLabels(c, kSwitchLabels);
    bevelBox(c, 0x145, 0x10E, 0x163, 0x168, false);
    for (;;) {
        // 0081D
        potReadings(c);
        switchStates(c);
        cpu.rc = 0xF;
        static constexpr Label kCmos[] = {{0xFA, 0x46, 0xC7F8}, {0xFA, 0x50, 0xC7F9}, {0x86, 0x50, 0xC7FA}};
        drawLabels(c, kCmos);
        // 0082C  itens 1–7 (calibração): mínimo, centro e máximo
        struct Cal { uint8_t item; uint16_t x, y; };
        static constexpr Cal kCal[] = {{1, 0xFA, 0x6E},  {2, 0x15E, 0x6E}, {3, 0xFA, 0x64}, {5, 0x12C, 0x64},
                                       {4, 0x15E, 0x64}, {6, 0xFA, 0x78},  {7, 0x15E, 0x78}};
        for (const Cal& k : kCal) {
            cpu.r[2] = c.item(k.item);
            cpu.ar[2] = c.rd(vars::kNumberBuf);
            c.call(0xA762);
            cpu.r[2] = k.x;
            cpu.r[3] = k.y;
            cpu.rc = 0xF;
            c.call(0xA7F4);
        }
        c.burn(kShortPause);
        cpu.r[0] = ~c.rd(vars::kKeys) & keys::kEnter;
        if (cpu.r[0] != 0) break;
        c.waitVblank();
    }
    // 00874  destaca ENTER e espera 11 quadros
    cpu.ar[2] = 0x14A;
    cpu.r[3] = cpu.ar[2] + 0xA;
    cpu.r[2] = 0x113 - 2;
    cpu.rc = cpu.r[2] + 0xC;
    cpu.rs = 3;
    c.call(0xA7E5);
    cpu.ar[5] = 0xA;
    do {
        c.waitVblank();
    } while (Ctx::s32(--cpu.ar[5]) >= 0);
}

// --- 0x0933: teste das DIPs ------------------------------------------------------------------

/// `0x0942`: 8 chaves de R7 (bit 7 primeiro): "ON"/"OFF" em x = R5 + 20 e o número em x = R5.
void dipColumn(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[5] = 7;
    cpu.r[4] = 1;
    do {
        cpu.ar[2] = (cpu.r[7] & 0x80u) == 0 ? c.rd(0xC800) : c.rd(0xC801);
        const uint32_t r7 = cpu.r[7];
        cpu.r[2] = cpu.r[5] + 0x14;
        cpu.r[3] = cpu.r[6];
        cpu.rc = 0xF;
        c.call(0xA7F4);
        cpu.r[2] = cpu.r[4];
        cpu.ar[2] = c.rd(0xC802);
        c.call(0xA762);
        cpu.r[2] = cpu.r[5];
        cpu.r[3] = cpu.r[6];
        cpu.rc = 0xF;
        c.call(0xA7F4);
        cpu.r[7] = r7 << 1;
        cpu.r[4] += 1;
        cpu.r[6] += 0xA;
    } while (Ctx::s32(--cpu.ar[5]) >= 0);
}

/// Lê a porta das DIPs (0x992000) como o original: `LDI @($2000),R7` com DP = 0x99 e a leitura
/// fictícia de `[0]` em AR2.
uint32_t readDipPort(Ctx& c) {
    c.cpu.ar[2] = 0;
    const uint32_t v = c.rd(0x992000);
    c.cpu.ar[2] = c.rd(0);
    c.cpu.dp = 0;
    return v;
}

/// `0x095A`: desenha as DIPs quando `[C8CC]` muda.
void dipScreen(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(vars::kDips);
    if (cpu.r[0] == c.rd(0xEBDB)) return;
    c.wr(0xEBDB, cpu.r[0]);
    c.textAt(0x1E, 0x28, 0xC803, 0xF);
    static constexpr Shape kBank2[] = {
        {0x14, 0x32, 0x190, 0x32, 0xB},  {0x15, 0x33, 0x18F, 0x33, 0xD}, {0x14, 0x32, 0x14, 0xAA, 0xB},
        {0x15, 0x33, 0x15, 0xA9, 0xD},   {0x190, 0x32, 0x190, 0xAA, 0xB}, {0x18F, 0x33, 0x18F, 0xA9, 0xD},
        {0x15, 0xAA, 0x18F, 0xAA, 0xD},  {0x16, 0xA9, 0x18E, 0xA9, 0xB}};
    drawShapes(c, 0xAC23, kBank2);
    c.shape(0xA7E5, 0x16, 0x34, 0x18E, 0xA8, 0);
    cpu.r[5] = 0x1E;
    cpu.r[6] = 0x3C;
    cpu.r[7] = readDipPort(c) >> 16;
    dipColumn(c);
    // 009A2  textos das chaves da DIP SW2
    struct Choice { uint16_t y; uint32_t bit, ifSet, ifClear; };
    static constexpr Choice kChoices[] = {{0x46, 0x40, 0x0A32, 0x0A31},
                                          {0x50, 0x20, 0xC804, 0xC805},
                                          {0x6E, 0x04, 0x0A34, 0x0A33},
                                          {0x82, 0x01, 0x0A30, 0x0A2F}};
    for (const Choice& ch : kChoices) {
        cpu.r[2] = 0x5A;
        cpu.r[3] = ch.y;
        cpu.r[0] = c.rd(vars::kDips);
        cpu.ar[2] = c.rd((cpu.r[0] & ch.bit) ? ch.ifSet : ch.ifClear);
        c.call(0xA7F4);
    }
    c.textAt(0x1E, 0xB4, 0xC80A, 0xF);
    static constexpr Shape kBank3[] = {
        {0x14, 0xBE, 0x190, 0xBE, 0xB},  {0x15, 0xBF, 0x18F, 0xBF, 0xD},  {0x14, 0xBE, 0x14, 0x136, 0xB},
        {0x15, 0xBF, 0x15, 0x135, 0xD},  {0x190, 0xBE, 0x190, 0x136, 0xB}, {0x18F, 0xBF, 0x18F, 0x135, 0xD},
        {0x15, 0x136, 0x18F, 0x136, 0xD}, {0x16, 0x135, 0x18E, 0x135, 0xB}};
    drawShapes(c, 0xAC23, kBank3);
    c.shape(0xA7E5, 0x16, 0xC0, 0x18E, 0x134, 0);
    cpu.r[5] = 0x1E;
    cpu.r[6] = 0xC8;
    cpu.r[7] = readDipPort(c) >> 24;
    dipColumn(c);
    c.shape(0xAC23, 0x55, 0xC8, 0x55, 0x10D, 0xF);
    // 00A08  preço escolhido pelas DIPs (0x73B7) e as linhas do registro (0x73B0)
    c.call(0x73B7);
    const uint32_t price = cpu.r[2];
    cpu.ar[0] = cpu.r[2];
    c.call(0x73B0);
    cpu.ar[4] = cpu.ar[0];
    cpu.ar[2] = c.rd(cpu.ar[4]++);
    cpu.r[2] = 0x5A;
    cpu.r[3] = 0xE6;
    cpu.rc = 0xF;
    c.call(0xA7F4);
    for (uint16_t y : {uint16_t{0xF0}, uint16_t{0xFA}}) {
        cpu.ar[2] = c.rd(cpu.ar[4]++);
        if (cpu.ar[2] == 0) break;
        cpu.r[2] = 0x5A;
        cpu.r[3] = y;
        cpu.rc = 0xF;
        c.call(0xA7F4);
    }
    cpu.ar[2] = c.rd(price + c.rd(0xB084));
    cpu.r[2] = 0x5A;
    cpu.r[3] = 0xDC;
    cpu.rc = 0xF;
    c.call(0xA7F4);
    cpu.r[2] = 0x5A;
    cpu.r[3] = 0x10E;
    cpu.r[0] = c.rd(vars::kDips);
    cpu.ar[2] = c.rd((cpu.r[0] & 0x100u) ? 0xC80B : 0xC80C);
    c.call(0xA7F4);
}

/// `0x0933`: teste das DIP switches.
void dipTest(Ctx& c) {
    clearScreen(c);
    c.textAt(0x14, 0x0F, 0xC7FF, 0xF);
    c.wr(0xEBDB, 0xFFFFFFFFu);
    c.textAt(0xB4, 0x173, 0xC7E3, 0xF);  // 0x06A8
    for (;;) {
        dipScreen(c);
        if (~c.rd(vars::kKeys) & keys::kEnter) return;
        c.waitVblank();
    }
}

// --- 0x06B3: teste do monitor ----------------------------------------------------------------

/// `0x06ED`: barras de cor (cinza, vermelho, verde e azul, 16 tons cada) na página 0.
void colorBars(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x8E7D);
    cpu.r[0] = 0;
    c.wr(vars::kBackColor, 0);
    cpu.ar[0] = 0x9E0000;
    for (int i = 0; i < 16; ++i, cpu.r[0] += 2) c.wr(cpu.ar[0]++, cpu.r[0] | (cpu.r[0] << 5) | (cpu.r[0] << 10));
    cpu.r[0] = 0;
    for (int i = 0; i < 16; ++i, cpu.r[0] += 2) c.wr(cpu.ar[0]++, cpu.r[0] << 10);
    cpu.r[0] = 0;
    for (int i = 0; i < 16; ++i, cpu.r[0] += 2) c.wr(cpu.ar[0]++, cpu.r[0] << 5);
    cpu.r[0] = 0;
    for (int i = 0; i < 16; ++i, cpu.r[0] += 2) c.wr(cpu.ar[0]++, cpu.r[0]);
    // 00710  4 faixas de 51 linhas; cada linha = 16 cores × 16 pixels a partir de x = 0x80
    cpu.r[3] = 0;
    cpu.r[2] = 0x32;
    cpu.ar[4] = 3;
    do {
        cpu.r[4] = cpu.r[3];
        cpu.ar[5] = 0x32;
        do {
            cpu.r[3] = cpu.r[4];
            cpu.ar[2] = 0x80;
            cpu.ar[7] = 0xF;
            do {
                cpu.ar[6] = 0xF;
                do {
                    c.call(0xA824);
                    cpu.ar[2] += 1;
                } while (Ctx::s32(--cpu.ar[6]) >= 0);
                cpu.r[3] += 1;
            } while (Ctx::s32(--cpu.ar[7]) >= 0);
            cpu.r[2] += 1;
        } while (Ctx::s32(--cpu.ar[5]) >= 0);
        cpu.r[2] += 0x14;
    } while (Ctx::s32(--cpu.ar[4]) >= 0);
}

/// Preenche a página 0 da VRAM com o padrão em @p patternAddr (`0x8E98`).
void fillPage(Ctx& c, uint32_t patternAddr) {
    c.cpu.ar[2] = 0x900000;
    c.cpu.r[3] = c.rd(0x06AE);
    c.cpu.r[2] = c.rd(patternAddr);
    c.call(0x8E98);
}

/// `0x06B3`: testes do monitor (barras de cor e padrões).
void monitorTest(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    colorBars(c);
    c.waitEnterDown();
    c.waitEnterUp();
    c.call(0x9E80);
    cpu.ar[2] = c.rd(0xC7E4);
    c.call(0x9F2F);
    // 006B8  telas cheias nas cores 3, 4, 9 e 8 (duas cores por palavra)
    fillPage(c, 0x06AF);
    for (uint32_t pattern : {0x06B0u, 0x06B1u, 0x06B2u}) {
        c.waitEnterDown();
        c.waitEnterUp();
        fillPage(c, pattern);
    }
    // 006D3  grade: 21 linhas horizontais (de 20 em 20; a 2ª em y = 19) e 33 verticais (de 16 em 16)
    cpu.ar[7] = 0x14;
    cpu.ar[6] = 0;
    do {
        if (cpu.ar[6] == 0x14) cpu.ar[6] = 0x13;
        cpu.ar[2] = 0;
        cpu.r[3] = 0x1FF;
        cpu.r[2] = cpu.ar[6];
        cpu.rc = cpu.ar[6];
        cpu.rs = 1;
        c.call(0xAC23);
        cpu.ar[6] += 0x14;
    } while (Ctx::s32(--cpu.ar[7]) >= 0);
    cpu.ar[7] = 0x20;
    cpu.ar[6] = 0;
    do {
        if (cpu.ar[6] == 0x10) cpu.ar[6] = 0xF;
        cpu.r[2] = 0;
        cpu.rc = 0x18F;
        cpu.ar[2] = cpu.ar[6];
        cpu.r[3] = cpu.ar[6];
        cpu.rs = 1;
        c.call(0xAC23);
        cpu.ar[6] += 0x10;
    } while (Ctx::s32(--cpu.ar[7]) >= 0);
    c.waitEnterDown();
    c.waitEnterUp();
}

// --- 0x0B02: placa de som --------------------------------------------------------------------

/// `0x0B02`: teste da placa de som.
void soundTest(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    clearScreen(c);
    cpu.r[5] = 0;
    cpu.r[6] = 0;
    cpu.rc = 0xF;
    static constexpr Label kLabels[] = {
        {0x32, 0x14, 0xC83A}, {0x14, 0x64, 0xC83B}, {0x14, 0x6E, 0xC83C}, {0x14, 0x78, 0xC83D}};
    drawLabels(c, kLabels);
    cpu.ar[5] = c.rd(0xC83E);  // músicas (texto, comando)
    cpu.ar[6] = c.rd(0xC83F);  // efeitos
    cpu.r[5] = 0xFFFFFFFFu;
    cpu.r[6] = 0xFFFFFFFFu;
    for (;;) {
        // 00B1A  troca de música / efeito: limpa, escreve o nome e toca (0x91DA)
        if (cpu.r[5] != cpu.ar[5]) {
            cpu.r[5] = cpu.ar[5];
            c.shape(0xA7E5, 0x14, 0xC8, 0xC8, 0xDC, 0);
            c.textAt(0x14, 0xC8, 0xC840, 0xF);
            c.text(0x1E, 0xD2, c.rd(cpu.ar[5]));
            cpu.ar[2] = c.rd(cpu.ar[5] + 1);
            c.call(0x91DA);
        }
        if (cpu.r[6] != cpu.ar[6]) {
            cpu.r[6] = cpu.ar[6];
            c.shape(0xA7E5, 0x14, 0xFA, 0xC8, 0x10E, 0);
            c.textAt(0x14, 0xFA, 0xC841, 0xF);
            c.text(0x1E, 0x104, c.rd(cpu.ar[6]));
            cpu.ar[2] = c.rd(cpu.ar[6] + 1);
            c.call(0x91DA);
        }
        for (;;) {
            // 00B42
            if (~c.rd(vars::kKeys) & keys::kEnter) {
                c.call(0x9166);
                return;
            }
            if (~c.rd(vars::kKeys) & keys::kUp) {
                do {
                    c.waitVblank();
                } while (~c.rd(vars::kKeys) & keys::kUp);
                cpu.ar[5] += 2;
                if (Ctx::s32(c.rd(cpu.ar[5])) < 0) cpu.ar[5] = c.rd(0xC842);
                break;
            }
            if (~c.rd(vars::kKeys) & keys::kDown) {
                do {
                    c.waitVblank();
                } while (~c.rd(vars::kKeys) & keys::kDown);
                cpu.ar[6] += 2;
                if (Ctx::s32(c.rd(cpu.ar[6])) < 0) cpu.ar[6] = c.rd(0xC843);
                break;
            }
            c.burn(kShortPause);
            c.waitVblank();
        }
    }
}

// --- 0x0B65: volante com motor ---------------------------------------------------------------

/// Rampa de 128 quadros da força do motor do volante (`0xB001`); @p left liga o bit 0x80 (sentido).
void wheelRamp(Ctx& c, bool left) {
    Tms32031State& cpu = c.cpu;
    cpu.r[6] = 0;
    cpu.ar[5] = 0x7F;
    do {
        cpu.r[6] += 1;
        if (Ctx::s32(cpu.r[6]) > 0x7F) cpu.r[6] = 0x7F;
        cpu.r[0] = left ? (cpu.r[6] | 0x80u) : cpu.r[6];
        c.call(0xB001);
        c.waitVblank();
    } while (Ctx::s32(--cpu.ar[5]) >= 0);
}

/// `0x0B65`: teste do motor do volante (rampas para a esquerda e a direita).
void wheelTest(Ctx& c) {
    clearScreen(c);
    c.textAt(0x14, 0x32, 0xC844, 0xF);
    c.textAt(0x14, 0x64, 0xC845, 0xF);
    wheelRamp(c, true);
    c.textAt(0x14, 0x78, 0xC846, 0xF);
    c.waitEnterUp();
    c.waitEnterDown();
    c.sound(0x03D4);
    c.textAt(0x14, 0x8C, 0xC847, 0xF);
    wheelRamp(c, false);
    c.textAt(0x14, 0xA0, 0xC848, 0xF);
    c.waitEnterUp();
    c.waitEnterDown();
    c.sound(0x03D4);
    c.cpu.r[0] = 0;
    c.call(0xB001);
}

// --- 0x0BA5: burn-in -------------------------------------------------------------------------

/// Duração estimada de uma passada do teste da placa (`0x6381`): as pausas e os testes de RAM do
/// original não são reproduzidos ciclo a ciclo. Aproximação: 1 s.
constexpr uint32_t kBurnInPass = 431600u * 58u;

/// `0x0BA5`: burn-in, repete o teste da placa (`0x6381`) até a chave de teste.
void burnIn(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    for (;;) {
        c.call(0x6381);
        // 00BA6  bus control 0x470; desliga o GIE; SP = [0x63]
        c.wr(0x808064, 0x470);
        cpu.dp = 0;
        cpu.st &= ~Tms32031State::kStGie;
        cpu.sp = c.rd(0x0063);
        c.feedWatchdog();  // o autoteste alimenta o watchdog de hardware (0x5029/0x503C)
        c.burn(kBurnInPass);
        c.feedWatchdog();
        // 00BB2  conta a passada (item 0x8E) e sai com a chave de teste (porta bruta 0x991060)
        cpu.ar[2] = 0x8E;
        c.call(0x99DA);
        cpu.r[0] = c.rd(0x991060);
        if ((cpu.r[0] & keys::kEnter) == 0) throw NonLocalJump{0x0699};
    }
}

// --- 0x0D1F: volume -------------------------------------------------------------------------

/// Uma passada do laço `0x0D86` sem mudança de volume (sorteio do som de amostra + teclas).
constexpr uint32_t kVolumeIdle = cycles::kPoll;
/// Uma passada com mudança: barra de 236 × 31 pixels (`0x0DBA`), comando de som e gravação na CMOS.
constexpr uint32_t kVolumeStep = 30000;

/// `0x0DBA`: barra de volume (R4 pixels na cor 4, o resto na 8).
void volumeBar(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = 0x78;
    cpu.r[3] = cpu.ar[2] + cpu.r[4];
    cpu.r[2] = 0x96;
    cpu.rc = 0xB4;
    cpu.rs = 4;
    c.call(0xA7E5);
    cpu.ar[2] = 0x78 + cpu.r[4];
    cpu.r[3] = 0x163;
    cpu.r[2] = 0x96;
    cpu.rc = 0xB4;
    cpu.rs = 8;
    c.call(0xA7E5);
}

/// R4 limitado por baixo pelo volume mínimo (item 0x20); grava o volume (0x910F, item 8) e redesenha.
void applyVolume(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r0 = cpu.r[0];
    const uint32_t minimum = c.item(0x20);
    if (Ctx::s32(cpu.r[4]) < Ctx::s32(minimum)) cpu.r[4] = minimum;
    cpu.r[0] = r0;
    cpu.r[1] = cpu.r[4];
    c.call(0x910F);
    c.setItem(8, cpu.r[4]);
    volumeBar(c);
}

/// `0x0D1F`: tela de volume.
void volumeScreen(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    clearScreen(c);
    static constexpr Shape kFrame[] = {
        {0x5A, 0x3C, 0x19A, 0x3C, 0xB},  {0x5B, 0x3D, 0x199, 0x3D, 0xD},  {0x5A, 0x3C, 0x5A, 0xF0, 0xB},
        {0x5B, 0x3D, 0x5B, 0xEF, 0xD},   {0x19A, 0x3C, 0x19A, 0xF0, 0xB}, {0x199, 0x3D, 0x199, 0xEF, 0xD},
        {0x5B, 0xF0, 0x199, 0xF0, 0xD},  {0x5C, 0xEF, 0x198, 0xEF, 0xB}};
    drawShapes(c, 0xAC23, kFrame);
    bevelBox(c, 0x78, 0x48, 0xF0, 0x5A, false);
    c.textAt(0x7D, 0x4C, 0xC860, 0xF);
    bevelBox(c, 0x77, 0x95, 0x17B, 0xB5, false);
    c.shape(0xA7E5, 0x78, 0x96, 0x17A, 0xB4, 8);
    cpu.rc = 0xF;
    static constexpr Label kLabels[] = {{0x76, 0x87, 0xC861},
                                        {0x168, 0x87, 0xC862},
                                        {0x82, 0xC8, 0xC863},
                                        {0x82, 0xD2, 0xC864},
                                        {0x82, 0xDC, 0xC865}};
    drawLabels(c, kLabels);
    cpu.r[4] = c.item(8);
    const uint32_t minimum = c.item(0x20);
    if (Ctx::s32(cpu.r[4]) < Ctx::s32(minimum)) cpu.r[4] = minimum;
    volumeBar(c);
    c.sound2(0x00DE);
    c.sound2(0x023D);
    for (;;) {
        // 00D86  de vez em quando (1 em 701) toca um som de amostra da tabela [C866]
        cpu.ar[2] = 0x2BC;
        c.call(0x8EDB);
        if (cpu.r[0] == 0) {
            cpu.ar[2] = 6;
            c.call(0x8EDB);
            cpu.ar[2] = c.rd(c.rd(0xC866) + cpu.r[0]);
            c.call(0x91E0);
        }
        cpu.r[0] = c.rd(vars::kKeys);
        cpu.r[1] = cpu.r[0];
        if ((cpu.r[1] & keys::kEnter) == 0) {
            c.sound2(0x026D);
            return;
        }
        if ((cpu.r[0] & keys::kUp) == 0) {
            cpu.r[4] += 1;
            if (Ctx::s32(cpu.r[4]) > 0xFF) cpu.r[4] = 0xFF;
            applyVolume(c);
            c.burn(kVolumeStep);
        } else if ((cpu.r[0] & keys::kDown) == 0) {
            cpu.r[4] -= 1;
            if (Ctx::s32(cpu.r[4]) < 1) cpu.r[4] = 1;
            applyVolume(c);
            c.burn(kVolumeStep);
        } else {
            c.burn(kVolumeIdle);
        }
    }
}

// --- 0x13D5: segurança do movimento ---------------------------------------------------------

/// Retângulos dos sensores (tapetes, óticos e chaves de segurança), na ordem da `0x137A`.
constexpr Shape kSensorBoxes[] = {
    {0x20, 0x52, 0x80, 0x76, 0},   {0x20, 0x8E, 0x3A, 0xDA, 0},   {0x66, 0x8E, 0x80, 0xDA, 0},
    {0x20, 0xE8, 0x80, 0x116, 0},  {0x16A, 0x52, 0x18E, 0x76, 0}, {0x14C, 0xE8, 0x166, 0x116, 0},
    {0x192, 0xE8, 0x1AC, 0x116, 0}, {0xB6, 0xD4, 0xC6, 0xE4, 0},  {0xCF, 0xE8, 0xDF, 0xF8, 0},
    {0xED, 0xE8, 0xFD, 0xF8, 0},   {0x106, 0xD4, 0x116, 0xE4, 0}, {0xB6, 0x70, 0xC6, 0x80, 0},
    {0xCF, 0x52, 0xDF, 0x62, 0},   {0xED, 0x52, 0xFD, 0x62, 0},   {0x106, 0x70, 0x116, 0x80, 0}};

/// Porta do movimento (0x991030) com a leitura fictícia de `[0]` em AR2.
uint32_t readMotionPort(Ctx& c) {
    c.cpu.ar[2] = 0;
    const uint32_t v = c.rd(0x991030);
    c.cpu.ar[2] = c.rd(0);
    c.cpu.dp = 0;
    return v;
}

/// Mensagem de erro do movimento: moldura, texto `[ptr]` na cor 8 e os sensores acionados.
struct MotionError {
    uint32_t addr;  ///< Endereço da rotina na ROM.
    uint32_t msg;   ///< Ponteiro do texto do erro.
    /// Um sensor: bit na porta do movimento e caixa marcada quando ele está acionado.
    struct Bit {
        uint32_t mask;  ///< Bit do sensor (0 = sem sensor).
        Shape box;      ///< Caixa desenhada na tela.
    };
    Bit bits[4];    ///< Sensores mostrados com esta mensagem.
};

/// Desenha a mensagem de erro do movimento @p e e marca os sensores acionados.
void motionError(Ctx& c, const MotionError& e) {
    Tms32031State& cpu = c.cpu;
    bevelBox(c, 0x32, 0x140, 0x1C2, 0x168, false);
    c.textAt(0x46, 0x14A, e.msg, 8);
    bool any = false;
    for (const auto& b : e.bits) any = any || b.mask != 0;
    if (!any) return;
    cpu.r[4] = readMotionPort(c) >> 8;
    for (const auto& b : e.bits) {
        if (b.mask == 0) continue;
        if (cpu.r[4] & b.mask) c.shape(0xA7E5, b.box.ar2, b.box.r2, b.box.r3, b.box.rc, 3);
    }
}

/// Sensores dos tapetes (bits da porta do movimento e caixas na tela).
constexpr MotionError::Bit kMats[4] = {{1, {0x66, 0x8E, 0x80, 0xDA, 3}}, {2, {0x20, 0xE8, 0x80, 0x116, 3}},
                                       {4, {0x20, 0x8E, 0x3A, 0xDA, 3}}, {8, {0x20, 0x52, 0x80, 0x76, 3}}};
/// Sensores de segurança da plataforma.
constexpr MotionError::Bit kFailSafe[4] = {{4, {0x16A, 0x52, 0x18E, 0x76, 3}},
                                           {1, {0x14C, 0xE8, 0x166, 0x116, 3}},
                                           {2, {0x192, 0xE8, 0x1AC, 0x116, 3}},
                                           {0, {}}};

/// Mensagens de erro do movimento, uma por rotina da ROM.
const MotionError kMotionErrors[] = {
    {0x11B1, 0xC8A4, {}},
    {0x11BC, 0xC8A5, {kMats[0], kMats[1], kMats[2], kMats[3]}},
    {0x11ED, 0xC8A6, {kMats[0], kMats[1], kMats[2], kMats[3]}},
    {0x121E, 0xC8A7, {}},
    {0x1229, 0xC8A8, {{1, {0xB6, 0xD4, 0xC6, 0xE4, 3}}, {2, {0xCF, 0xE8, 0xDF, 0xF8, 3}},
                      {4, {0xED, 0xE8, 0xFD, 0xF8, 3}}, {8, {0x106, 0xD4, 0x116, 0xE4, 3}}}},
    {0x125A, 0xC8A9, {{1, {0xB6, 0x70, 0xC6, 0x80, 3}}, {2, {0xCF, 0x52, 0xDF, 0x62, 3}},
                      {4, {0xED, 0x52, 0xFD, 0x62, 3}}, {8, {0x106, 0x70, 0x116, 0x80, 3}}}},
    {0x128B, 0xC8AA, {kFailSafe[0], kFailSafe[1], kFailSafe[2], kFailSafe[3]}},
    {0x12B4, 0xC8AB, {kFailSafe[0], kFailSafe[1], kFailSafe[2], kFailSafe[3]}},
};

/// `0x1170`: apaga os sensores (`0x137A`) e mostra o primeiro erro da tabela `[C8A2]` (máscara,
/// valor, rotina; −1 no fim) que casa com `[0x991030] >> 8`; sem erro, "ALL SYSTEMS OK".
void motionStatus(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3];
    drawShapes(c, 0xA7E5, kSensorBoxes);
    cpu.r[0] = readMotionPort(c) >> 8;
    cpu.ar[2] = c.rd(0xC8A2);
    for (;;) {
        cpu.r[1] = c.rd(cpu.ar[2]);
        if (cpu.r[1] == 0xFFFFFFFFu) {
            bevelBox(c, 0x32, 0x140, 0x1C2, 0x168, false);
            c.textAt(0x46, 0x14A, 0xC8A3, 8);
            break;
        }
        cpu.r[1] &= c.rd(cpu.ar[2] + 1);
        cpu.r[3] = cpu.r[0] & c.rd(cpu.ar[2] + 1);
        if (cpu.r[1] == cpu.r[3]) {
            c.call(c.rd(cpu.ar[2] + 2));
            break;
        }
        cpu.ar[2] += 3;
    }
    cpu.r[3] = r3;
    cpu.r[2] = r2;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}

/// `0x12DD`: desenho fixo da tela (tapetes, óticos, chaves de segurança e legendas).
void motionLayout(Ctx& c) {
    c.cpu.rc = 8;
    static constexpr Label kMatLabels[] = {{0x32, 0x32, 0xC8AC}, {0x37, 0x48, 0xC8AD}, {0x37, 0x11D, 0xC8AE}};
    drawLabels(c, kMatLabels);
    static constexpr Shape kMatBoxes[] = {{0x1E, 0x50, 0x82, 0x78, 8},
                                          {0x1E, 0x8C, 0x3C, 0xDC, 8},
                                          {0x64, 0x8C, 0x82, 0xDC, 8},
                                          {0x1E, 0xE6, 0x82, 0x118, 8}};
    drawShapes(c, 0xABFC, kMatBoxes);
    c.cpu.rc = 8;
    static constexpr Label kOptos[] = {{0xC8, 0x32, 0xC8AF}, {0xCD, 0x48, 0xC8B0}, {0xCD, 0x11D, 0xC8B1}};
    drawLabels(c, kOptos);
    static constexpr Shape kOptoBoxes[] = {
        {0xB4, 0x6E, 0xC8, 0x82, 8},  {0xCD, 0x50, 0xE1, 0x64, 8},  {0xEB, 0x50, 0xFF, 0x64, 8},
        {0x104, 0x6E, 0x118, 0x82, 8}, {0xB4, 0xD2, 0xC8, 0xE6, 8},  {0xCD, 0xE6, 0xE1, 0xFA, 8},
        {0xEB, 0xE6, 0xFF, 0xFA, 8},  {0x104, 0xD2, 0x118, 0xE6, 8}};
    drawShapes(c, 0xABFC, kOptoBoxes);
    c.cpu.rc = 8;
    static constexpr Label kSwitches[] = {{0x12C, 0x32, 0xC8B2}, {0x169, 0x48, 0xC8B3}, {0x16D, 0x11D, 0xC8B4}};
    drawLabels(c, kSwitches);
    static constexpr Shape kSwitchBoxes[] = {
        {0x168, 0x50, 0x190, 0x78, 8}, {0x14A, 0xE6, 0x168, 0x118, 8}, {0x190, 0xE6, 0x1AE, 0x118, 8}};
    drawShapes(c, 0xABFC, kSwitchBoxes);
    c.cpu.rc = 8;
    static constexpr Label kTitles[] = {
        {0x14, 0x17C, 0xC8B5}, {0x1E, 0x14, 0xC8B6}, {0x32, 0x131, 0xC8B7}, {0x12C, 0x0F, 0xC8B8}};
    drawLabels(c, kTitles);
    bevelBox(c, 0x32, 0x140, 0x1C2, 0x168, false);
    bevelBox(c, 0x186, 0x05, 0x1AE, 0x23, false);
}

/// Bits 7–15 da porta do movimento, como em `0x13DD` (LSH −7, LSH 23, LSH −16).
uint32_t motionStopBits(uint32_t port) {
    return tmsLsh(tmsLsh(tmsLsh(port, static_cast<uint32_t>(-7)), 23), static_cast<uint32_t>(-16));
}

/// `0x13D5`: tela de segurança do movimento.
void motionSafety(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    clearScreen(c);
    motionLayout(c);
    motionStatus(c);
    c.wr(0xEBE3, motionStopBits(readMotionPort(c)));
    for (;;) {
        // 013E1  ENTER sai; o indicador "MOTION STOP" segue o bit 7 da porta
        c.burn(cycles::kPoll);
        if (~c.rd(vars::kKeys) & keys::kEnter) return;
        cpu.r[0] = readMotionPort(c);
        c.shape(0xA7E5, 0x190, 0x0A, 0x1A4, 0x1E, (cpu.r[0] & 0x80u) ? 3u : 0u);
        cpu.r[0] = motionStopBits(readMotionPort(c));
        if (cpu.r[0] == c.rd(0xEBE3)) continue;
        c.wr(0xEBE3, cpu.r[0]);
        c.shape(0xA7E5, 0x12C, 0x131, 0x1F4, 0x13B, 0);
        cpu.r[2] = (readMotionPort(c) >> 8) & 0xFFu;
        cpu.ar[2] = c.rd(0xC8B9);
        c.call(0xA7AC);
        cpu.rc = 8;
        cpu.r[2] = 0x12C;
        cpu.r[3] = 0x131;
        c.call(0xA7F4);
        motionStatus(c);
    }
}

} // namespace

/// Registra os testes de diagnóstico do menu.
void registerDiagnostics(SubroutineDispatcher& d, IBusyWait& wait) {
    addSvc(d, wait, 0x0724, switchTest);
    addSvc(d, wait, 0x0895, switchStates);
    addSvc(d, wait, 0x08BA, shifterState);
    addSvc(d, wait, 0x08FF, potReadings);
    addSvc(d, wait, 0x0933, dipTest);
    addSvc(d, wait, 0x0942, dipColumn);
    addSvc(d, wait, 0x095A, dipScreen);
    addSvc(d, wait, 0x06B3, monitorTest);
    addSvc(d, wait, 0x06ED, colorBars);
    addSvc(d, wait, 0x0B02, soundTest);
    addSvc(d, wait, 0x0B65, wheelTest);
    addSvc(d, wait, 0x0BA5, burnIn);
    addSvc(d, wait, 0x0D1F, volumeScreen);
    addSvc(d, wait, 0x0DBA, volumeBar);
    addSvc(d, wait, 0x13D5, motionSafety);
    addSvc(d, wait, 0x1170, motionStatus);
    addSvc(d, wait, 0x12DD, motionLayout);
    addSvc(d, wait, 0x137A, [](Ctx& c) { drawShapes(c, 0xA7E5, kSensorBoxes); });
    for (const MotionError& e : kMotionErrors) {
        addSvc(d, wait, e.addr, [&e](Ctx& c) { motionError(c, e); });
    }
}

} // namespace rea::core::service
