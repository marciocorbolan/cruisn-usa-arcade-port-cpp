/**
 * @file car_crash.cpp
 * @brief Tradução de `0x1FB6`–`0x1FF5`, `0x23EF`–`0x2468`, `0x2478`–`0x2586` e `0x5543`–`0x5566`.
 */
#include "core/game/car_crash.hpp"

#include <cmath>

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kBit23 = 1u << 23;  ///< Bit 23 da palavra de flags do objeto (`+0xE`).
constexpr uint32_t kFarBehind = static_cast<uint32_t>(-0x1770);  ///< z relativo abaixo do qual o carro ficou para trás (−6000).

/// Liga ou desliga o bit C (carry) de ST, que a rotina original usa como retorno de sucesso ou falha.
void setCarry(Tms32031State& cpu, bool on) {
    if (on) cpu.st |= Tms32031State::kStCarry;
    else cpu.st &= ~Tms32031State::kStCarry;
}

/// Lê o registrador R@p i como float TMS.
double R(ProcCtx& c, int i) { return tmsToDouble(c.cpu.r[i]); }
/// Grava @p v, convertido para float TMS, no registrador R@p i.
void setR(ProcCtx& c, int i, double v) { c.cpu.r[i] = doubleToTms(v); }

/// `LDF n,R0 / CALL $8ED1` → aleatório em [−n, +n).
double spread(ProcCtx& c, double n) {
    setR(c, 0, n);
    c.call(0x8ED1);
    return R(c, 0);
}
/// `LDF n,R0 / CALL $8EC2` → n·(0..1).
double scaled(ProcCtx& c, double n) {
    setR(c, 0, n);
    c.call(0x8EC2);
    return R(c, 0);
}

// --- 0x1FB6: chão sob a caixa --------------------------------------------------------------------

/// 01FCF  menor altura (R7) do chão sob os 8 cantos `[[1FB5]+0x18..]` nos objetos da lista R0 que
/// alcançam o objeto AR4 (raios `+1D`).
void lowestInList(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = cpu.r[0];
    const double myR = static_cast<double>(s(c.rd(cpu.ar[4] + 0x1D)));
    while (cpu.ar[2] != 0) {
        const double dx = c.rf(cpu.ar[2] + 1) - c.rf(cpu.ar[4] + 1);
        const double dz = c.rf(cpu.ar[2] + 3) - c.rf(cpu.ar[4] + 3);
        const double reach = static_cast<double>(s(c.rd(cpu.ar[2] + 0x1D))) + myR;
        if (dz * dz + dx * dx < reach * reach) {
            // 01FE6  os 8 cantos contra o objeto AR2
            const uint32_t ar4 = cpu.ar[4], mesh = cpu.ar[2];
            cpu.ar[4] = c.rd(0x1FB5) + 0x18;
            for (int k = 0; k < 8; ++k) {
                const uint32_t r7 = cpu.r[7], pt = cpu.ar[4];
                c.call(0x20B4);
                cpu.ar[2] = mesh;
                cpu.ar[4] = pt;
                cpu.r[7] = r7;
                if (cpu.carry() && tmsToDouble(cpu.r[7]) > R(c, 0)) cpu.r[7] = cpu.r[0];
                cpu.ar[4] += 3;
            }
            cpu.ar[4] = ar4;
            cpu.ir1 = 3;
        }
        cpu.ar[2] = c.rd(cpu.ar[2] + 0x20);
    }
}

/// 01FB6  R0 = menor altura relativa do chão sob a caixa (×0,7; y de baixo ×1) do objeto AR4;
/// carry = achou (R0 < 20000). Preserva AR5; R7 = 20000.
void lowestGround(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar5 = cpu.ar[5];
    cpu.ar[2] = c.rd(0x1FB5);
    cpu.ar[0] = cpu.ar[4];
    for (int i = 0; i <= 5; ++i) setR(c, i, i == 4 ? 1.0 : c.k(0x1FB9 + static_cast<uint32_t>(i)));
    c.call(0x28AE);
    setR(c, 5, static_cast<double>(s(c.rd(cpu.ar[4] + 0x1D))));
    setR(c, 7, 20000.0);
    cpu.r[0] = c.rd(0xE403);
    lowestInList(c);
    cpu.r[0] = c.rd(0xE406);
    lowestInList(c);
    cpu.ar[5] = ar5;
    cpu.r[0] = cpu.r[7];
    setR(c, 7, 20000.0);
    setCarry(cpu, R(c, 0) < 20000.0);
}

