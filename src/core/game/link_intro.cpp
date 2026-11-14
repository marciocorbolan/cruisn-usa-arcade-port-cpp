/**
 * @file link_intro.cpp
 * @brief Tradução de `0x154D`–`0x1616` (abertura do jogo em link, caso −7 da `0x9503`) e de `0xA1EA`/`0xA1F0`
 *        (caso −6: final mostrado pela DIP de teste).
 */
#include "core/game/link_intro.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// `DBU ARn`: decremento de 24 bits; desvia se o resultado não for negativo.
inline bool decrementAr(uint32_t& ar) {
    const uint32_t res = (ar - 1u) & 0x00FFFFFFu;
    ar = (ar & 0xFF000000u) | res;
    return (res & 0x00800000u) == 0;
}

/// Converte a palavra @p v em float TMS.
double reg(uint32_t v) { return tmsToDouble(v); }

/// `0x15D4`: carry ligado (BLO) quando o link caiu: sem link (`[C8D1]` = 0), modo `[C8D6]&F` = 2, ou
/// `[C8DD]` = 0 fora dos modos 5..7.
bool linkLost(ProcCtx& c) {
    c.cpu.r[0] = c.rd(0xC8D1);
    bool lost = true;
    if (c.cpu.r[0] != 0) {
        c.cpu.r[0] = c.rd(0xC8D6) & 0xFu;
        if (c.cpu.r[0] == 5 || c.cpu.r[0] == 6 || c.cpu.r[0] == 7) {
            lost = false;
        } else if (c.cpu.r[0] != 2) {
            c.cpu.r[0] = c.rd(0xC8DD);
            lost = c.cpu.r[0] == 0;
        }
    }
    if (lost) c.cpu.st |= Tms32031State::kStCarry;
    else c.cpu.st &= ~Tms32031State::kStCarry;
    return lost;
}

/// `0x7660` + `0x76BF` com o texto @p ptr em (256, 100) e flag 0x100.
void textAt(ProcCtx& c, uint32_t ptr) {
    c.cpu.ar[2] = c.rd(ptr);
    c.cpu.r[2] = doubleToTms(256.0);
    c.cpu.r[3] = doubleToTms(100.0);
    c.cpu.rc = 1;
    c.call(0x7660);
    c.call(0x76BF);
    c.orField(c.cpu.ar[0], 8, 0x100);
}

/// `0x15E6`: com AR6 = 1, o texto `[B1D7]`.
void text15E6(ProcCtx& c) {
    if (c.cpu.ar[6] == 1) textAt(c, 0xB1D7);
}

/// `0x15D0`: apaga (`0x793A`), `[C8E8]` = −2 e reinicia (`0x1E17`).
void abortIntro(ProcCtx& c) {
    c.call(0x793A);
    c.cpu.r[0] = 0xFFFFFFFEu;
    c.wr(0xC8E8, c.cpu.r[0]);
    c.jump(0x1E17);
}

/// `0x1559`…: objeto da tabela @p ptr na origem, flags 0x4A3, encadeado, paleta `[ptr+1]`; vai para `*+AR7(n)`.
void makePiece(ProcCtx& c, uint32_t ptr, uint32_t n) {
    c.cpu.ar[2] = c.rd(ptr);
    c.cpu.r[2] = 0;
    c.cpu.r[3] = 0;
    c.cpu.rc = 0x170;
    c.call(0x7309);
    c.orField(c.cpu.ar[0], 0x0E, 0x4A3);
    c.cpu.ar[2] = c.cpu.ar[0];
    c.call(0x7070);
    c.cpu.ar[2] = c.rd(ptr + 1);
    c.call(0x9EDB);
    c.wr(c.cpu.ar[0] + 0x10, c.cpu.r[0]);
    c.setVar(n, c.cpu.ar[0]);
}

/// `0x1582`: as peças se aproximam (R6 −4 por quadro, R7 → `[B1CF]` em 10 %).
void approach(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(c.var(0x0D) + 2, cpu.r[6]);
    c.wf(c.var(0x0C) + 2, -reg(cpu.r[6]));
    c.wr(c.var(0x0E) + 3, cpu.r[7]);
    cpu.r[6] = doubleToTms(reg(cpu.r[6]) - 4.0);
    cpu.r[0] = c.rd(0xB1CF);
    cpu.r[1] = doubleToTms((reg(cpu.r[7]) - reg(cpu.r[0])) * c.k(0x158C));  // SUBF3 R7,R0,R1 = R7 − R0
    cpu.r[7] = doubleToTms(reg(cpu.r[7]) - reg(cpu.r[1]));
    text15E6(c);
    if (linkLost(c)) return abortIntro(c);
    c.sleep(1, 0x1593);
}

/// `0x154F` (AR6 = 0) / `0x154D` (AR6 = 1).
void introEntry(ProcCtx& c, uint32_t ar6) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[6] = ar6;
    cpu.r[0] = 2;
    c.wr(0xC931, cpu.r[0]);
    cpu.r[0] = c.rd(0xC8C5) & 0xFu;
    c.wr(0xC8C5, cpu.r[0]);
    makePiece(c, 0xB1C9, 0x0C);
    makePiece(c, 0xB1CB, 0x0D);
    makePiece(c, 0xB1CD, 0x0E);
    cpu.r[6] = doubleToTms(160.0);
    cpu.r[7] = doubleToTms(4000.0);
    cpu.ar[5] = 0x19;
    approach(c);
}

