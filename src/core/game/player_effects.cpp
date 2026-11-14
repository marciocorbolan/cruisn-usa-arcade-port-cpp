/**
 * @file player_effects.cpp
 * @brief Tradução de `0xAD93`–`0xAE04` (chamas do escapamento) e `0x86FC`–`0x87B7` (fumaça dos pneus).
 */
#include "core/game/player_effects.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kFlameType = 0x101;  ///< Tipo do objeto de chama.
constexpr uint32_t kSmokeType = 0x102;  ///< Tipo do objeto de fumaça.
constexpr uint32_t kSmokeAlive = 0xE918;  ///< Há um processo de fumaça.
constexpr uint32_t kSmokeSlots = 0xE;     ///< `*+AR7(0xE + i)`: nuvens vivas.
constexpr uint32_t kSmokeCount = 0xC;     ///< `*+AR7(0xC)`: nuvens criadas.
constexpr uint32_t kSmokePal = 0xD;       ///< `*+AR7(0xD)`: paleta da fumaça no asfalto.

/// `LDI type,AR2 / CALL $70BB`: carry = achou (AR0 = objeto).
bool findType(ProcCtx& c, uint32_t type) {
    c.cpu.ar[2] = type;
    c.call(0x70BB);
    return c.cpu.carry();
}

// --- 0xAD93: chamas ---------------------------------------------------------------------------

/// `0xADD9`: apaga todos os objetos 0x101 e morre.
void flameEnd(ProcCtx& c) {
    while (findType(c, kFlameType)) {
        c.cpu.ar[2] = c.cpu.ar[0];
        c.call(0x7100);
    }
    c.die();
}

/// `0xADE0`: quadro IR0 da tabela AR1 no objeto AR0 (volta ao início no negativo), com a matriz
/// AR3, posição = carro AR6 + AR3·`[obj+0x11..]` e matriz final = AR3 × Euler(`[obj+0x14..]`).
/// R0 = próximo índice.
void flameFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    for (;;) {
        cpu.r[1] = c.rd(cpu.ar[1] + cpu.ir0);
        if (s(cpu.r[1]) >= 0) break;
        cpu.ir0 = 0;
    }
    cpu.ir0 += 1;
    c.wr(cpu.ar[0] + 0xD, cpu.r[1]);
    const uint32_t next = cpu.ir0;
    cpu.r[2] = cpu.ar[3];
    cpu.ar[2] = cpu.ar[0] + 4;
    c.call(0x93F1);
    cpu.ar[2] = cpu.ar[0] + 0x11;
    cpu.r[2] = cpu.ar[0] + 4;
    cpu.r[3] = cpu.ar[0] + 1;
    c.call(0x941E);
    for (uint32_t i = 1; i <= 3; ++i) c.wf(cpu.ar[0] + i, c.rf(cpu.ar[6] + i) + c.rf(cpu.ar[0] + i));
    cpu.ar[2] = c.rd(0x5B);
    cpu.r[2] = cpu.ar[0] + 0x14;
    c.call(0x9343);
    cpu.ar[2] = cpu.ar[3];
    cpu.r[2] = c.rd(0x5B);
    cpu.r[3] = cpu.ar[0] + 4;
    c.call(0x947F);
    cpu.r[0] = next;
}

/// `0xADC2`: um quadro das chamas (R5 = quadros restantes, AR4/AR5 = objetos, AR6 = carro).
void flameLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[1] = c.rd(cpu.ar[6] + 0x1B);
    if (c.rd(cpu.ar[1] + 0x31) != 0) return flameEnd(c);
    cpu.ar[2] = c.var(0xE);
    cpu.r[2] = cpu.ar[6] + 4;
    cpu.r[3] = c.rd(0x5A);
    c.call(0x947F);
    for (uint32_t obj : {cpu.ar[4], cpu.ar[5]}) {
        cpu.ar[3] = c.rd(0x5A);
        cpu.ar[0] = obj;
        cpu.ir0 = c.var(0xD);
        cpu.ar[1] = c.rd(0xB140);
        c.call(0xADE0);
    }
    c.setVar(0xD, cpu.r[0]);
    c.sleep(1, 0xADD7);
}

