/**
 * @file object_effects.cpp
 * @brief Tradução de `0x677E`–`0x67AE`, `0x6822`–`0x685B` e `0x685D`–`0x68E4`.
 */
#include "core/game/object_effects.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// `LDF n,R0 / CALL $8ED1` → aleatório em [−n, +n).
double randSpread(ProcCtx& c, double n) {
    c.cpu.r[0] = doubleToTms(n);
    c.call(0x8ED1);
    return tmsToDouble(c.cpu.r[0]);
}
/// `LDI n,AR2 / CALL $8EDB / MPYI 3,R0 / FLOAT R0,R0` → 3·(0..n−1).
double randStep3(ProcCtx& c, uint32_t n) {
    c.cpu.ar[2] = n;
    c.call(0x8EDB);
    return static_cast<double>(s(c.cpu.r[0]) * 3);
}

/// `LDI [lim],R0 / CMPI *+AR4(1C),R0 / BLT` → objeto mais longe que o limite.
bool tooFar(ProcCtx& c, uint32_t limAddr) { return s(c.rd(limAddr)) < s(c.rd(c.cpu.ar[4] + 0x1C)); }

/// Solta o objeto AR4 (`0x7100`) e encerra o processo.
void freeAndDie(ProcCtx& c) {
    c.cpu.ar[2] = c.cpu.ar[4];
    c.call(0x7100);
    c.die();
}

/// Decrementa o contador @p counter e solta o objeto.
void decAndFree(ProcCtx& c, uint32_t counter) {
    c.cpu.r[0] = c.rd(counter) - 1;
    c.wr(counter, c.cpu.r[0]);
    freeAndDie(c);
}

/// Copia a posição de @p from para o objeto AR4.
void copyPos(ProcCtx& c, uint32_t from) {
    for (uint32_t i = 1; i <= 3; ++i) c.wr(c.cpu.ar[4] + i, c.rd(from + i));
}

/// `0x7025` → AR4 = novo objeto (AR6 = origem). Devolve false (e morre) sem objeto livre.
bool allocFrom(ProcCtx& c) {
    c.cpu.ar[6] = c.cpu.ar[4];
    c.call(0x7025);
    if (c.cpu.carry()) {
        c.die();
        return false;
    }
    c.cpu.ar[4] = c.cpu.ar[0];
    return true;
}

// --- 0x677E: nuvem que sobe --------------------------------------------------------------------

/// Laço da baforada: move o objeto até ficar longe demais.
void puffLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (tooFar(c, 0xB12E)) return freeAndDie(c);
    c.wf(cpu.ar[4] + 1, c.rf(cpu.ar[4] + 1) + tmsToDouble(cpu.r[6]));
    c.wf(cpu.ar[4] + 3, c.rf(cpu.ar[4] + 3) + tmsToDouble(cpu.r[7]));
    cpu.ar[0] = c.rd(cpu.ar[5]++);
    if (cpu.ar[0] == 0xFFFFFFFFu) return freeAndDie(c);
    c.wr(cpu.ar[4] + 0xD, cpu.ar[0]);
    c.wf(cpu.ar[4] + 2, c.rf(cpu.ar[4] + 2) - 10.0);
    c.sleep(1, 0x67AB);
}

/// Começa uma baforada (objeto da lista `[67AF]`).
void puffStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (tooFar(c, 0xB12D)) return c.die();
    if (!allocFrom(c)) return;
    cpu.ar[5] = c.rd(0x67AF);
    c.wr(cpu.ar[4] + 0xD, c.rd(cpu.ar[5]));
    copyPos(c, cpu.ar[6]);
    c.orField(cpu.ar[4], 0xE, 0x8);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x7093);
    cpu.r[6] = doubleToTms(randSpread(c, 6.0));
    cpu.r[7] = doubleToTms(randSpread(c, 6.0));
    puffLoop(c);
}

// --- 0x6822: partículas 0x520 ------------------------------------------------------------------

/// Laço da faísca: tremula o objeto até ficar longe demais.
void sparkLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (tooFar(c, 0xB131)) return decAndFree(c, 0xE5F6);
    c.wf(cpu.ar[4] + 1, randSpread(c, 6.0) + c.rf(cpu.ar[4] + 1));
    c.wf(cpu.ar[4] + 3, randSpread(c, 6.0) + c.rf(cpu.ar[4] + 3));
    cpu.ar[0] = c.rd(cpu.ar[5]++);
    if (cpu.ar[0] == 0xFFFFFFFFu) return decAndFree(c, 0xE5F6);
    c.wr(cpu.ar[4] + 0xD, cpu.ar[0]);
    const uint32_t wait = c.rd(cpu.ar[5]++);
    c.wf(cpu.ar[4] + 2, c.rf(cpu.ar[4] + 2) - c.rf(cpu.ar[5]++));
    c.sleep(wait, 0x6855);
}