// --- 0x23EF: batida forte do jogador ---------------------------------------------------------------

/// 05543  capota o carro do jogador: vista salva em `*+AR7(21)` do PCB `[E877]` (na cabine cria
/// `[C61D]`, ID 0x500), base da altura, giros 0,104, vertical −60 e `[E873]` = 1.
void playerFlip(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar4 = cpu.ar[4], ar5 = cpu.ar[5], ar7 = cpu.ar[7];
    cpu.ar[4] = c.rd(0xE876);
    cpu.ar[5] = c.rd(0xE878);
    cpu.ar[7] = c.rd(0xE877);
    cpu.r[0] = c.rd(0xE871);
    c.wr(cpu.ar[7] + 0x21, cpu.r[0]);
    if (cpu.r[0] == 0) {
        cpu.ar[2] = c.rd(0xC61D);
        cpu.r[2] = 0x500;
        c.call(0xA844);
    }
    c.wf(cpu.ar[7] + 0x20, c.rf(cpu.ar[5] + 3) + c.rf(cpu.ar[4] + 2));
    c.wf(cpu.ar[7] + 0xC, c.k(0x5552));
    c.wf(cpu.ar[7] + 0xD, c.k(0x5554));
    c.wf(cpu.ar[7] + 0xE, 0.0);
    c.wf(cpu.ar[7] + 0xF, 0.0);
    c.wf(cpu.ar[7] + 0x11, 0.0);
    c.wr(cpu.ar[7] + 0x10, c.rd(cpu.ar[5] + 0x2C));
    c.wf(cpu.ar[4] + 0x12, -60.0);
    c.wr(0xE873, 1);
    c.wr(cpu.ar[5] + 0x41, 0);
    cpu.r[0] = 0;
    cpu.ar[7] = ar7;
    cpu.ar[5] = ar5;
    cpu.ar[4] = ar4;
}

/// Mata o processo de animação guardado em @p obj + @p slot e zera o slot.
void killAnim(ProcCtx& c, uint32_t obj, uint32_t slot) {
    const uint32_t pcb = c.rd(obj + slot);
    if (pcb == 0) return;
    c.cpu.ar[2] = pcb;
    c.call(0xA8AC);
    c.wr(obj + slot, 0);
}

/// `0x23EF`: batida forte do jogador contra outro carro.
void hardHit(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar0 = cpu.ar[0], ar1 = cpu.ar[1], ar3 = cpu.ar[3], ar4 = cpu.ar[4], ar5 = cpu.ar[5];
    cpu.ar[3] = c.rd(cpu.ar[0] + 0x1B);
    cpu.ar[4] = cpu.ar[1];
    cpu.ar[5] = c.rd(cpu.ar[4] + 0x1B);
    const uint32_t me = cpu.ar[3], other = cpu.ar[1], od = cpu.ar[5];
    if (c.rd(0xC8E3) == 0 || c.rd(od + 0x58) == 0) {
        if ((c.rd(other + 0xE) & kBit23) != 0) killAnim(c, other, 0x17);
        if ((c.rd(other + 0xE) & 0x800u) != 0) killAnim(c, other, 0x16);
        c.wr(od + 0x41, 0);
        c.wf(od + 0x2D, spread(c, c.k(0x2410)) + c.rf(me + 0x2D));
        setR(c, 0, (scaled(c, c.k(0x2414)) + c.k(0x2416)) * c.k(0x2417));
        cpu.r[1] = c.rd(od + 0x30);
        c.call(0xA509);
        c.wf(od + 0x26, R(c, 0) * c.rf(me + 0x26));
        double lift = (scaled(c, c.k(0x241C)) + c.k(0x241E)) * c.k(0x241F) * c.rf(me + 0x26);
        if (lift < -65.0) lift = -65.0;
        c.wf(other + 0x12, lift);
        cpu.ar[2] = c.rd(0x2477);
        cpu.r[2] = 0x290;
        c.call(0xA868);
        if (!cpu.carry()) {
            c.wr(cpu.ar[4] + 0x17, cpu.ar[0]);
            c.wr(cpu.ar[4] + 0xE, c.rd(cpu.ar[4] + 0xE) | kBit23);
        }
    }
    // 0242D  o jogador perde metade (× [E87E]²); o outro fica com pelo menos 1,25× isso
    const double g = c.rf(0xE87E);
    const double mine = c.rf(me + 0x26) * 0.5 * g * g;
    c.wf(me + 0x26, mine);
    double theirs = c.rf(od + 0x26);
    if (theirs < mine * c.k(0x2432)) theirs = mine * c.k(0x2432);
    c.wf(od + 0x26, theirs);
    uint32_t count;
    bool flipped = false;
    if (c.rd(0xE873) == 0) {
        cpu.ar[2] = 0x190;
        c.call(0x8EE9);
        flipped = cpu.carry();
    }
    if (flipped) {
        c.call(0x5543);
        cpu.ar[2] = c.rd(0x246A);
        count = 4;
    } else {
        bool turn = false;
        if (c.rd(0xE871) != 0) {
            cpu.ar[2] = 0x2EE;
            c.call(0x8EE9);
            turn = cpu.carry();
        }
        double rate;
        uint32_t mode;
        if (!turn) {
            // 02445  meia-volta
            c.wf(me + 0x2D, c.rf(me + 0x2D) + c.k(0x2446));
            rate = 0.0;
            mode = 0xF;
        } else {
            // 0244B  giro de π em +32 com taxa ±0,08 ± 0,04
            c.wf(me + 0x32, c.k(0x244B));
            const double r = spread(c, c.k(0x244D));
            rate = (r > 0.0 ? c.k(0x2450) : c.k(0x2451)) + r;
            mode = 1;
        }
        c.wf(me + 0x2E, rate);
        c.wr(me + 0x31, mode);
        cpu.ar[2] = c.rd(0x2469);
        count = 8;
    }
    // 02458  som pelo modelo do outro carro
    cpu.r[0] = count;
    cpu.r[1] = c.rd(other + 0xD);
    if (cpu.r[1] == c.rd(0x23ED)) c.sound(0x48);
    else if (cpu.r[1] == c.rd(0x23EE)) c.sound(0x63);
    else c.call(0x3159);
    cpu.ar[5] = ar5;
    cpu.ar[4] = ar4;
    cpu.ar[3] = ar3;
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
}

