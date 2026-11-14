/**
 * @file course_select.cpp
 * @brief Tradução de `0x6C8F`–`0x6FE8` (seleção de percurso), `0x5CC0`–`0x5CDC`, `0x1C62`, `0x1C85`,
 *        `0x1D38`–`0x1D51`, `0x8BE4` e `0xAF13`.
 */
#include "core/game/course_select.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
constexpr uint32_t kNone = 0xFFFFFFFFu;  ///< Marcador de "nenhum" (−1).

// Variáveis do processo (`*+AR7(n)`).
constexpr uint32_t kVarCar      = 0xC;  ///< Objeto do carrinho que anda pelo mapa.
constexpr uint32_t kVarSpecial  = 0xD;  ///< Objeto 0x410 (removido; só o modelo fica guardado).
constexpr uint32_t kVarShown    = 0xE;  ///< Percurso mostrado por último (`0x6F93`).
constexpr uint32_t kVarCarModel = 0xF;  ///< Modelo do carrinho.
constexpr uint32_t kVarAltModel = 0x10; ///< Modelo do carrinho no percurso 5 (o do objeto 0x410).
constexpr uint32_t kVarButton   = 0x11; ///< Percurso especial pelos botões de vista (1..3) ou −1.

/// Variáveis globais.
constexpr uint32_t kCourse     = 0xE634;
constexpr uint32_t kRaceKind   = 0xE636;  ///< 1 = cross-country (etapas em sequência)
constexpr uint32_t kWheelIndex = 0xEA47; ///< Faixa do volante (0..`[E638]` − 1).
constexpr uint32_t kWheelSlots = 0xE638;  ///< Quantidade de faixas do volante.
constexpr uint32_t kTimer      = 0xE604;  ///< Contagem regressiva por segundo, só com `[C8C5] & 0x200`.
constexpr uint32_t kMapChain   = 0xEA31; ///< Primeiro objeto da cadeia `+0x1F` do mapa.
constexpr uint32_t kChosenObj  = 0xEA32;  ///< Objeto do percurso escolhido.
constexpr uint32_t kPrevCourse = 0xEA2D;  ///< Percurso escolhido antes.
constexpr uint32_t kLinkCourse = 0xC8D7; ///< Percurso escolhido pelo outro gabinete (−1 = nenhum).
constexpr uint32_t kLinked     = 0xC8E3;  ///< 1 = corrida com link em andamento (tipo 5); 0 nos tipos 6 e 8.
constexpr uint32_t kStartReq   = 0xE631;  ///< ≠ 0: o jogador apertou para pular

/// `LDI obj,AR2 / CALL $70BB` → AR0 = objeto com esse tipo.
uint32_t findObj(ProcCtx& c, uint32_t type) {
    c.cpu.ar[2] = type;
    c.call(0x70BB);
    return c.cpu.ar[0];
}

// --- Rotinas de pedal e volante ------------------------------------------------------------

/// `0x1C62`: o volante escolhe a faixa `[EA47]` entre `[E638]` (histerese de 0,6 faixa).
void wheelToSlot(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r4 = cpu.r[4];
    cpu.r[4] = c.rd(0xC8C5) & 0xFu;
    const double wheel = static_cast<double>(s(c.rd(0xC8C8)));
    cpu.r[0] = c.rd(0xE8A1);  // largura do curso do volante
    cpu.r[1] = doubleToTms(static_cast<double>(s(c.rd(kWheelSlots))));
    c.call(0xA509);           // R0 = largura de uma faixa
    const double slot = tmsToDouble(cpu.r[0]);
    double center = static_cast<double>(s(c.rd(kWheelIndex))) * slot + slot * 0.5 + c.rf(0xE89C);
    if (cpu.r[4] != 6) c.wf(0xEA48, center);
    const double diff = center - wheel;
    if (std::fabs(diff) > slot * c.k(0x1C75)) {
        int32_t idx = s(c.rd(kWheelIndex)) + (diff < 0.0 ? 1 : -1);
        if (idx < 0) idx = 0;
        if (idx >= s(c.rd(kWheelSlots))) idx = s(c.rd(kWheelSlots)) - 1;
        cpu.r[0] = static_cast<uint32_t>(idx);
        c.wr(kWheelIndex, cpu.r[0]);
    }
    cpu.r[4] = r4;
}