/// `0xAD93`: cria as duas chamas do escapamento.
void flameStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (findType(c, kFlameType)) return c.die();
    if (!findType(c, 0x100)) return c.die();
    cpu.ar[6] = cpu.ar[0];
    cpu.ar[1] = c.rd(cpu.ar[0] + 0x1B);
    if (c.rd(cpu.ar[1] + 0x31) != 0) return c.die();
    cpu.ar[2] = cpu.ar[0];
    c.call(0xAE1F);
    c.setVar(0xE, cpu.ar[0]);
    c.setVar(0xD, 0);
    c.call(0x7025);
    if (cpu.carry()) return c.die();
    cpu.ar[4] = cpu.ar[0];
    cpu.ar[0] = c.rd(0xE640) * 3 + c.rd(0xB141);
    const double x = s(c.rd(cpu.ar[0])), y = s(c.rd(cpu.ar[0] + 1)), z = s(c.rd(cpu.ar[0] + 2));
    c.wf(cpu.ar[4] + 0x11, x);
    c.wf(cpu.ar[4] + 0x12, y);
    c.wf(cpu.ar[4] + 0x13, z);
    c.wr(cpu.ar[4] + 0xF, kFlameType);
    c.call(0x7025);
    if (cpu.carry()) return c.die();
    cpu.ar[5] = cpu.ar[0];
    c.wf(cpu.ar[5] + 0x15, c.k(0xADB4));
    c.wf(cpu.ar[5] + 0x11, -x);
    c.wf(cpu.ar[5] + 0x12, y);
    c.wf(cpu.ar[5] + 0x13, z);
    c.wr(cpu.ar[5] + 0xF, kFlameType);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x7093);
    cpu.ar[2] = cpu.ar[5];
    c.call(0x7093);
    cpu.r[5] = 8;
    flameLoop(c);
}

// --- 0x86FC: fumaça dos pneus ----------------------------------------------------------------

/// `0x8780` (a partir de `[AR2+0x1A]`) / `0x877E` (a partir de `[AR2]`): AR2 = próxima peça com
/// `+0x12` = 1 (roda traseira).
void nextRearWheel(ProcCtx& c, bool fromChild) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(cpu.ar[2] + (fromChild ? 0x1A : 0));
    for (;;) {
        cpu.ar[2] = cpu.r[0];
        cpu.r[1] = c.rd(cpu.ar[2] + 0x12);
        if (cpu.r[1] == 1) return;
        cpu.r[0] = c.rd(cpu.ar[2]);
    }
}

/// `0x8788`: põe a nuvem AR0 (animação AR1) na roda AR2 do carro AR4; chão R4 = 0x300 usa a
/// paleta da fumaça. Posição = carro + `[0x5A]`·(roda ± 40 em X, +130 em Y, + R5 em Z).
void placeSmoke(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar1 = cpu.ar[1];
    cpu.ir0 = c.var(kSmokeCount) + kSmokeSlots;
    c.wr(cpu.ar[7] + cpu.ir0, cpu.ar[0]);
    c.wr(cpu.ar[0] + 0x1E, cpu.ar[1]);
    if (cpu.r[4] == 0x300) c.wr(cpu.ar[0] + 0x10, c.var(kSmokePal));
    c.wr(cpu.ar[0] + 0xD, c.rd(cpu.ar[1]));
    c.wr(cpu.ar[0] + 0xF, kSmokeType);
    cpu.r[0] = doubleToTms(40.0);
    c.call(0x8ED1);
    c.wf(cpu.ar[0] + 1, tmsToDouble(cpu.r[0]) + c.rf(cpu.ar[2] + 1));
    c.wf(cpu.ar[0] + 2, 130.0 + c.rf(cpu.ar[2] + 2));
    c.wf(cpu.ar[0] + 3, c.rf(cpu.ar[2] + 3) + tmsToDouble(cpu.r[5]));
    cpu.r[2] = c.rd(0x5A);
    cpu.r[3] = cpu.ar[0] + 1;
    cpu.ar[2] = cpu.r[3];
    c.call(0x941E);
    for (uint32_t i = 1; i <= 3; ++i) c.wf(cpu.ar[0] + i, c.rf(cpu.ar[0] + i) + c.rf(cpu.ar[4] + i));
    c.orField(cpu.ar[0], 0xE, 0x18);
    cpu.ar[2] = cpu.ar[0];
    c.call(0x7093);
    c.setVar(kSmokeCount, c.var(kSmokeCount) + 1);
    cpu.ar[1] = ar1;
}