// --- 0x2478: carro arremessado ------------------------------------------------------------------

/// Com link, sincroniza o estado do carro (`0x7ACF`).
void linkSync(ProcCtx& c) {
    if (c.rd(0xC8E3) != 0) c.call(0x7ACF);
}

/// 02558  fim: no link, avisa (`0x2597`/`0x2587`); `0x676B`, libera os dados e o objeto e morre.
void crashEnd(ProcCtx& c, bool checkLaps) {
    Tms32031State& cpu = c.cpu;
    if (checkLaps) {
        if (c.rd(0xC8E3) == 0) {
            c.call(0x676B);
        } else {
            c.call(0x5268);
            if (s(cpu.r[1]) - s(cpu.r[0]) > 0) {
                c.call(0x2597);
                return c.jump(0xA718);
            }
            c.call(0x2587);
            c.call(0x676B);
        }
    } else {
        if (c.rd(0xC8E3) != 0) c.call(0x2587);
        c.call(0x676B);
    }
    cpu.ar[2] = cpu.ar[5];
    c.call(0x9032);
    c.wr(cpu.ar[4] + 0xE, c.rd(cpu.ar[4] + 0xE) ^ kBit23);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x7100);
    c.die();
}

/// true se o carro de AR4 ficou muito para trás.
bool farBehind(ProcCtx& c) { return s(c.rd(c.cpu.ar[4] + 0x1C)) < s(kFarBehind); }

/// Velocidade horizontal (+11, +13) pelo rumo `[D+2D]` + π/2 e a velocidade `[D+26]`.
void headingVelocity(ProcCtx& c, uint32_t kPc) {
    Tms32031State& cpu = c.cpu;
    setR(c, 2, c.rf(cpu.ar[5] + 0x2D) + c.k(kPc));
    c.call(0x92E2);
    const double sp = c.rf(cpu.ar[5] + 0x26);
    c.wf(cpu.ar[4] + 0x13, R(c, 0) * sp);
    c.call(0x92DF);
    c.wf(cpu.ar[4] + 0x11, R(c, 0) * sp);
}

void rolling(ProcCtx& c);