/// `0x1C85`: carry = 1 na borda de subida do acelerador (R5 guarda o estado anterior).
void gasEdge(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(0xC93B);
    cpu.dp = 0;
    cpu.r[1] = static_cast<uint32_t>(static_cast<int32_t>(std::floor(c.rf(0xE89A)))) + 0x14;
    bool edge = false;
    if (s(cpu.r[0]) < s(cpu.r[1])) cpu.r[5] = 0;
    else edge = cpu.r[5] == 0;
    cpu.setFlag(Tms32031State::kStCarry, edge);
}

/// `0x1D38` / `0x1D3F` / `0x1D41`: tempo `[E604]` centralizado em (256, y), paleta `[B225]`.
void timerText(ProcCtx& c, double y) {
    Tms32031State& cpu = c.cpu;
    cpu.r[2] = c.rd(kTimer);
    cpu.ar[2] = c.rd(0x9CE0);
    c.call(0xA762);  // número → texto em [9CE0] (AR2 segue apontando para ele)
    cpu.r[2] = doubleToTms(256.0);
    cpu.r[3] = doubleToTms(y);
    cpu.rc = 1;
    c.call(0x7660);
    c.orField(cpu.ar[0], 8, 0x100);
    c.call(0x76A7);
    cpu.ar[2] = c.rd(0xB225);
    c.call(0x9EDB);
    c.wr(cpu.ar[0] + 0xC, cpu.r[0]);
}

/// `0xAF13`: retângulo de imagem AR2 em (R2, R3) com a paleta R4 (`0xAECC`). Preserva R0–R7, AR0–AR3.
void drawImage(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const Tms32031State saved = cpu;
    const uint32_t img = cpu.ar[2];
    cpu.ar[0] = (c.rd(img + 0xA) >> 16) + c.rd(0x0062);
    cpu.rs = c.rd(cpu.ar[0]) >> 8;
    c.wr(0xEA34, cpu.r[4]);
    cpu.r[4] = c.rd(img + 0xC);
    c.wr(0xEA42, cpu.r[4]);
    cpu.r[5] = cpu.r[4] >> 16;
    c.wr(0xEA43, cpu.r[5]);
    cpu.r[6] = c.rd(img + 0xD);
    c.wr(0xEA44, cpu.r[6]);
    cpu.r[7] = cpu.r[6] >> 16;
    c.wr(0xEA45, cpu.r[7]);
    cpu.re = c.rd(img + 0xE);
    const uint32_t y = cpu.r[3];
    cpu.r[4] &= 0xFFu;
    cpu.r[6] &= 0xFFu;
    cpu.ar[2] = cpu.r[2];
    cpu.r[6] -= cpu.r[4];
    cpu.r[3] = cpu.r[2] + cpu.r[6];  // x final
    cpu.r[2] = y;
    cpu.r[7] = (cpu.r[7] >> 8) & 0xFFu;
    cpu.r[0] = cpu.r[2] + cpu.r[7];  // y final
    cpu.rc = cpu.r[0];
    c.call(0xAECC);
    for (int i = 0; i < 8; ++i) cpu.r[i] = saved.r[i];
    for (int i = 0; i < 4; ++i) cpu.ar[i] = saved.ar[i];
}

/// `0x8BE4`: libera as paletas da seleção (`[C63A]` condicional, `[C63C]`..`[C64F]`).
void releaseSelectPalettes(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(0xC63A);
    c.call(0x9EDB);
    if (cpu.carry()) {
        cpu.ar[2] = c.rd(0xC63B);
        c.call(0x9EEC);
    }
    for (uint32_t p = 0xC63C; p <= 0xC64F; ++p) {
        cpu.ar[2] = c.rd(p);
        c.call(0x9DFE);
    }
}

// --- Objetos do mapa -----------------------------------------------------------------------

