/**
 * @file service_menu.cpp
 * @brief Núcleo do menu de serviço: entrada (`0x0669`/`0x0678`/`0x0699`), laço genérico de menu
 *        (`0x0A4F`), molduras (`0x0602`/`0x0628`), esperas de tecla (`0x05F6`–`0x0600`), caixas de
 *        mensagem e de confirmação (`0x0E62`, `0x0E76`–`0x0EC8`), lâmpadas (`0x1159`–`0x116F`) e a
 *        saída pela reentrada `0x4B28`.
 */
#include "core/service/service_menu.hpp"

#include <algorithm>
#include <utility>

#include "core/tms_float.hpp"

#include "core/non_local_jump.hpp"
#include "core/service/service_internal.hpp"

namespace rea::core::service {

namespace {

constexpr uint32_t kMainMenuPtr = 0xC81E;  ///< `[C81E]` = 0x10D8: no menu principal SERVICE não volta
constexpr uint32_t kExitItem = 0x0AE0;     ///< "EXIT TO MAIN MENU": volta do submenu

/// Valores iniciais do contador de repetição de tecla (`MPYI $37` de 10000 / 5000).
constexpr uint32_t kRepeatFirst = 10000u * 55u;
constexpr uint32_t kRepeatNext = 5000u * 55u;  ///< Repetição seguinte (5000 × 55).

/**
 * @brief Laço de contagem com saída por tecla (`0x0AAF`–`0x0ABA`).
 * @details R1 = R7; repete `R1 −= 1` (sai se < 0) e lê `[C8C9] & 0x300` até as duas teclas soltarem.
 * Soltas: R7 = 550 000; esgotado: R7 = 275 000 (repetição mais rápida).
 */
void repeatDelay(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    int64_t r1 = Ctx::s32(cpu.r[7]);
    constexpr uint64_t kPerChunk = cycles::kPoll / cycles::kRepeatLoop;
    for (;;) {
        if (r1 - 1 < 0) {           // 00AB0  SUBI 1,R1; BLT $0AB9
            cpu.r[1] = static_cast<uint32_t>(r1 - 1);
            cpu.r[7] = kRepeatNext;
            return;
        }
        cpu.r[0] = c.rd(vars::kKeys) & 0x0300u;  // 00AB2
        if (cpu.r[0] == 0x0300u) {
            cpu.r[1] = static_cast<uint32_t>(r1 - 1);
            cpu.r[7] = kRepeatFirst;
            return;
        }
        const uint64_t steps = std::min<uint64_t>(kPerChunk, static_cast<uint64_t>(r1));
        r1 -= static_cast<int64_t>(steps);
        c.burn(steps * cycles::kRepeatLoop);
    }
}

/// @brief `0x0AE0`: sai do submenu ([EBDE] = 0, lâmpadas apagadas pela `0x1159`).
void exitMenu(Ctx& c) {
    c.wr(vars::kScrollTop, 0);
    c.cpu.r[0] = 0;
    c.wr(vars::kLampMask, 0);  // 01159 → 0116C
    c.cpu.dp = 0;
}

/// @brief `0x0678`: abandona a pilha e prepara a máquina para o menu (modo 1, interrupções INT0/INT3).
void enterService(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    // 00678  POP R0; SP = [0x63]; PUSH R0 — a pilha anterior é descartada
    cpu.dp = 0;
    cpu.sp = c.rd(0x0063);
    // 0067C  espera o DMA e o blitter ([0x980082] bits 0 e 1)
    while (c.rd(0x980082) & 0x1u) c.burn(cycles::kPoll);
    while (c.rd(0x980082) & 0x2u) c.burn(cycles::kPoll);
    // 00686  CALL $4EE3; $9166; item 0x29 = 1 (0x99F4)
    c.call(0x4EE3);
    c.call(0x9166);
    cpu.r[2] = 1;
    cpu.ar[2] = 0x29;
    c.call(0x99F4);
    // 0068B  modo 1; IE = INT0 | INT3; IF = 0; liga as interrupções (0x4C33)
    c.wr(vars::kGameMode, 1);
    cpu.ie = Tms32031State::kIeInt0 | Tms32031State::kIeInt3;
    cpu.if_ = 0;
    c.call(0x4C33);
    // 00690  mata os processos (0x95CB); fila de paletas (0x9E80); paleta [C7E0]; som 0
    c.call(0x95CB);
    c.call(0x9E80);
    cpu.ar[2] = c.rd(0xC7E0);
    c.call(0x9F2F);
    cpu.ar[2] = 0;
    c.call(0x9211);
    cpu.r[4] = 0;
    c.wr(vars::kPrinterFlag, 0);
}

/// @brief `0x0699`: menu principal.
void mainMenu(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    enterService(c);
    c.wr(vars::kLampMask, 0);
    c.wr(vars::kGameMode, 1);
    cpu.ar[5] = c.rd(0xC7E1);
    cpu.ar[6] = 7;
    cpu.ar[7] = c.rd(0x0A4A);
    menuLoop(c, true);
}

/// @brief `0x0E62`: caixa com a mensagem AR2 por 31 quadros.
void messageBox(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t msg = cpu.ar[2];
    bevelBox(c, 0x6E, 0x73, 0x172, 0x11D, true);
    bevelBox(c, 0x78, 0x7D, 0x168, 0x113, false);
    cpu.rc = 0xF;
    c.text(0x82, 0xAA, msg);
    cpu.ar[6] = 0x1E;
    do {
        c.waitVblank();
    } while (Ctx::s32(--cpu.ar[6]) >= 0);  // 00E74  DBU AR6
}

/**
 * @brief `0x0E7D`: caixa de escolha entre AR4 (R5 = 1) e AR5 (R5 = 0), com as linhas AR2 e AR3.
 * @details VOLUME + alterna (som 0x25B); ENTER confirma. Saída: carry = 1 se R5 == 0.
 *          VOLUME − não faz nada (os dois ramos de `0x0EB8` vão para `0x0EBE`).
 */
void choiceBox(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[5] &= 1u;
    const uint32_t first = cpu.ar[4], second = cpu.ar[5], line2 = cpu.ar[3], line1 = cpu.ar[2];
    c.waitReleased(0x0310u);
    bevelBox(c, 0x6E, 0x73, 0x172, 0x11D, true);
    bevelBox(c, 0x78, 0x7D, 0x168, 0x113, false);
    cpu.rc = 0xF;
    c.text(0x82, 0x8C, line1);
    c.text(0x82, 0x96, line2);
    c.text(0x82, 0xEB, c.rd(0xC888));
    c.text(0x82, 0xF5, c.rd(0xC889));
    c.waitVblank();
    c.waitVblank();
    cpu.ar[7] = second;
    cpu.ar[6] = first;
    for (;;) {
        // 00EA5  opção destacada na cor 3
        cpu.rc = (cpu.r[5] == 1) ? 3u : 0xFu;
        c.text(0xAA, 0xC8, cpu.ar[6]);
        cpu.rc = (cpu.r[5] == 0) ? 3u : 0xFu;
        c.text(0xDC, 0xC8, cpu.ar[7]);
        c.burn(cycles::kPoll);
        cpu.r[0] = c.rd(vars::kKeys);
        if ((cpu.r[0] & keys::kEnter) == 0) break;
        if ((cpu.r[0] & keys::kUp) == 0) {
            cpu.r[5] ^= 1u;
            c.sound2(0x025B);
        }
        c.waitReleased(0x0310u);
    }
    cpu.setFlag(Tms32031State::kStCarry, cpu.r[5] == 0);
}

/**
 * @brief `0x0F5B`: caixa de valor numérico R5 entre R6 e R7, passo R4, texto pelo formatador AR4.
 * @details VOLUME + soma R4 (som 0x3D5), VOLUME − subtrai (som 0x3D4), ENTER aceita. A repetição
 *          usa `[EBE2]` (contador) e `[EBE1]` (última combinação de teclas).
 */
void valueBox(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r5 = cpu.r[5], r6 = cpu.r[6], r7 = cpu.r[7], ar4 = cpu.ar[4], ar5 = cpu.ar[5];
    const uint32_t line2 = cpu.ar[3], line1 = cpu.ar[2];
    c.waitReleased(0x0310u);
    c.wr(0xEBE2, kRepeatFirst);
    c.wr(0xEBE1, 0);
    bevelBox(c, 0x6E, 0x73, 0x140, 0x11D, true);
    bevelBox(c, 0x78, 0x7D, 0x136, 0x113, false);
    cpu.rc = 0xF;
    c.text(0x82, 0x8C, line1);
    c.text(0x82, 0x96, line2);
    c.text(0x82, 0xE1, c.rd(0xC88A));
    c.text(0x82, 0xEB, c.rd(0xC88B));
    c.text(0x82, 0xF5, c.rd(0xC88C));
    c.waitVblank();
    c.waitVblank();
    cpu.ar[7] = ar5;
    cpu.ar[6] = ar4;
    cpu.r[7] = r7;
    cpu.r[6] = r6;
    cpu.r[5] = r5;
    for (;;) {
        // 00F91  limita R5 a [R6, R7]
        if (Ctx::s32(cpu.r[5]) < Ctx::s32(cpu.r[6])) cpu.r[5] = cpu.r[6];
        if (Ctx::s32(cpu.r[5]) > Ctx::s32(cpu.r[7])) cpu.r[5] = cpu.r[7];
        c.shape(0xA7E5, 0xAA, 0xC8, 0x135, 0xD1, 0);
        cpu.r[0] = cpu.ar[4];
        c.call(cpu.r[0]);  // formatador: AR2 = texto
        cpu.rc = 0xF;
        cpu.r[2] = 0xAA;
        cpu.r[3] = 0xC8;
        c.call(0xA7F4);
        c.waitVblank();
        cpu.r[0] = c.rd(vars::kKeys);
        if ((cpu.r[0] & keys::kEnter) == 0) return;
        if ((cpu.r[0] & keys::kUp) == 0) {
            cpu.r[5] += cpu.r[4];
            c.sound(0x03D5);
        } else if ((cpu.r[0] & keys::kDown) == 0) {
            cpu.r[5] -= cpu.r[4];
            c.sound(0x03D4);
        }
        // 00FB0  R1 = [EBE2]; conta até as teclas mudarem
        int64_t r1 = Ctx::s32(c.rd(0xEBE2));
        const uint32_t last = c.rd(0xEBE1);
        constexpr uint64_t kPerChunk = cycles::kPoll / cycles::kRepeatLoop;
        for (;;) {
            if (r1 - 1 < 0) break;
            cpu.r[0] = c.rd(vars::kKeys) & 0x0310u;
            if (cpu.r[0] != last) break;
            const uint64_t steps = std::min<uint64_t>(kPerChunk, static_cast<uint64_t>(r1));
            r1 -= static_cast<int64_t>(steps);
            c.burn(steps * cycles::kRepeatLoop);
        }
        cpu.r[1] = static_cast<uint32_t>(r1 - 1);
        if (cpu.r[0] == last) {
            c.wr(0xEBE2, kRepeatNext);  // 00FB9  mesma combinação: repete mais rápido
        } else {
            c.wr(0xEBE2, kRepeatFirst);
            c.wr(0xEBE1, cpu.r[0]);
        }
    }
}

/// @brief `0x105B`: callback de item: R0 = valor do item `[AR4 + 2·R0]`, e chama `[AR4 + 2·R0 + 1]`.
void itemValue(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], ar2 = cpu.ar[2];
    cpu.r[0] = tmsMpyi(cpu.r[0], 2);
    cpu.ar[4] += cpu.r[0];
    cpu.ar[2] = c.rd(cpu.ar[4]);
    c.call(0x99E9);
    cpu.r[1] = c.rd(cpu.ar[4] + 1);
    if (cpu.r[1] != 0) c.call(cpu.r[1]);
    cpu.ar[2] = ar2;
    cpu.r[2] = r2;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}

/// @brief `0x10A6`: R0 em decimal alinhado à direita em x = 0x10E (RC e R3 do chamador).
void rightNumber(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t rc = cpu.rc;
    cpu.r[2] = cpu.r[0];
    cpu.ar[2] = c.rd(vars::kNumberBuf);
    c.call(0xA762);
    cpu.ar[2] = c.rd(vars::kNumberBuf);
    // 010AB  a coluna usa o R0 devolvido pela 0xA762 (bits da última palavra), como no original
    cpu.r[2] = 0x10E;
    const int32_t r0 = Ctx::s32(cpu.r[0]);
    if (r0 >= 10) cpu.r[2] = 0x106;
    if (r0 >= 100) cpu.r[2] = 0xFE;
    if (r0 >= 1000) cpu.r[2] = 0xF6;
    if (r0 >= 10000) cpu.r[2] = 0xEE;
    if (r0 >= 100000) cpu.r[2] = 0xE6;
    cpu.rc = rc;
    c.call(0xA7F4);
}

/// @brief `0x107B`: R2 = comprimento do texto AR2 (preserva AR2, R0, R1 e R3).
void textLength(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    uint32_t a = cpu.ar[2];
    uint32_t n = 0;
    for (;;) {
        const uint32_t w = c.rd(a++);
        bool end = false;
        for (int sh = 0; sh < 32; sh += 8) {
            if (((w >> sh) & 0xFFu) == 0) {
                end = true;
                break;
            }
            ++n;
        }
        if (end) break;
    }
    cpu.r[2] = n;
}

} // namespace