/// 02485  fase 1: no ar.
void airborne(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t v = cpu.ar[7];
    if (c.rd(0xE5EC) != 0x33BB) {
        cpu.r[4] = 0;
        if (farBehind(c)) return crashEnd(c, true);
        cpu.r[3] = c.rd(cpu.ar[4] + 0x1B);
        c.call(0x2041);
        cpu.ar[5] = c.rd(cpu.ar[4] + 0x1B);
        const double dt = static_cast<double>(s(c.rd(0xC92F)));
        c.wf(v + 0xF, c.rf(v + 0xC) * dt + c.rf(v + 0xF));
        c.wf(v + 0x10, c.rf(v + 0xD) * dt + c.rf(v + 0x10));
        c.call(0x25D6);
        headingVelocity(c, 0x249A);
        c.call(0x908B);
        const double vy = static_cast<double>(s(c.rd(0xC92F))) * 2.0 + c.rf(cpu.ar[4] + 0x12);
        c.wf(cpu.ar[4] + 0x12, vy);
        if (vy > 300.0) return crashEnd(c, false);
        c.call(0x2EFD);
        if ((c.rd(0xC8C5) & 0x800u) == 0) c.call(0x2F6B);
        if (!(c.rf(cpu.ar[4] + 0x12) < 0.0)) {
            c.call(0x1FB6);
            if (cpu.carry() && !(R(c, 0) > 0.0)) {
                setR(c, 2, R(c, 0) + 30.0);
                c.call(0x894B);
                c.wf(cpu.ar[4] + 2, R(c, 0) + c.rf(cpu.ar[4] + 2));
                const double vy2 = c.rf(cpu.ar[4] + 0x12);
                if (vy2 < 35.0) return rolling(c);
                c.wf(cpu.ar[4] + 0x12, vy2 * -0.5);
                c.wf(cpu.ar[5] + 0x26, c.rf(cpu.ar[5] + 0x26) * 0.5);
                c.wf(v + 0xC, c.rf(v + 0xC) * 0.5);
                c.wf(v + 0xD, c.rf(v + 0xD) * 0.5);
                cpu.ar[2] = c.rd(0x3085);
                cpu.r[0] = 3;
                c.call(0x3168);
            }
        }
    }
    linkSync(c);
    c.sleep(1, 0x24CE);
}

/// 0254A  de rodas para cima: espera até ser empurrado (velocidade ≠ 0) ou ficar para trás.
void upsideDownWait(ProcCtx& c) {
    linkSync(c);
    c.sleep(1, 0x254E);
}

/// 0257B  de pé: carro parado (`0x2C18` sem aceleração) até ficar para trás.
void parkedLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[4] = 2;
    if (farBehind(c)) return crashEnd(c, true);
    setR(c, 2, 0.0);
    c.call(0x2C18);
    c.call(0x2EFD);
    linkSync(c);
    c.sleep(1, 0x2586);
}