/// `0x6ECD`: guarda a posição final dos objetos, sobe-os aleatoriamente e monta a cadeia `+0x1F`.
void prepareMap(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(kMapChain, 0);
    cpu.r[6] = kNone;
    auto link = [&]() {
        if (cpu.r[6] == kNone) cpu.r[6] = cpu.ar[0];
        c.wr(cpu.ar[0] + 0x1F, cpu.ar[5]);
    };
    auto spawnAnim = [&](uint32_t pcPtr) {
        cpu.ar[4] = cpu.ar[0];
        cpu.ar[2] = c.rd(pcPtr);
        cpu.r[2] = 0x5C5;
        c.call(0xA844);
        cpu.ar[0] = cpu.ar[4];
    };
    for (cpu.ar[5] = c.rd(0xC982); cpu.ar[5] != 0;) {
        cpu.ar[0] = cpu.ar[5];
        cpu.ar[5] = c.rd(cpu.ar[0]);
        for (uint32_t i = 1; i <= 3; ++i) c.wr(cpu.ar[0] + 0x10 + i, c.rd(cpu.ar[0] + i));
        cpu.ar[2] = 0x6D60;
        c.call(0x8EDB);  // altura extra sorteada
        c.wf(cpu.ar[0] + 3, static_cast<double>(s(cpu.r[0])) + c.rf(cpu.ar[0] + 3));
        cpu.r[0] = doubleToTms(6.0);
        c.call(0x8ED1);
        c.wr(cpu.ar[0] + 0x16, cpu.r[0]);
        const uint32_t type = c.rd(cpu.ar[0] + 0xF);
        switch (type) {
        case 0x40C:  // carrinho
            c.setVar(kVarCar, cpu.ar[0]);
            c.setVar(kVarCarModel, c.rd(cpu.ar[0] + 0xD));
            link();
            break;
        case 0x8A1: spawnAnim(0xC6FA); link(); break;
        case 0x8A2: spawnAnim(0xC6FB); link(); break;
        case 0x410:
            c.setVar(kVarSpecial, cpu.ar[0]);
            c.setVar(kVarAltModel, c.rd(cpu.ar[0] + 0xD));
            cpu.ar[2] = cpu.ar[0];
            c.call(0x7100);
            link();
            break;
        case 0x405: {
            const uint32_t obj = cpu.ar[0];
            spawnAnim(0xC6FC);
            cpu.ar[0] = obj;
            link();
            break;
        }
        case 0x40E:
        case 0x40F:
            cpu.ar[2] = cpu.ar[0];
            c.call(0x7100);
            break;
        default: link(); break;
        }
    }
    c.wr(kMapChain, cpu.r[6]);
}

/// `0x6EB0`: um passo da descida dos objetos até a altura final.
void dropMapStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (cpu.ar[5] = c.rd(0xC982); cpu.ar[5] != 0;) {
        cpu.ar[0] = cpu.ar[5];
        cpu.ar[5] = c.rd(cpu.ar[0]);
        const double step = (c.rf(cpu.ar[0] + 0x13) - c.rf(cpu.ar[0] + 3)) * c.k(0x6EB7);
        if (step > -5.0) {
            c.wr(cpu.ar[0] + 3, c.rd(cpu.ar[0] + 0x13));
            cpu.r[2] = doubleToTms(0.0);
        } else {
            c.wf(cpu.ar[0] + 3, step + c.rf(cpu.ar[0] + 3));
            cpu.r[2] = doubleToTms(c.rf(cpu.ar[0] + 0x16) * c.k(0x6EC6));
        }
        c.wr(cpu.ar[0] + 0x16, cpu.r[2]);
        cpu.ar[2] = cpu.ar[0] + 4;
        c.call(0x93C4);
    }
}

/// `0x6DFE`: devolve o modelo original ao marcador especial R0 (1..3; −1 = nenhum).
void restoreSpecial(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (cpu.r[0] == kNone) return;
    const uint32_t ar0 = cpu.ar[0], ar2 = cpu.ar[2];
    const uint32_t type = cpu.r[0] == 2 ? 0x406 : cpu.r[0] == 3 ? 0x409 : 0x401;
    const uint32_t obj = findObj(c, type);
    cpu.r[0] = c.rd(obj + 0x1B);
    c.wr(obj + 0xD, cpu.r[0]);
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
}

/// `0x6E0D`: botões de vista 1/2/3 (`[C937]` bits 20–22) escolhem um percurso especial.
void specialButtons(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t buttons = (c.rd(0xC937) >> 16) & 0x70u;
    struct Special { uint32_t mask, sel, type, modelPtr; };
    static constexpr Special kSpecials[] = {{0x30, 1, 0x401, 0xC6F7}, {0x50, 2, 0x406, 0xC6F8}, {0x60, 3, 0x409, 0xC6F9}};
    for (const Special& sp : kSpecials) {
        if (buttons != sp.mask) continue;
        cpu.r[0] = c.var(kVarButton);
        if (cpu.r[0] == sp.sel) return;
        restoreSpecial(c);
        c.setVar(kVarButton, sp.sel);
        const uint32_t obj = findObj(c, sp.type);
        c.wr(obj + 0xE, c.rd(obj + 0xE) & ~0x400u);
        c.wr(obj + 0x1B, c.rd(obj + 0xD));
        cpu.r[0] = c.rd(sp.modelPtr);
        c.wr(obj + 0xD, cpu.r[0]);
        return;
    }
    cpu.r[0] = c.var(kVarButton);
    if (cpu.r[0] == kNone) return;
    restoreSpecial(c);
    c.setVar(kVarButton, kNone);
}