void clearScreen(Ctx& c) {
    // 00AE9  CALL $8E79; CALL $8E4E; [CE13] = 0x900000
    c.call(0x8E79);
    c.call(0x8E4E);
    c.cpu.r[0] = 0x900000;
    c.wr(vars::kDrawPage, c.cpu.r[0]);
}

void bevelBox(Ctx& c, uint32_t ar2, uint32_t r2, uint32_t r3, uint32_t rc, bool raised) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar3 = cpu.ar[3];
    const uint32_t light = raised ? 0xDu : 0xBu, dark = raised ? 0xBu : 0xDu;
    c.shape(0xA7E5, ar2, r2, r3, rc, 0xC);   // fundo
    c.shape(0xAC23, ar2, r2, r3, r2, light);  // topo
    c.shape(0xAC23, ar2, r2, ar2, rc, light); // esquerda
    c.shape(0xAC23, r3, r2, r3, rc, dark);    // direita
    c.shape(0xAC23, ar2, rc, r3, rc, dark);   // base
    cpu.rc = rc;
    cpu.r[3] = r3;
    cpu.r[2] = r2;
    cpu.ar[2] = ar2;
    cpu.ar[3] = ar3;
}

void menuLoop(Ctx& c, bool resetCursor) {
    Tms32031State& cpu = c.cpu;
    if (resetCursor) {
        // 00A4F  [EBDE] = 0; R6 = 0
        cpu.r[0] = 0;
        c.wr(vars::kScrollTop, 0);
        cpu.r[6] = 0;
    }
    for (;;) {
        // 00A52  cabeçalho
        const uint32_t saved_r6 = cpu.r[6];
        c.wr(vars::kBackColor, c.rd(0x064F));
        const uint32_t saved_ar7 = cpu.ar[7];
        clearScreen(c);
        cpu.rc = 0xF;
        c.text(0x1E, 0x14, c.rd(0xC81B));
        c.text(0x14, 0x104, c.rd(0xC81C));
        c.text(0x14, 0x10E, c.rd(0xC81D));
        cpu.r[2] = c.rd(cpu.ar[5] - 1);
        if (cpu.r[2] != 0 && (cpu.r[2] = c.rd(cpu.ar[5] - 2)) != 0) {
            c.call(cpu.r[2]);  // cabeçalho próprio do menu
        } else {
            bevelBox(c, 0x14, 0x2D, 0x118, 0xFA, false);
        }
        // 00A6F  título AR7
        cpu.r[2] = 0x1E;
        cpu.r[3] = 0x23;
        cpu.rc = 0xF;
        cpu.ar[7] = saved_ar7;
        cpu.ar[2] = cpu.ar[7];
        c.call(0xA7F4);
        cpu.r[7] = kRepeatFirst;
        cpu.r[6] = saved_r6;
        bool redraw = false;
        while (!redraw) {
            // 00A78  itens visíveis: até 20 a partir de [EBDE]; o cursor (R6) na cor 3
            cpu.r[0] = c.rd(vars::kScrollTop);
            cpu.ar[4] = tmsMpyi(2, cpu.r[0]) + cpu.ar[5];
            cpu.r[3] = 0x32;
            const uint32_t ar7 = cpu.ar[7];
            cpu.ar[7] = cpu.ar[6] - c.rd(vars::kScrollTop);
            if (Ctx::s32(cpu.ar[7]) > 0x14) cpu.ar[7] = 0x14;
            cpu.ar[7] -= 1;
            for (;;) {
                cpu.rc = (cpu.r[6] == cpu.r[0]) ? 3u : 0xFu;
                cpu.ar[2] = c.rd(cpu.ar[4]);
                cpu.ar[4] += 2;
                cpu.r[2] = 0x1E;
                c.call(0xA7F4);
                cpu.r[5] = c.rd(cpu.ar[5] - 1);
                if (cpu.r[5] != 0) c.call(cpu.r[5]);  // 00A8A  CALLNE R5: valor do item
                cpu.r[3] += 0xA;
                cpu.r[0] += 1;
                if (Ctx::s32(--cpu.ar[7]) < 0) break;  // DBU AR7
            }
            cpu.ar[7] = ar7;
            // 00A8F  teclas
            cpu.r[0] = c.rd(vars::kKeys);
            cpu.r[1] = cpu.r[0];
            if ((cpu.r[1] & keys::kService) == 0 && cpu.ar[5] != c.rd(kMainMenuPtr)) {
                c.sound(0x03D6);  // SERVICE: volta ao menu anterior
                exitMenu(c);
                return;
            }
            if ((cpu.r[1] & keys::kEnter) == 0) {
                // 00ACF  ENTER: executa o item
                c.sound(0x03D6);
                cpu.ar[0] = tmsMpyi(cpu.r[6], 2) + cpu.ar[5];
                cpu.r[0] = c.rd(cpu.ar[0] + 1);
                if (cpu.r[0] == 0) continue;
                if (cpu.r[0] == kExitItem) {
                    exitMenu(c);
                    return;
                }
                const uint32_t ar5 = cpu.ar[5], ar6 = cpu.ar[6], ar7s = cpu.ar[7], r6 = cpu.r[6];
                c.call(cpu.r[0]);
                cpu.r[6] = r6;
                cpu.ar[7] = ar7s;
                cpu.ar[6] = ar6;
                cpu.ar[5] = ar5;
                redraw = true;
                continue;
            }
            bool moved = false;
            if ((cpu.r[0] & keys::kDown) == 0) {
                c.sound(0x03D4);
                cpu.r[6] += 1;
                if (!(Ctx::s32(cpu.r[6]) < Ctx::s32(cpu.ar[6]))) cpu.r[6] = cpu.ar[6] - 1;
                moved = true;
            } else if ((cpu.r[0] & keys::kUp) == 0) {
                c.sound(0x03D5);
                cpu.r[6] -= 1;
                if (Ctx::s32(cpu.r[6]) < 0) cpu.r[6] = 0;
                moved = true;
            }
            if (moved) repeatDelay(c);
            // 00ABB  rolagem de 10 em 10 itens
            if (cpu.r[6] == 0) continue;
            cpu.r[2] = 0;
            cpu.r[0] = c.rd(vars::kScrollTop);
            cpu.r[1] = cpu.r[0] + 0x14;
            const int32_t r6 = Ctx::s32(cpu.r[6]);
            if (!(r6 < Ctx::s32(cpu.r[0])) && Ctx::s32(cpu.ar[6]) <= Ctx::s32(cpu.r[1])) continue;
            if (r6 < Ctx::s32(cpu.r[0])) cpu.r[2] = static_cast<uint32_t>(-0xA);
            if (r6 >= Ctx::s32(cpu.r[1])) cpu.r[2] = 0xA;
            cpu.r[0] += cpu.r[2];
            if (Ctx::s32(cpu.r[0]) < 0) cpu.r[0] = 0;
            c.wr(vars::kScrollTop, cpu.r[0]);
            if (cpu.r[2] != 0) redraw = true;
        }
    }
}