/// Começa uma faísca (no máximo 5, contadas em `[E5F6]`).
void sparkStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(0xE5F6);
    if (s(cpu.r[0]) >= 5) return c.die();
    c.wr(0xE5F6, cpu.r[0] + 1);
    if (tooFar(c, 0xB12F)) return c.die();  // quirk: [E5F6] fica incrementado
    if (!allocFrom(c)) return;              // idem
    cpu.ar[5] = c.rd(0x685C);
    c.wr(cpu.ar[4] + 0xD, c.rd(cpu.ar[5]));
    c.wr(cpu.ar[4] + 0xF, 0x520);
    cpu.ar[2] = c.rd(0xB130);
    c.call(0x9EDB);
    c.wr(cpu.ar[4] + 0x10, cpu.r[0]);
    copyPos(c, cpu.ar[6]);
    c.orField(cpu.ar[4], 0xE, 0x408);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x7093);
    sparkLoop(c);
}

// --- 0x685D: detritos que quicam ---------------------------------------------------------------

/// true fora do modo 2 (atração).
bool notAttract(ProcCtx& c) { return (c.rd(0xC8C5) & 0xFu) != 2; }

/// Destroço parado: solta o objeto quando sai da tela.
void debrisRest(ProcCtx& c) {
    if (s(c.rd(c.cpu.ar[4] + 0x1C)) < 0) return decAndFree(c, 0xE5F7);
    c.sleep(1, 0x68DE);
}

/// Laço do destroço: cai e gira até parar ou sair da tela.
void debrisLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t o = cpu.ar[4];
    const int32_t depth = s(c.rd(o + 0x1C));
    if (depth < 0 || depth > 0x4E20) return decAndFree(c, 0xE5F7);
    double vy = c.rf(o + 0x12) + 25.0;
    if (vy > 450.0) vy = 450.0;
    c.wf(o + 0x12, vy);
    c.wf(o + 1, c.rf(o + 1) + c.rf(o + 0x11));
    c.wf(o + 3, c.rf(o + 3) + c.rf(o + 0x13));
    double y = c.rf(o + 2) + vy;
    c.wf(o + 2, y);
    if (y >= c.rf(cpu.ar[5] + 2)) {
        // 068AC  tocou o chão
        const uint32_t bounces = c.var(0xE);
        if (bounces == 0) {
            c.wf(o + 0x11, randStep3(c, 0x1E) - 45.0);
            c.wf(o + 0x13, randStep3(c, 0x1E) - 45.0);
        } else if (bounces == 3) {
            return debrisRest(c);
        } else {
            c.wf(o + 0x11, c.rf(o + 0x11) * 0.5);
            c.wf(o + 0x13, c.rf(o + 0x13) * 0.5);
        }
        c.setVar(0xE, c.var(0xE) + 1);
        if (vy <= 3.0) return debrisRest(c);
        vy *= -0.5;
        c.wf(o + 0x12, vy);
        c.wf(o + 2, vy + c.rf(o + 2));
        if (notAttract(c)) {
            cpu.ar[2] = 1;
            c.call(0x8EDB);
            c.sound(cpu.r[0] == 0 ? 0x132 : 0x12F);
        }
    }
    c.sleep(1, 0x68D9);
}

/// Começa um destroço (no máximo 5, contados em `[E5F7]`).
void debrisStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(0xE5F7);
    if (s(cpu.r[0]) >= 5) return c.die();
    c.wr(0xE5F7, cpu.r[0] + 1);
    c.call(0x7025);
    if (cpu.carry()) return c.die();  // quirk: [E5F7] fica incrementado
    cpu.ar[4] = cpu.ar[0];
    const uint32_t o = cpu.ar[4];
    c.wr(o + 1, c.rd(cpu.ar[5] + 1));
    c.wf(o + 2, c.rf(cpu.ar[5] + 2) - 1500.0);
    c.wr(o + 3, c.rd(cpu.ar[5] + 3));
    c.wr(o + 0xD, c.rd(0xB132));
    c.orField(o, 0xE, 0x8);
    cpu.ar[2] = o;
    c.call(0x7093);
    if (notAttract(c)) c.sound(0x12F);
    c.wr(cpu.ar[7] + 0xD, c.rd(o + 2));
    c.call(0x1FB0);
    c.wr(cpu.ar[7] + 0xC, cpu.r[0]);
    c.wf(o + 0x12, randStep3(c, 0x14) + 20.0);
    c.wf(o + 1, randStep3(c, 0x14) - 30.0 + c.rf(o + 1));
    c.wf(o + 3, randStep3(c, 0x14) - 30.0 + c.rf(o + 3));
    c.setVar(0xE, 0);
    c.wf(o + 0x11, 0.0);
    c.wf(o + 0x13, 0.0);
    debrisLoop(c);
}

} // namespace

void registerObjectEffects(SubroutineDispatcher& d) {
    addProc(d, 0x677E, puffStart);
    addProc(d, 0x67AB, puffLoop);
    addProc(d, 0x6822, sparkStart);
    addProc(d, 0x6855, sparkLoop);
    addProc(d, 0x685D, debrisStart);
    addProc(d, 0x68D9, debrisLoop);
    addProc(d, 0x68DE, debrisRest);
}

} // namespace rea::core::game