/// `0x6E4F`: percurso pelo volante e o nome dele sobre o marcador.
void showCourse(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x1C62);
    cpu.r[0] = c.rd(kWheelIndex);
    if (s(cpu.r[0]) > 5) --cpu.r[0];
    if (cpu.r[0] != c.rd(kCourse)) {
        const uint32_t r0 = cpu.r[0];
        c.sound(0xD5);
        cpu.r[0] = r0;
    }
    c.wr(kCourse, cpu.r[0]);
    // 06E5B  percurso especial no lugar do 1, 6 ou 9 → nome 0xB, 0xC ou 0xD
    const uint32_t button = c.var(kVarButton);
    uint32_t name = cpu.r[0];
    if (button != kNone) {
        if (cpu.r[0] == 1 && button == 1) name = 0xB;
        else if (cpu.r[0] == 6 && button == 2) name = 0xC;
        else if (cpu.r[0] == 9 && button == 3) name = 0xD;
    }
    cpu.ar[3] = name;
    const uint32_t marker = findObj(c, c.rd(kCourse) + 0x400);
    const double x = c.rf(marker + 1) + 256.0, y = c.rf(marker + 2) + 180.0;
    const uint32_t textIdx = c.rd(c.rd(0x6FC5) + cpu.ar[3]);
    c.text(0x7660, c.rd(textIdx + c.rd(0x3953)), x, y, 1, false);
    c.call(0x76D8);
    c.orField(cpu.ar[0], 8, 0x100);
    if (cpu.ar[3] == 5 && c.item(0x24) == 1) {
        for (auto [ptr, ty] : {std::pair{0x6C8Du, 120.0}, std::pair{0x6C8Eu, 135.0}}) {
            c.text(0x7660, c.rd(ptr), 256.0, ty, 1, false);
            c.call(0x76D8);
            c.orField(cpu.ar[0], 8, 0x100);
        }
    }
}

/// `0x6F93`: põe o carrinho sobre o marcador do percurso atual (8 acima).
void moveCar(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.var(kVarShown);
    cpu.ar[0] = c.rd(kCourse);
    if (cpu.r[0] == cpu.ar[0]) return;
    c.setVar(kVarShown, cpu.ar[0]);
    cpu.ar[3] = cpu.ar[0];
    const uint32_t marker = findObj(c, cpu.ar[0] + 0x400);
    cpu.r[0] = cpu.ar[3] == 5 ? c.var(kVarAltModel) : c.var(kVarCarModel);
    cpu.ar[1] = c.var(kVarCar);
    c.wr(cpu.ar[1] + 0xD, cpu.r[0]);
    c.wr(cpu.ar[1] + 1, c.rd(marker + 1));
    c.wr(cpu.ar[1] + 2, c.rd(marker + 2));
    c.wf(cpu.ar[1] + 3, c.rf(marker + 3) - 8.0);
}

/// `0x6F5A`: nome do percurso escolhido sobre o marcador `AR4`.
void chosenName(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const double x = c.rf(cpu.ar[4] + 1) + 256.0, y = c.rf(cpu.ar[4] + 2) + 180.0;
    const uint32_t idx = c.rd(kRaceKind) == 1 ? 0xE : c.rd(kCourse);
    c.text(0x7660, c.rd(idx + c.rd(0x3953)), x, y, 1, false);
    c.call(0x76D8);
    c.orField(cpu.ar[0], 8, 0x100);
}

/// `0x6F6F`: afasta os outros objetos da cadeia (x, y × 1,1; z sobe no mínimo 50).
void pushOthers(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (cpu.r[0] = c.rd(kMapChain); cpu.r[0] != 0; cpu.r[0] = c.rd(cpu.ar[0] + 0x1F)) {
        cpu.ar[0] = cpu.r[0];
        if (cpu.r[0] == c.rd(kChosenObj)) continue;
        c.wf(cpu.ar[0] + 1, c.rf(cpu.ar[0] + 1) * c.k(0x6F77) + c.rf(cpu.ar[0] + 1));
        c.wf(cpu.ar[0] + 2, c.rf(cpu.ar[0] + 2) * c.k(0x6F7B) + c.rf(cpu.ar[0] + 2));
        c.wf(cpu.ar[0] + 3, std::max(c.rf(cpu.ar[0] + 3) * c.k(0x6F7F), 50.0) + c.rf(cpu.ar[0] + 3));
    }
}

// --- Processo (chamada local [B1EB]) ------------------------------------------------------