/// 024CF  fase 2: rolando no chão.
void rolling(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t v = cpu.ar[7];
    if (c.rd(0xE5EC) == 0x33BB) {
        linkSync(c);
        return c.sleep(1, 0x2542);
    }
    cpu.r[4] = 1;
    if (farBehind(c)) return crashEnd(c, true);
    c.wf(cpu.ar[4] + 0x12, 500.0);
    headingVelocity(c, 0x24D9);
    c.wr(v + 0xD, c.rd(cpu.ar[5] + 0x2E));
    cpu.r[3] = c.rd(cpu.ar[4] + 0x1B);
    c.call(0x2041);
    cpu.ar[5] = c.rd(cpu.ar[4] + 0x1B);
    const uint32_t o = cpu.ar[4], dd = cpu.ar[5];
    int32_t steps = s(c.rd(0xC92F));  // RPTB com RC = dt − 1: dt passos (ao menos 1)
    if (steps < 1) steps = 1;
    uint32_t state = 0;
    for (int32_t i = 0; i < steps && state == 0; ++i) {
        const double vx = c.rf(o + 0x11) * c.k(0x24EA);
        c.wf(o + 0x11, vx);
        c.wf(o + 1, vx + c.rf(o + 1));
        c.wf(o + 2, c.rf(o + 0x12) + c.rf(o + 2));
        const double vz = c.rf(o + 0x13) * c.k(0x24F2);
        c.wf(o + 0x13, vz);
        c.wf(o + 3, vz + c.rf(o + 3));
        const double rollRate = c.rf(v + 0xC) * c.k(0x24F7);
        c.wf(v + 0xC, rollRate);
        c.wf(v + 0xF, rollRate + c.rf(v + 0xF));
        const double yawRate = c.rf(v + 0xD) * c.k(0x24FC);
        c.wf(v + 0xD, yawRate);
        c.wf(dd + 0x2E, yawRate);
        c.wf(v + 0x10, yawRate + c.rf(v + 0x10));
        setR(c, 2, c.rf(v + 0xF));
        c.call(0x9308);
        const double roll = R(c, 2);
        const double absRoll = std::fabs(roll);
        const double sp = c.rf(dd + 0x26) * c.k(0x2505);
        c.wf(dd + 0x26, sp);
        if (!(sp > 10.0)) {
            const double spin = (std::fabs(c.rf(dd + 0x2E)) + std::fabs(c.rf(v + 0xC))) * 10.0;
            if (!(spin > c.k(0x250D))) {
                if (!(absRoll > c.k(0x250F))) {
                    c.wf(v + 0xF, 0.0);
                    state = 2;  // de pé
                    break;
                }
                if (!(absRoll < c.k(0x2515))) {
                    state = 1;  // de rodas para cima
                    break;
                }
            }
        }
        // 02519  gravidade do giro rumo ao lado mais próximo (±π/2)
        const double edge = roll >= 0.0 ? c.k(0x251B) : c.k(0x251D);
        double r0 = (roll < edge ? c.k(0x251E) : c.k(0x251F)) * c.k(0x2520);
        r0 += c.rf(v + 0xC);
        cpu.r[0] = doubleToTms(r0);
        cpu.r[1] = c.rd(v + 0xC);
        if (s(cpu.r[0] ^ cpu.r[1]) < 0) {  // quirk: XOR3 dos bits (bit 31 = expoente)
            setR(c, 2, std::fabs(c.rf(v + 0xF)));
            c.call(0x9308);
            const double a = std::fabs(R(c, 2));
            if (!(a < c.k(0x2528)) && !(a > c.k(0x252A))) {
                cpu.ar[2] = 0x1EF;
                c.call(0x316E);
            }
        }
        c.wf(v + 0xC, r0);
    }
    // 02532
    c.call(0x2EFD);
    c.call(0x2F6B);
    c.call(0x25D6);
    c.call(0x1FB6);
    cpu.r[1] = state;
    if (cpu.carry()) {
        c.wf(o + 2, R(c, 0) + c.rf(o + 2));
        c.call(0x894C);
        if (state == 0) {
            linkSync(c);
            return c.sleep(1, 0x2542);
        }
    }
    if (state == 2) {
        // 0256A  de pé: volta a ser carro (tipo 0x2x) parado
        c.wr(dd + 0x41, 1);
        const uint32_t type = (c.rd(o + 0xF) & ~0xF0u) | 0x20u;
        c.wr(o + 0xF, type);
        c.wr(dd + 0x40, type);
        c.wr(v + 3, type);
        c.wf(dd + 0x24, 0.0);
        c.wf(dd + 0x2E, 0.0);
        c.wf(dd + 0x26, 0.0);
        c.wr(dd + 0x2D, c.rd(v + 0x10));
        c.wr(dd + 0x2C, c.rd(v + 0x10));
        c.wr(dd + 0x31, 0);
        return parkedLoop(c);
    }
    // 02545  quirk: o tipo 0x2x calculado em R0 é descartado
    c.wf(dd + 0x26, 0.0);
    upsideDownWait(c);
}

/// `0x2478`: começa o arremesso com velocidades aleatórias.
void crashStart(ProcCtx& c) {
    const uint32_t v = c.cpu.ar[7];
    c.wf(v + 0xC, spread(c, c.k(0x2478)));
    c.wf(v + 0xD, spread(c, c.k(0x247B)));
    c.wf(v + 0xE, 0.0);
    c.wf(v + 0xF, 0.0);
    c.wf(v + 0x11, 0.0);
    c.wr(v + 0x10, c.rd(c.cpu.ar[5] + 0x2C));
    airborne(c);
}

} // namespace

void registerCarCrash(SubroutineDispatcher& d) {
    addProc(d, 0x1FB6, lowestGround);
    addProc(d, 0x23EF, hardHit);
    addProc(d, 0x5543, playerFlip);
    addProc(d, 0x894B, [](ProcCtx&) {});
    addProc(d, 0x894C, [](ProcCtx&) {});
    addProc(d, 0x2478, crashStart);
    addProc(d, 0x24CE, airborne);
    addProc(d, 0x2542, rolling);
    addProc(d, 0x254E, [](ProcCtx& c) {
        c.cpu.r[4] = 1;
        if (farBehind(c)) return crashEnd(c, true);
        if (c.rf(c.cpu.ar[5] + 0x26) == 0.0) return upsideDownWait(c);
        c.wf(c.cpu.ar[7] + 0xC, spread(c, c.k(0x2554)));
        rolling(c);
    });
    addProc(d, 0x2586, parkedLoop);
}

} // namespace rea::core::game