/// `0x874E`: solta um par de nuvens nas rodas traseiras (sempre na primeira vez; depois só com
/// freio forte a mais de 20 ou derrapagem ≥ 0,25, até 20). Carry = não soltou.
void spawnSmoke(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t d = cpu.ar[5];
    if (s(c.var(kSmokeCount)) >= 1) {
        const bool hardBrake = !(c.rf(d + 0x2B) < c.k(0x8752)) && c.rf(d + 0x26) > 20.0;
        if (!hardBrake && c.rf(d + 0x25) < 0.25) {
            cpu.st |= Tms32031State::kStCarry;
            return;
        }
        if (s(c.var(kSmokeCount)) >= 0x14) {
            cpu.st |= Tms32031State::kStCarry;
            return;
        }
    }
    // 0875D
    cpu.ar[2] = cpu.ar[4];
    c.call(0xAE1F);
    cpu.ar[2] = cpu.ar[0];
    cpu.r[2] = cpu.ar[4] + 4;
    cpu.r[3] = c.rd(0x5A);
    c.call(0x947F);
    cpu.ar[1] = c.rd(0xC6BD);
    if (c.rf(d + 0x25) > 0.5) cpu.ar[1] = c.rd(0xC6BE);
    if (c.rd(d + 0x31) != 0) cpu.ar[1] = c.rd(0xC6BE);
    cpu.ar[2] = c.rd(cpu.ar[1]);
    c.call(0x700A);
    if (cpu.carry()) return;
    cpu.ar[2] = cpu.ar[4];
    nextRearWheel(c, true);
    const uint32_t wheel = cpu.ar[2];
    cpu.ar[3] = c.rd(d + 0x1D);
    cpu.r[4] = c.rd(cpu.ar[3] + 0xF);
    placeSmoke(c);
    cpu.ar[2] = c.rd(cpu.ar[1]);
    c.call(0x700A);
    cpu.ar[2] = wheel;
    if (cpu.carry()) return;
    nextRearWheel(c, false);
    cpu.ar[3] = c.rd(d + 0x17);
    cpu.r[4] = c.rd(cpu.ar[3] + 0xF);
    placeSmoke(c);
}

/// `0x8739`: R5 = 0,0; solta fumaça a cada 2 quadros (todo quadro em rodopio); dorme 1.
void smokeTick(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[5] = doubleToTms(0.0);
    double r7 = tmsToDouble(cpu.r[7]);
    bool spawn = true;
    if (c.rd(cpu.ar[5] + 0x31) == 0) {
        if (r7 != 0.0) spawn = false;
        else r7 = 2.0;
    }
    cpu.r[7] = doubleToTms(r7);
    if (spawn) c.call(0x874E);
    cpu.r[7] = doubleToTms(tmsToDouble(cpu.r[7]) - 1.0);
    c.sleep(1, 0x8743);
}