void csBuild(ProcCtx& c);
void csPick(ProcCtx& c);
void csPickLoop(ProcCtx& c);
void csConfirm(ProcCtx& c);
void csResolve(ProcCtx& c);

/// `0x6C8F`: espera o DMA e pede a troca de página.
void csStart(ProcCtx& c) {
    c.wr(0xC940, 0);
    c.wr(kStartReq, 0);
    c.waitDma(1);
    c.waitDma(2);
    c.wr(0xC93F, 1);
    csBuild(c);  // 06C9E
}

/// `0x6C9E`: espera a INT0 atender `[C93F]` (port: 1 quadro por vez) e reinicia as paletas.
void csBuild(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(0xC93F) != 0) return c.sleep(1, 0x6C9E);
    c.call(0x95F5);
    c.setVar(kVarShown, kNone);
    c.setVar(kVarButton, kNone);
    cpu.ar[2] = c.rd(0xC6DF);
    c.call(0x9DF6);
    c.sleep(1, 0x6CA8);
}

/// Trecho traduzido da ROM em `0x6CA8`.
void csLoadTextures(ProcCtx& c) {
    c.waitDma(1);
    c.waitDma(2);
    c.call(0x4EE3);
    c.cpu.ar[2] = c.rd(0xC6E0);
    c.call(0xA3C2);
    c.sleep(1, 0x6CB7);
}

/// `0x6CB7`: espera a descompressão e monta o mapa.
void csMap(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(0xE5CA) != 0) return c.sleep(1, 0x6CB7);
    auto with = [&](uint32_t fn, uint32_t ptr) { cpu.ar[2] = c.rd(ptr); c.call(fn); };
    with(0x9DFE, 0xC6E1);
    with(0x9DF6, 0xC6E2);
    with(0xA3C2, 0xC6E3);
    c.call(0x6FEB);
    c.call(0x6FD4);
    if (c.rd(0xE637) != 0) {
        with(0xA3C2, 0xC6E4);
        with(0x9DF6, 0xC6E5);
    }
    c.wf(0x809800, 0.0);
    c.wf(0x809801, 0.0);
    c.wf(0x809802, -368.0);
    cpu.dp = 0;
    c.wr(kWheelSlots, 12);
    c.wr(kRaceKind, 2);
    with(0x440C, 0xC6E6);
    c.call(0x6ECD);
    c.sound(0x1B9);
    c.wr(kWheelIndex, 6);
    c.lcall(0xC6E7, 0xC6E8);  // 0x6EA2 → 0x6CDF
}

/// `0x6EA2`/`0x6EA3`: 21 quadros de descida (com o percurso já visível se o link não escolheu).
void dropFrame(ProcCtx& c) {
    c.call(0x6EB0);
    if (c.rd(kLinkCourse) == kNone) {
        c.call(0x6E4F);
        c.call(0x6F93);
    }
    c.sleep(1, 0x6EAB);
}

/// `0x6CDF`: com o item 0x80 (modem) ligado, contagem de 10 s e reinício (`0x4B28`).
void csAfterDrop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.item(0x80) == 0) return csPick(c);
    c.call(0x6FEB);
    cpu.ar[5] = 0xA;
    c.jump(0x6CE5);
}

/// Trecho traduzido da ROM em `0x6CE5`.
void csModemFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (auto [ptr, y] : {std::pair{0xC6E9u, 50.0}, std::pair{0xC6EAu, 120.0}, std::pair{0xC6EBu, 190.0}}) {
        c.text(0x7651, c.rd(ptr), 256.0, y, 0x3C, true);
        c.call(0x76C9);
    }
    cpu.r[2] = cpu.ar[5];
    cpu.ar[2] = c.rd(0xC6EC);
    c.call(0xA762);
    c.text(0x7651, c.rd(0xC6EC), 256.0, 260.0, 0x3C, true);
    c.call(0x76B1);
    c.sound(0x240);
    c.sleep(0x3C, 0x6D1C);
}

/// `0x6D20`: atrato/continuação sem jogador; jogo com jogador vai para `0x6D48`.
void csPick(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(0xE637) != 0) return csPickLoop(c);
    if (c.rd(0xE53F) == 0 && c.rd(0xE607) == 1 && c.item(0x1A) != 0) {
        cpu.ar[2] = c.rd(0xE481) - 1 + 0xB5;
        c.call(0x99DA);
        c.audit(0xD3);
        c.audit(0x6A);
        c.call(0x9937);
        return c.lcall(0xC6EF, 0xC6F0);  // 0x1720 → 0x6D48
    }
    c.call(0x6FDB);
    c.lcall(0xC6ED, 0xC6EE);  // 0x1E2C → 0x6D3C
}