/// Registra o núcleo do menu: esperas da tecla ENTER, navegação e a entrada pelo RESET.
void registerMenuCore(SubroutineDispatcher& d, IBusyWait& wait) {
    addSvc(d, wait, 0x05F6, [](Ctx& c) { c.waitEnterDown(); });
    addSvc(d, wait, 0x05FA, [](Ctx& c) { c.waitEnterUp(); });
    addSvc(d, wait, 0x05FE, [](Ctx& c) {
        c.waitEnterDown();
        c.waitEnterUp();
    });
    addSvc(d, wait, 0x0600, [](Ctx& c) { c.sound(0x03D4); });
    addSvc(d, wait, 0x0602, [](Ctx& c) {
        bevelBox(c, c.cpu.ar[2], c.cpu.r[2], c.cpu.r[3], c.cpu.rc, true);
    });
    addSvc(d, wait, 0x0628, [](Ctx& c) {
        bevelBox(c, c.cpu.ar[2], c.cpu.r[2], c.cpu.r[3], c.cpu.rc, false);
    });
    // 00669  auditorias pela sequência secreta (0x4FAE): sem a pilha, modo 1, [EBBB] = 1; depois 0x4B28
    addSvc(d, wait, 0x0669, [](Ctx& c) {
        enterService(c);
        c.wr(vars::kBackColor, c.rd(0x064F));
        c.wr(vars::kGameMode, 1);
        c.call(0x8E79);
        c.call(0x8E4E);
        c.wr(vars::kDrawPage, 0x900000);
        c.wr(vars::kPrinterFlag, 1);
        c.call(0x14D0);
        throw NonLocalJump{0x4B28};
    });
    addSvc(d, wait, 0x0678, enterService);
    addSvc(d, wait, 0x0699, mainMenu);
    // 006A2  volta ao menu principal mantendo o cursor (BU $0A52)
    addSvc(d, wait, 0x06A2, [](Ctx& c) {
        c.wr(vars::kGameMode, 1);
        c.cpu.ar[5] = c.rd(0xC7E2);
        c.cpu.ar[6] = 7;
        c.cpu.ar[7] = c.rd(0x0A4A);
        menuLoop(c, false);
    });
    addSvc(d, wait, 0x06A8, [](Ctx& c) { c.textAt(0xB4, 0x173, 0xC7E3, 0xF); });  // HIT ENTER TO EXIT
    // Submenus: AR5 = tabela, AR6 = itens, AR7 = título
    struct Sub { uint32_t addr, table, count, title; };
    static constexpr Sub kSubs[] = {
        {0x0A36, 0xC817, 7, 0x0A35},   // MOTION MENU
        {0x0A3B, 0xC818, 18, 0x0A3A},  // ADJUSTMENTS
        {0x0A46, 0xC819, 6, 0x0A45},   // UTILITIES
        {0x0A4C, 0xC81A, 10, 0x0A4B},  // DIAGNOSTICS
        {0x0FC4, 0xC88D, 16, 0x0FC3},  // STANDARD PRICING
        {0x0FE3, 0xC894, 16, 0x0FE2},  // CUSTOM PRICING
        {0x1155, 0xC8A1, 11, 0x113D},  // LAMP TEST
    };
    for (const Sub& s : kSubs) {
        addSvc(d, wait, s.addr, [s](Ctx& c) {
            c.cpu.ar[5] = c.rd(s.table);
            c.cpu.ar[6] = s.count;
            c.cpu.ar[7] = c.rd(s.title);
            menuLoop(c, true);
        });
    }
    addSvc(d, wait, 0x0AE9, clearScreen);
    addSvc(d, wait, 0x0E62, messageBox);
    // 00E76 / 00E77 / 00E7A: escolhas [0E4E]/[0E4F] (R5 = 0 em 0x0E76) ou [0E50]/[0E51]
    addSvc(d, wait, 0x0E76, [](Ctx& c) {
        c.cpu.r[5] = 0;
        c.cpu.ar[4] = c.rd(0x0E4E);
        c.cpu.ar[5] = c.rd(0x0E4F);
        choiceBox(c);
    });
    addSvc(d, wait, 0x0E77, [](Ctx& c) {
        c.cpu.ar[4] = c.rd(0x0E4E);
        c.cpu.ar[5] = c.rd(0x0E4F);
        choiceBox(c);
    });
    addSvc(d, wait, 0x0E7A, [](Ctx& c) {
        c.cpu.ar[4] = c.rd(0x0E50);
        c.cpu.ar[5] = c.rd(0x0E51);
        choiceBox(c);
    });
    addSvc(d, wait, 0x0E7D, choiceBox);
    addSvc(d, wait, 0x0F5A, [](Ctx& c) {
        c.cpu.r[4] = 1;
        valueBox(c);
    });
    addSvc(d, wait, 0x0F5B, valueBox);
    addSvc(d, wait, 0x105B, itemValue);
    // 00A40 / 01056: callbacks de item com as tabelas [0A3F] (ajustes) e [1055] (preço personalizado)
    for (const auto& [addr, table] : {std::pair<uint32_t, uint32_t>{0x0A40, 0x0A3F}, {0x1056, 0x1055}}) {
        addSvc(d, wait, addr, [table = table](Ctx& c) {
            const uint32_t ar4 = c.cpu.ar[4];
            c.cpu.ar[4] = c.rd(table);
            itemValue(c);
            c.cpu.ar[4] = ar4;
        });
    }
    addSvc(d, wait, 0x107B, textLength);
    // 01090  nome do preço padrão (item 0x17 != 0) alinhado à direita em 0x116
    addSvc(d, wait, 0x1090, [](Ctx& c) {
        Tms32031State& cpu = c.cpu;
        const uint32_t value = cpu.r[0];
        cpu.ar[2] = 0x17;
        c.call(0x99E9);
        cpu.r[2] = value;
        if (cpu.r[0] == 0) return;
        cpu.r[2] += c.rd(0xB084);
        cpu.ar[2] = c.rd(cpu.r[2]);
        textLength(c);
        cpu.r[2] = tmsMpyi(cpu.r[2], static_cast<uint32_t>(-8)) + 0x116;
        c.call(0xA7F4);
    });
    // 0109E  "ON" se R0 != 1
    addSvc(d, wait, 0x109E, [](Ctx& c) {
        if (c.cpu.r[0] == 1) return;
        c.cpu.ar[2] = c.rd(0xC800);
        c.cpu.r[2] = 0x106;
        c.call(0xA7F4);
    });
    addSvc(d, wait, 0x10A4, [](Ctx& c) {
        c.cpu.r[0] = tmsMpyi(c.cpu.r[0], 5) + 0x3C;
        rightNumber(c);
    });
    addSvc(d, wait, 0x10A6, rightNumber);
    // 0106B  item 0x20 (volume mínimo, 0–255) mostrado em 0–30: FIX(R0 · 0.117645)
    addSvc(d, wait, 0x106B, [](Ctx& c) {
        Tms32031State& cpu = c.cpu;
        const uint32_t rc = cpu.rc;
        cpu.r[0] = tmsFix(static_cast<double>(Ctx::s32(cpu.r[0])) * shortFloat(static_cast<uint16_t>(c.rd(0x106D))));
        cpu.r[2] = cpu.r[0];
        cpu.ar[2] = c.rd(vars::kNumberBuf);
        c.call(0xA762);
        cpu.ar[2] = c.rd(vars::kNumberBuf);
        cpu.r[2] = 0x10E;
        if (Ctx::s32(cpu.r[0]) >= 10) cpu.r[2] = 0x106;
        if (Ctx::s32(cpu.r[0]) >= 100) cpu.r[2] = 0xFE;
        cpu.rc = rc;
        c.call(0xA7F4);
    });
    // 010BD / 010C6 / 010CF: R0 = 1 − R0 e textos de sim/não, ON/OFF e [0E3F]/[0E40]
    auto yesNo = [](Ctx& c) {
        c.cpu.ar[2] = c.rd(0x0E4E);
        c.cpu.r[2] = 0xFE;
        if (c.cpu.r[0] == 0) {
            c.cpu.ar[2] = c.rd(0x0E4F);
            c.cpu.r[2] = 0x106;
        }
        c.call(0xA7F4);
    };
    auto onOff = [](Ctx& c) {
        c.cpu.ar[2] = c.rd(0xC800);
        c.cpu.r[2] = 0x106;
        if (c.cpu.r[0] == 0) {
            c.cpu.ar[2] = c.rd(0xC801);
            c.cpu.r[2] = 0xFE;
        }
        c.call(0xA7F4);
    };
    auto twoWay = [](Ctx& c) {
        c.cpu.ar[2] = c.rd(0x0E40);
        c.cpu.r[2] = 0xFE;
        if (c.cpu.r[0] == 1) c.cpu.ar[2] = c.rd(0x0E3F);
        c.call(0xA7F4);
    };
    addSvc(d, wait, 0x10BD, [yesNo](Ctx& c) {
        c.cpu.r[0] = 1u - c.cpu.r[0];
        yesNo(c);
    });
    addSvc(d, wait, 0x10BF, yesNo);
    addSvc(d, wait, 0x10C6, [onOff](Ctx& c) {
        c.cpu.r[0] = 1u - c.cpu.r[0];
        onOff(c);
    });
    addSvc(d, wait, 0x10C8, onOff);
    addSvc(d, wait, 0x10CF, [twoWay](Ctx& c) {
        c.cpu.r[0] = 1u - c.cpu.r[0];
        twoWay(c);
    });
    addSvc(d, wait, 0x10D1, twoWay);
    // 01159…0116B: lâmpadas [C8CD] = 0, 0xFF, 1, 2, 4, …, 0x80
    static constexpr std::pair<uint32_t, uint32_t> kLamps[] = {
        {0x1159, 0x00}, {0x115B, 0xFF}, {0x115D, 0x01}, {0x115F, 0x02}, {0x1161, 0x04}, {0x1163, 0x08},
        {0x1165, 0x10}, {0x1167, 0x20}, {0x1169, 0x40}, {0x116B, 0x80}};
    for (const auto& [addr, mask] : kLamps) {
        addSvc(d, wait, addr, [mask = mask](Ctx& c) {
            c.cpu.r[0] = mask;
            c.cpu.dp = 0;
            c.wr(vars::kLampMask, mask);
        });
    }
    // 04B28  reentrada do RESET ("EXIT TO GAME OVER"): desvio não local
    addSvc(d, wait, 0x4B28, [](Ctx&) { throw NonLocalJump{0x4B28}; });
}

void ServiceMenu::registerIn(SubroutineDispatcher& d) {
    registerMenuCore(d, wait_);
    registerDiagnostics(d, wait_);
    registerAdjustments(d, wait_);
    registerAudits(d, wait_);
    registerMotion(d, wait_);
    registerCalibration(d, wait_);
}

} // namespace rea::core::service