/// `0x1594`: som 0x36 (se o item 0x1E da CMOS = 1) e a primeira batida (`0x15F2` na peça `*+AR7(C)`).
void afterApproach(ProcCtx& c) {
    if (c.item(0x1E) == 1) {
        c.cpu.ar[2] = 0x36;
        c.call(0x91E0);
    }
    c.cpu.ar[4] = c.var(0x0C);
    c.cpu.r[6] = doubleToTms(120.0);
    c.lcall(0xB1D0, 0xB1D1);
}

/// `0x15C1`: laço de espera; mostra o texto enquanto `[E56C]` ≤ 0.
void loop15C1(ProcCtx& c) {
    if (s(c.rd(0xE56C)) <= 0) textAt(c, 0xB1D6);
    c.sleep(1, 0x15CE);
}

// ---- 0x15F2: batida da peça AR4 (sobe R6 em 2 quadros, treme 5, desce em 2) ----

/// Trecho traduzido da ROM em `0x15F3`.
void hitDown(ProcCtx& c) {
    c.wf(c.cpu.ar[4] + 3, c.rf(c.cpu.ar[4] + 3) - reg(c.cpu.r[6]));
    text15E6(c);
    if (linkLost(c)) return c.lret();
    c.sleep(1, 0x15FB);
}

/// Trecho traduzido da ROM em `0x1603`.
void hitHold(ProcCtx& c) {
    text15E6(c);
    if (linkLost(c)) return c.lret();
    c.sleep(1, 0x1608);
}

/// Trecho traduzido da ROM em `0x160A`.
void hitUp(ProcCtx& c) {
    c.wf(c.cpu.ar[4] + 3, c.rf(c.cpu.ar[4] + 3) + reg(c.cpu.r[6]));
    text15E6(c);
    if (linkLost(c)) return c.lret();
    c.sleep(1, 0x1612);
}

} // namespace

void registerLinkIntro(SubroutineDispatcher& d) {
    addProc(d, 0x154D, [](ProcCtx& c) { introEntry(c, 1); });
    addProc(d, 0x154F, [](ProcCtx& c) { introEntry(c, 0); });
    addProc(d, 0x1593, [](ProcCtx& c) {
        if (decrementAr(c.cpu.ar[5])) return approach(c);
        afterApproach(c);
    });
    addProc(d, 0x15A2, [](ProcCtx& c) {  // volta da 1ª batida
        if (linkLost(c)) return abortIntro(c);
        c.cpu.ar[4] = c.var(0x0E);
        c.cpu.r[6] = doubleToTms(300.0);
        c.lcall(0xB1D2, 0xB1D3);
    });
    addProc(d, 0x15AC, [](ProcCtx& c) {  // volta da 2ª batida
        if (linkLost(c)) return abortIntro(c);
        c.cpu.ar[4] = c.var(0x0D);
        c.cpu.r[6] = doubleToTms(120.0);
        c.lcall(0xB1D4, 0xB1D5);
    });
    addProc(d, 0x15B6, [](ProcCtx& c) {  // volta da 3ª batida
        if (linkLost(c)) return abortIntro(c);
        if (c.cpu.ar[6] == 1) {
            c.cpu.r[0] = 1;
            c.wr(0xC931, c.cpu.r[0]);
            return loop15C1(c);
        }
        if (linkLost(c)) return abortIntro(c);
        c.sleep(0x1E, 0x15BE);
    });
    addProc(d, 0x15BE, [](ProcCtx& c) { c.jump(0x1E1C); });
    addProc(d, 0x15C1, loop15C1);
    addProc(d, 0x15CE, [](ProcCtx& c) {
        if (!linkLost(c)) return loop15C1(c);
        abortIntro(c);
    });
    addProc(d, 0x15D0, abortIntro);
    addProc(d, 0x15D4, [](ProcCtx& c) { linkLost(c); });
    addProc(d, 0x15E6, text15E6);

    addProc(d, 0x15F2, [](ProcCtx& c) {
        c.cpu.ar[5] = 1;
        hitDown(c);
    });
    addProc(d, 0x15F3, hitDown);
    addProc(d, 0x15FB, [](ProcCtx& c) {
        if (decrementAr(c.cpu.ar[5])) return hitDown(c);
        if (c.item(0x1E) == 1) {
            c.cpu.ar[2] = 0x36;
            c.call(0x91E0);
        }
        c.cpu.ar[5] = 4;
        hitHold(c);
    });
    addProc(d, 0x1603, hitHold);
    addProc(d, 0x1608, [](ProcCtx& c) {
        if (decrementAr(c.cpu.ar[5])) return hitHold(c);
        c.cpu.ar[5] = 1;
        hitUp(c);
    });
    addProc(d, 0x160A, hitUp);
    addProc(d, 0x1612, [](ProcCtx& c) {
        if (decrementAr(c.cpu.ar[5])) return hitUp(c);
        c.lret();
    });
    addProc(d, 0x1613, [](ProcCtx& c) { c.lret(); });

    // 0A1EA  caso −6: o final (`0xA1F1`) como chamada local que volta em 0xA1F0 (BR $1E1C)
    addProc(d, 0xA1EA, [](ProcCtx& c) { c.lcall(0xB146, 0xB147); });
    addProc(d, 0xA1F0, [](ProcCtx& c) { c.jump(0x1E1C); });
}

} // namespace rea::core::game