/// `0x6D48`: 12 s de escolha.
void csPickLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(0xC8C5, (c.rd(0xC8C5) & ~0xDFFu) | 0x203u);
    c.wr(kTimer, 12);
    c.call(0x1C5B);
    c.wr(kStartReq, 0);
    if (c.rd(kLinked) == 0 || c.rd(kLinkCourse) == kNone) {
        cpu.ar[5] = 8;
        return c.jump(0x6D57);
    }
    c.jump(0x6D5C);
}

/// Trecho traduzido da ROM em `0x6D57`.
void csSettle(ProcCtx& c) {
    c.call(0x6E4F);
    c.call(0x6F93);
    c.sleep(1, 0x6D5B);
}

/// `0x6D5E`: um quadro de escolha.
void csChoose(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kLinked) != 0 && c.rd(kLinkCourse) != kNone) {
        c.setVar(kVarButton, c.rd(0xC8DF));
        c.wr(kCourse, c.rd(kLinkCourse));
        c.wr(kRaceKind, c.rd(0xC8E0));
        return csConfirm(c);
    }
    if (c.rd(kStartReq) != 0) return csConfirm(c);
    c.call(0x6E0D);
    c.call(0x6E4F);
    c.call(0x6F93);
    cpu.ar[2] = c.rd(0xC6F1);
    cpu.r[2] = 0xF2;
    cpu.r[3] = 0xF0;
    cpu.r[4] = 0x900;
    c.call(0xAF13);
    c.text(0x7651, c.rd(0x6C8C), 256.0, 30.0, 1, true);
    c.call(0x1C85);
    if (cpu.carry()) return csConfirm(c);
    c.call(0x1D38);
    c.sleep(1, 0x6D83);
}

/// `0x6D85`: escolha feita; com link, espera até 20 quadros pelo outro gabinete.
void csConfirm(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(0xEA30, c.var(kVarButton));
    c.call(0x7A98);
    if (c.rd(kLinked) != 0 && (c.rd(0xC8CC) & 1u) != 0) {
        cpu.ar[5] = 0x14;
        return c.sleep(1, 0x6D90);
    }
    csResolve(c);
}

/// `0x6D9C`: tira o carrinho do mapa e resolve o percurso final.
void csResolve(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(0xC8DF, 0);
    c.sound(0xD5);
    cpu.ar[4] = c.var(kVarCar);
    cpu.ar[5] = c.rd(cpu.ar[4] + 0x1F);
    for (cpu.r[0] = c.rd(kMapChain);;) {  // 06DA2  desencadeia o carrinho
        cpu.ar[0] = cpu.r[0];
        if (c.rd(cpu.ar[0] + 0x1F) == cpu.ar[4]) {
            c.wr(cpu.ar[0] + 0x1F, cpu.ar[5]);
            break;
        }
        cpu.r[0] = c.rd(cpu.ar[0] + 0x1F);
        if (cpu.r[0] == 0) break;
    }
    cpu.ar[2] = c.var(kVarCar);
    c.call(0x7100);
    if (c.rd(0xE637) == 0) return c.lcall(0xC6F2, 0xC6F3);  // 0x1617 → 0x6DB4
    c.jump(0x6DB4);
}

/// `0x6DB4`: percurso final em `[E634]` e tipo de corrida em `[E636]`.
void csFinalCourse(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kRaceKind) != 1) {
        cpu.r[1] = c.rd(kCourse) == 5 ? 1u : 2u;
        c.wr(kRaceKind, cpu.r[1]);
        if (cpu.r[1] == 1) {
            c.audit(0x99);
            c.audit(0xEE);
            cpu.ar[2] = 0;
        } else {
            cpu.r[0] = c.var(kVarButton);
            c.wr(0xEA30, cpu.r[0]);
            const uint32_t course = c.rd(kCourse);
            c.wr(kPrevCourse, course);
            if (cpu.r[0] == kNone) {
                cpu.ar[2] = course + 0xE9;
                c.call(0x99DA);
                cpu.ar[2] = c.rd(c.rd(0x6FC5) + course);
            } else {
                uint32_t to = 0, auditItem = 0xF6;
                if (cpu.r[0] == 2) to = 0xB, auditItem = 0xF5;
                if (cpu.r[0] == 3) to = 1, auditItem = 0xF4;
                c.audit(auditItem);
                cpu.ar[2] = to;
            }
        }
        c.wr(kCourse, cpu.ar[2]);
    }
    c.lcall(0xC6F4, 0xC6F5);  // 0x6F2A → 0x6DE6
}