/// `0x8708`: anima as nuvens vivas (próximo quadro, deriva para trás, altura do chão).
void smokeUpdate(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[6] = 0;
    cpu.r[5] = 0;
    cpu.ir0 = kSmokeSlots;
    // port: limite de segurança contra laço infinito com contagem 0 (a ROM não tem)
    for (uint32_t guard = 0; guard < 0x10000; ++guard) {
        cpu.ar[0] = c.rd(cpu.ar[7] + cpu.ir0);
        if (cpu.ar[0] != 0) {
            cpu.ar[1] = c.rd(cpu.ar[0] + 0x1E);
            cpu.r[0] = c.rd(cpu.ar[1]);
            cpu.ar[1] += 1;
            if (s(cpu.r[0]) < 0) {
                // 08744  fim da animação: libera e esvazia o slot (reavalia o mesmo slot)
                cpu.ar[2] = cpu.ar[0];
                const uint32_t ir0 = cpu.ir0;
                c.call(0x7100);
                cpu.ir0 = ir0;
                cpu.r[0] = 0;
                c.wr(cpu.ar[7] + cpu.ir0, 0);
                continue;
            }
            c.wr(cpu.ar[0] + 0xD, cpu.r[0]);
            c.wr(cpu.ar[0] + 0x1E, cpu.ar[1]);
            cpu.ar[2] = c.rd(0x5A);
            cpu.r[2] = c.rd(cpu.ar[5] + 0x2D);
            c.call(0x938A);
            c.call(0x9414);
            const double back = static_cast<double>(s(c.rd(0xC92F))) * c.rf(cpu.ar[5] + 0x26) * c.k(0x8719) - 50.0;
            c.wf(cpu.ar[2] + 2, back);
            cpu.r[2] = c.rd(0x5A);
            cpu.r[3] = cpu.ar[2];
            c.call(0x941E);
            cpu.ar[1] = c.rd(0x5D);
            c.wf(cpu.ar[0] + 1, c.rf(cpu.ar[1]) + c.rf(cpu.ar[0] + 1));
            c.wf(cpu.ar[0] + 3, c.rf(cpu.ar[1] + 2) + c.rf(cpu.ar[0] + 3));
            const uint32_t ar4 = cpu.ar[4], ir0 = cpu.ir0, r5 = cpu.r[5];
            cpu.ar[4] = cpu.ar[0] + 1;
            c.call(0x1F8B);
            cpu.r[5] = r5;
            cpu.ir0 = ir0;
            cpu.ar[4] = ar4;
            cpu.ar[0] = c.rd(cpu.ar[7] + cpu.ir0);
            c.wf(cpu.ar[0] + 2, tmsToDouble(cpu.r[0]) + c.rf(cpu.ar[0] + 2));
            cpu.r[6] += 1;
        }
        // 08733
        cpu.ir0 += 1;
        cpu.r[5] += 1;
        if (cpu.r[5] == c.var(kSmokeCount)) break;
    }
    if (cpu.r[6] == 0) {
        c.wr(kSmokeAlive, 0);
        return c.die();
    }
    smokeTick(c);
}

/// `0x86FC`: só um processo de fumaça; paleta, contagem 0 e o primeiro par.
void smokeStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (s(c.rd(kSmokeAlive)) >= 1) return c.die();
    cpu.r[7] = doubleToTms(0.0);
    cpu.ar[2] = c.rd(0xC6BF);
    c.call(0x9EDB);
    c.setVar(kSmokePal, cpu.r[0]);
    c.setVar(kSmokeCount, 0);
    c.wr(kSmokeAlive, 1);
    smokeTick(c);
}

} // namespace

void registerPlayerEffects(SubroutineDispatcher& d) {
    addProc(d, 0xAD93, flameStart);
    addProc(d, 0xADC2, flameLoop);
    addProc(d, 0xADD7, [](ProcCtx& c) {
        c.cpu.r[5] -= 1;
        if (s(c.cpu.r[5]) > 0) return flameLoop(c);
        flameEnd(c);
    });
    addProc(d, 0xADD9, flameEnd);
    addProc(d, 0xADE0, flameFrame);
    addProc(d, 0x86FC, smokeStart);
    addProc(d, 0x8708, smokeUpdate);
    addProc(d, 0x8743, smokeUpdate);  // BR $8708 depois do sleep
    addProc(d, 0x874E, spawnSmoke);
    addProc(d, 0x877E, [](ProcCtx& c) { nextRearWheel(c, false); });
    addProc(d, 0x8780, [](ProcCtx& c) { nextRearWheel(c, true); });
    addProc(d, 0x8788, placeSmoke);
}

} // namespace rea::core::game