/// `0x6F2A`: destaca o marcador escolhido por 31 quadros.
void highlight(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.sound(0x1B9);
    cpu.ar[2] = c.rd(kRaceKind) == 1 ? 5u : c.rd(kPrevCourse);
    cpu.ar[3] = cpu.ar[2];
    c.wr(kChosenObj, findObj(c, cpu.ar[2] + 0x400));
    cpu.ar[2] = c.rd(0xC6FD);
    cpu.r[2] = 0x500;
    c.call(0xA868);
    cpu.ar[5] = 0x1E;
    c.call(0x6F6F);
    c.sleep(1, 0x6F3D);
}

/// `0x6F42`: processo que aproxima o marcador escolhido (x, y × 0,8 por 16 quadros; depois sobe).
void zoomChosen(ProcCtx& c, bool second) {
    Tms32031State& cpu = c.cpu;
    if (!second) {
        c.wf(cpu.ar[4] + 1, c.rf(cpu.ar[4] + 1) * c.k(0x6F45));
        c.wf(cpu.ar[4] + 2, c.rf(cpu.ar[4] + 2) * c.k(0x6F48));
    } else {
        c.wf(cpu.ar[4] + 3, std::max(c.rf(cpu.ar[4] + 3) * c.k(0x6F50), 50.0) + c.rf(cpu.ar[4] + 3));
    }
    c.call(0x6F5A);
    c.sleep(1, second ? 0x6F58 : 0x6F4D);
}

/// `0x6DE6`/`0x6DEB`: limpa a tela e prepara a pista escolhida.
void csFinish(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x6DFB);
    c.call(0x8BE4);
    c.wr(0xE3FE, c.rd(c.rd(kCourse) + c.rd(0x18A5)));
    c.call(0x3F6F);
    cpu.r[0] = 0x5C5;
    cpu.r[1] = kNone;
    c.call(0xA8CF);
    c.wr(0xE481, c.rd(kCourse));
    c.lret();
}

/// Laço de animação `LDI *AR5++,R0 / BLT|CMPI −1 → recomeça / STI R0,*+AR4(D) / dorme n`.
void animate(ProcCtx& c, uint32_t frames, uint32_t resume, uint32_t restart) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(cpu.ar[5]++);
    if (s(cpu.r[0]) < 0) return c.jump(restart);
    c.wr(cpu.ar[4] + 0xD, cpu.r[0]);
    c.sleep(frames, resume);
}

} // namespace

void registerCourseSelect(SubroutineDispatcher& d) {
    using C = ProcCtx;
    // rotinas
    addProc(d, 0x1C62, wheelToSlot);
    addProc(d, 0x1C85, gasEdge);
    addProc(d, 0x1D38, [](C& c) { timerText(c, 253.0); });
    addProc(d, 0x1D3F, [](C& c) { timerText(c, 215.0); });
    addProc(d, 0x1D41, [](C& c) { timerText(c, 350.0); });
    addProc(d, 0xAF13, drawImage);
    addProc(d, 0x8BE4, releaseSelectPalettes);
    addProc(d, 0x6ECD, prepareMap);
    addProc(d, 0x6EB0, dropMapStep);
    addProc(d, 0x6DFE, restoreSpecial);
    addProc(d, 0x6E0D, specialButtons);
    addProc(d, 0x6E4F, showCourse);
    addProc(d, 0x6F93, moveCar);
    addProc(d, 0x6F5A, chosenName);
    addProc(d, 0x6F6F, pushOthers);
    addProc(d, 0x6FDB, [](C& c) {
        c.cpu.ar[2] = c.var(kVarCar);
        c.wr(c.cpu.ar[2] + 0x1B, c.rd(c.cpu.ar[2] + 1));
        c.wf(c.cpu.ar[2] + 1, -6000.0);
        c.cpu.ar[2] = c.rd(0xEA33);
        c.call(0x7070);
    });
    addProc(d, 0x6FE3, [](C& c) {
        c.cpu.ar[2] = c.rd(0xEA33);
        c.call(0x7100);
        c.cpu.ar[2] = c.var(kVarCar);
        c.wr(c.cpu.ar[2] + 1, c.rd(c.cpu.ar[2] + 0x1B));
    });
    // processo
    addProc(d, 0x6C8F, csStart);
    addProc(d, 0x6C9E, csBuild);
    addProc(d, 0x6CA8, csLoadTextures);
    addProc(d, 0x6CB7, csMap);
    addProc(d, 0x6EA2, [](C& c) { c.cpu.ar[4] = 0x14; dropFrame(c); });
    addProc(d, 0x6EA3, dropFrame);
    addProc(d, 0x6EAB, [](C& c) {
        if (s(--c.cpu.ar[4]) >= 0) return dropFrame(c);
        c.lret();
    });
    addProc(d, 0x6CDF, csAfterDrop);
    addProc(d, 0x6CE5, csModemFrame);
    addProc(d, 0x6D1C, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return csModemFrame(c);
        c.sleep(0x46, 0x6D1F);
    });
    addProc(d, 0x6D1F, [](C& c) { c.jump(0x4B28); });
    addProc(d, 0x6D20, csPick);
    addProc(d, 0x6D3C, [](C& c) {
        c.call(0x6FE3);
        if (c.cpu.ar[6] != 1) return c.jump(0x1D59);
        c.audit(0xD3);
        c.call(0x9937);
        c.lcall(0xC6EF, 0xC6F0);
    });
    addProc(d, 0x6D48, csPickLoop);
    addProc(d, 0x6D57, csSettle);
    addProc(d, 0x6D5B, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return csSettle(c);
        c.wr(kStartReq, 0);
        csChoose(c);
    });
    addProc(d, 0x6D5C, [](C& c) {
        c.wr(kStartReq, 0);
        csChoose(c);
    });
    addProc(d, 0x6D5E, csChoose);
    addProc(d, 0x6D83, [](C& c) {
        if (s(c.rd(kTimer)) > 0) return csChoose(c);
        csConfirm(c);
    });
    addProc(d, 0x6D85, csConfirm);
    addProc(d, 0x6D90, [](C& c) {
        if (--c.cpu.ar[5] == 0) return csResolve(c);
        if (c.rd(kLinkCourse) == kNone) return c.sleep(1, 0x6D90);
        c.setVar(kVarButton, c.rd(0xC8DF));
        c.wr(kCourse, c.rd(kLinkCourse));
        c.wr(kRaceKind, c.rd(0xC8E0));
        csResolve(c);
    });
    addProc(d, 0x6DB4, csFinalCourse);
    addProc(d, 0x6DE6, [](C& c) {
        c.wr(0xC940, 0);
        c.call(0x6FEB);
        c.sleep(2, 0x6DEB);
    });
    addProc(d, 0x6DEB, csFinish);
    addProc(d, 0x6F2A, highlight);
    addProc(d, 0x6F3D, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) {
            c.call(0x6F6F);
            return c.sleep(1, 0x6F3D);
        }
        c.lret();
    });
    addProc(d, 0x6F42, [](C& c) {
        c.cpu.ar[4] = c.rd(kChosenObj);
        c.cpu.ar[5] = 0xF;
        zoomChosen(c, false);
    });
    addProc(d, 0x6F4D, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return zoomChosen(c, false);
        c.cpu.ar[5] = 0xF;
        zoomChosen(c, true);
    });
    addProc(d, 0x6F58, [](C& c) {
        if (s(--c.cpu.ar[5]) >= 0) return zoomChosen(c, true);
        c.die();
    });
    // animações dos objetos do mapa
    addProc(d, 0x6F8C, [](C& c) {
        c.cpu.ar[5] = c.rd(0x6F86);
        animate(c, 4, 0x6F92, 0x6F8C);
    });
    addProc(d, 0x6F92, [](C& c) { animate(c, 4, 0x6F92, 0x6F8C); });
    addProc(d, 0x5CC0, [](C& c) {
        c.cpu.ar[5] = c.rd(0x5CB8);
        animate(c, 5, 0x5CC7, 0x5CC0);
    });
    addProc(d, 0x5CC7, [](C& c) { animate(c, 5, 0x5CC7, 0x5CC0); });
    addProc(d, 0x5CD0, [](C& c) {
        c.cpu.ar[5] = c.rd(0x5CC8);
        c.sleep(0x10, 0x5CD3);
    });
    auto random5cd3 = [](C& c) {
        c.cpu.r[0] = c.rd(c.cpu.ar[5]++);
        if (c.cpu.r[0] == kNone) return c.jump(0x5CD0);
        c.wr(c.cpu.ar[4] + 0xD, c.cpu.r[0]);
        c.cpu.r[1] = c.rd(0xC92F);
        c.cpu.r[0] = 6;
        c.call(0xA531);
        c.sleep(c.cpu.r[0], 0x5CDC);
    };
    addProc(d, 0x5CD3, random5cd3);
    addProc(d, 0x5CDC, random5cd3);
}

} // namespace rea::core::game
