/**
 * @file special_car.cpp
 * @brief Tradução de `0x7C86`–`0x7EE8` (carro especial 0x270 da pista 2) e `0xC10000`–`0xC1000E`
 *        (giro das rodas).
 */
#include "core/game/special_car.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kTicks = 0xC92F;  ///< Quadros (VBLANKs) gastos na última iteração do loop principal.
constexpr uint32_t kFocusCar = 0xE876;  ///< objeto do carro focado (jogador/câmera)
constexpr uint32_t kFocusData = 0xE878;  ///< dados D do carro focado
constexpr uint32_t kLastNode = 0xE470;  ///< Último nó de pista carregado (fim da janela `[E46E]`..`[E470]`).
constexpr uint32_t kWindowEnd = 0xE474;  ///< Fim da janela de nós carregados.
constexpr uint32_t kWindowStart = 0xE46C;  ///< Início da janela de nós carregados.
constexpr uint32_t kStop = 0xE540;     ///< ≠ 0: o carro sai.
constexpr uint32_t kOnePlayer = 0xE607;  ///< posição do focado (1 = líder)
constexpr uint32_t kMatA = 0x005A;  ///< `[0x5A]`: matriz de rascunho.
constexpr uint32_t kMatB = 0x005B;  ///< `[0x5B]`: segunda matriz de rascunho.
constexpr uint32_t kLaneVec = 0x809855;  ///< `($9855)` com DP = 0x80 (vetor `[0x5D]`).
constexpr uint32_t kEngine = 0x135;  ///< Som do motor do carro especial.

/// Quadros gastos na última iteração do loop principal (`[C92F]`), como double.
double ticks(ProcCtx& c) { return static_cast<double>(s(c.rd(kTicks))); }
/// Lê o registrador R@p i como float TMS.
double R(ProcCtx& c, int i) { return tmsToDouble(c.cpu.r[i]); }
/// Grava @p v, convertido para float TMS, no registrador R@p i.
void setR(ProcCtx& c, int i, double v) { c.cpu.r[i] = doubleToTms(v); }
/// Lê a variável float @p n do processo (AR7 + @p n).
double varF(ProcCtx& c, uint32_t n) { return c.rf(c.cpu.ar[7] + n); }
/// Grava a variável float @p n do processo (AR7 + @p n).
void setVarF(ProcCtx& c, uint32_t n, double v) { c.wf(c.cpu.ar[7] + n, v); }
/// Endereço do campo @p off do objeto apontado por AR4.
uint32_t obj(ProcCtx& c, uint32_t off) { return c.cpu.ar[4] + off; }

/// Aleatório em [0, @p n) (`0x8EDB`).
uint32_t rnd(ProcCtx& c, uint32_t n) {
    c.cpu.ar[2] = n;
    c.call(0x8EDB);
    return c.cpu.r[0];
}

void outerLoop(ProcCtx& c);
void leaveStart(ProcCtx& c);

/// `0x7E29`: devolve os dados de carro, solta o objeto, cala o motor, mata as rodas e morre.
void carExit(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(obj(c, 0x1B));
    c.call(0x9032);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x7100);
    cpu.ar[2] = kEngine;
    c.call(0x91C1);
    cpu.ar[2] = c.var(0x1E);
    c.call(0xA8AC);
    c.die();
}

/// `0x7C86`/`0x7D39`: zera `+11..+15` e `+1E`.
void zeroMotion(ProcCtx& c) {
    for (uint32_t off : {0x14u, 0x15u, 0x1Eu, 0x11u, 0x12u, 0x13u}) c.wf(obj(c, off), 0.0);
    setR(c, 0, 0.0);
}

/// Busca do trecho: `*+AR7(10)` = AR2, `*+AR7(13)` = `+1E`, `CALL lane` e posição no trecho deslocado
/// (1000 abaixo), rumo R2 em `+15` e `*+AR7(D)`, matriz rotY.
void placeOnNode(ProcCtx& c, uint32_t node, uint32_t lane) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = node;
    c.setVar(0x10, node);
    cpu.r[0] = c.rd(node + 0x1E);
    c.setVar(0x13, cpu.r[0]);
    c.call(lane);
    const uint32_t a = cpu.ar[2];
    c.wf(obj(c, 1), c.rf(a + 1) + c.rf(kLaneVec));
    setR(c, 1, 1000.0);
    c.wf(obj(c, 2), c.rf(a + 2) - 1000.0 + c.rf(kLaneVec + 1));
    setR(c, 0, c.rf(a + 3) + c.rf(kLaneVec + 2));
    c.wr(obj(c, 3), cpu.r[0]);
    cpu.dp = 0;
    c.wr(obj(c, 0x15), cpu.r[2]);
    c.setVar(0xD, cpu.r[2]);
    cpu.ar[2] = cpu.ar[4] + 4;
    c.call(0x938A);
}

/// `CALL $66BC` e guarda a distância em `*+AR7(17)` (anterior em `*+AR7(18)`).
double trackFocus(ProcCtx& c) {
    c.call(0x66BC);
    const uint32_t prev = c.var(0x17);
    c.setVar(0x17, c.cpu.r[0]);
    c.cpu.r[1] = prev;
    c.setVar(0x18, prev);
    return R(c, 0);
}

/// `0x7CB1`/`0x7D6E`: segue os trechos por @p link até um a mais de 5000.
/// @return 0 = achou, 1 = saiu da janela (`0x7E29`), 2 = sem trecho seguinte (só no sentido contrário).
int advanceNodes(ProcCtx& c, uint32_t link, bool negOffset) {
    Tms32031State& cpu = c.cpu;
    for (;;) {
        cpu.r[0] = c.var(0x13) >> 8;
        cpu.r[1] = c.rd(kWindowEnd) - c.rd(kWindowStart);
        if (s(cpu.r[0]) <= s(cpu.r[1])) return 1;
        cpu.ar[2] = c.var(0x10);
        cpu.r[0] = c.rd(cpu.ar[2] + link);
        if (cpu.r[0] == 0) return negOffset ? 2 : 1;
        if (!negOffset) {
            cpu.ar[2] = c.rd(kFocusData);
            c.setVar(0x1B, c.rd(cpu.ar[2] + 0x46));
        }
        c.call(negOffset ? 0x66EB : 0x66DB);
        if (R(c, 0) > 5000.0) return 0;
        cpu.ar[2] = c.var(0x10);
        cpu.r[0] = c.rd(cpu.ar[2] + link);
        c.setVar(0x10, cpu.r[0]);
        cpu.ar[0] = cpu.r[0];
        cpu.r[0] = c.rd(cpu.ar[0] + 0x1E);
        c.setVar(0x13, cpu.r[0]);
    }
}

/// `0x7CC8`/`0x7D82` (@p pc): volante para o trecho `*+AR7(10)` dividido pelos quadros que faltam
/// (R7 = distância / (`[C92F]`·`*+AR7(23)`)), limitado a ±0.3926, em `*+AR7(D)`.
void steerToNode(ProcCtx& c, uint32_t pc) {
    Tms32031State& cpu = c.cpu;
    double r1 = ticks(c) * varF(c, 0x23);
    if (r1 <= 0.0) r1 = c.k(pc + 2);
    setR(c, 1, r1);
    c.call(0xA509);
    cpu.r[7] = tmsFix(R(c, 0));
    cpu.ar[2] = c.var(0x10);
    const uint32_t n = cpu.ar[2];
    setR(c, 2, c.rf(n + 1) - c.rf(obj(c, 1)) + c.rf(kLaneVec));
    setR(c, 3, c.rf(n + 3) - c.rf(obj(c, 3)) + c.rf(kLaneVec + 2));
    cpu.dp = 0;
    c.call(0x931A);
    setR(c, 0, R(c, 0) - c.k(pc + 0xF));
    cpu.r[2] = c.rd(obj(c, 0x15));
    c.call(0x94A7);
    const double frames = static_cast<double>(s(cpu.r[7])) - 1.0;
    setR(c, 1, frames);
    if (frames > 0.0) c.call(0xA509);
    double a = R(c, 0);
    if (a > c.k(pc + 0x16)) a = c.k(pc + 0x17);
    if (a < c.k(pc + 0x18)) a = c.k(pc + 0x19);
    setR(c, 0, a);
    c.setVar(0xD, cpu.r[0]);
}

/// `0x7CEE`/`0x7E1C`: `*+AR7(21)` += `*+AR7(20)`, até 667.
void accelerate(ProcCtx& c) {
    double v = varF(c, 0x21) + varF(c, 0x20);
    setR(c, 1, 667.0);
    if (v > 667.0) v = 667.0;
    setR(c, 0, v);
    setVarF(c, 0x21, v);
}

/// `0x7E64`: dirige com volante @p steer e velocidade @p speed.
void drive(ProcCtx& c, uint32_t steer, uint32_t speed) {
    c.cpu.r[2] = steer;
    c.cpu.r[3] = speed;
    c.call(0x7E64);
}

// --- Passadas no mesmo sentido (`0x7C86`–`0x7CF9`) --------------------------------------------

/// `0x7CF9`: laço da passada; acaba a 22000 do carro focado ou no fim dos trechos.
void forwardLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (trackFocus(c) <= 22000.0) return leaveStart(c);
    c.call(0x7EC6);
    if (advanceNodes(c, 0x17, false) != 0) return carExit(c);
    steerToNode(c, 0x7CC8);
    // 07CE3  altura do chão sob o carro
    const uint32_t ar4 = cpu.ar[4];
    cpu.ar[4] = ar4 + 1;
    c.call(0x1F8B);
    cpu.ar[4] = ar4;
    if (cpu.carry()) {
        setR(c, 1, -1300.0);
        setR(c, 0, R(c, 0) - 1300.0 + c.rf(obj(c, 2)));
        c.setVar(0x26, cpu.r[0]);
    }
    accelerate(c);
    drive(c, c.var(0xD), c.var(0x21));
    c.sleep(1, 0x7CF9);
}

/// `0x7C86`: começa uma passada, 1000 abaixo no trecho do carro focado.
void forwardPass(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    zeroMotion(c);
    cpu.ar[2] = c.rd(kFocusData);
    c.setVar(0x1B, c.rd(cpu.ar[2] + 0x46));
    cpu.ar[2] = c.rd(kLastNode);
    cpu.ar[2] = c.rd(cpu.ar[2] + 0x17);
    cpu.ar[2] = c.rd(cpu.ar[2] + 0x17);
    placeOnNode(c, cpu.ar[2], 0x6739);
    forwardLoop(c);
}

// --- Passada em sentido contrário (`0x7D39`–`0x7E07`) -----------------------------------------

/// `0x7E07`: laço da passada em sentido contrário (acompanha o jogador).
void reverseLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kStop) != 0) return leaveStart(c);
    cpu.ar[2] = c.rd(kFocusData);
    setVarF(c, 0x1B, -c.rf(cpu.ar[2] + 0x46));
    if (trackFocus(c) > 10000.0) return leaveStart(c);
    c.call(0x7EC6);
    const int r = advanceNodes(c, 0x21, true);
    if (r == 1) return carExit(c);
    if (r == 2) return c.sleep(1, 0x7E07);
    steerToNode(c, 0x7D82);
    c.call(0x7EE9);
    c.setVar(0x28, cpu.ar[0]);
    c.setVar(0x26, c.var(0x27));
    cpu.ar[0] = c.var(0x28);
    cpu.r[0] = tmsFix(c.rf(cpu.ar[0] + 2)) - 0x514;
    setR(c, 0, static_cast<double>(s(cpu.r[0])));
    c.setVar(0x26, cpu.r[0]);
    // 07DA6  à frente do jogador? (trecho do objeto de estrada × trecho do jogador)
    cpu.ar[0] = c.var(0x28);
    cpu.r[0] = c.rd(cpu.ar[0] + 0x1E);
    cpu.ar[1] = c.rd(kFocusData);
    cpu.ar[2] = c.rd(cpu.ar[1] + 0x37);
    cpu.r[1] = c.rd(cpu.ar[2] + 0x1E);
    bool ahead;
    if (s(cpu.r[0]) < s(cpu.r[1])) {
        ahead = false;
    } else if (s(cpu.r[0]) > s(cpu.r[1])) {
        ahead = true;
    } else {
        cpu.ar[2] = c.rd(cpu.ar[2] + 0x21);
        cpu.ar[0] = c.rd(kFocusCar);
        const uint32_t n = cpu.ar[2], f = cpu.ar[0];
        const double fx = c.rf(f + 1) - c.rf(n + 1), fz = c.rf(f + 3) - c.rf(n + 3);
        const double ox = c.rf(obj(c, 1)) - c.rf(n + 1), oz = c.rf(obj(c, 3)) - c.rf(n + 3);
        const double df = fx * fx + fz * fz, dobj = ox * ox + oz * oz;
        setR(c, 0, df);
        setR(c, 1, dobj);
        ahead = !(dobj > df);
    }
    const double playerSpeed = c.rf(cpu.ar[1] + 0x26);
    const double dist = varF(c, 0x17);
    bool chase = false;
    double v = 0.0;
    if (ahead) {
        c.setVar(0x29, 1);
        if (dist < 1750.0) {
            chase = true;
        } else {
            const double cur = varF(c, 0x21);
            const double cap = cur * c.k(0x7DC8);
            v = cur + varF(c, 0x20);
            const double lim = playerSpeed * c.k(0x7DCB);
            if (v > lim) v = lim;
            if (v > cap) v = cap;
        }
    } else {
        c.setVar(0x29, 0);
        if (dist < 3500.0) {
            chase = true;
        } else {
            v = playerSpeed * c.k(0x7DD9);
            if (v < 600.0) v = 600.0;
        }
    }
    if (chase) {
        // 07DDF
        const double cap = varF(c, 0x21) * c.k(0x7DE0);
        v = playerSpeed * c.k(0x7DE2);
        if (v < 500.0) v = 500.0;
        if (v > cap) v = cap;
    }
    setR(c, 0, v);
    setVarF(c, 0x21, v);
    // 07DE9
    drive(c, c.var(0xD), c.var(0x21));
    cpu.r[4] = c.var(0x1F);
    if (cpu.r[4] != 2) return c.sleep(1, 0x7E07);
    cpu.r[0] = c.var(0x22) - 1;
    c.setVar(0x22, cpu.r[0]);
    if (s(cpu.r[0]) > 0) return c.sleep(1, 0x7E07);
    if (c.var(0x29) == 0) return c.sleep(1, 0x7E07);
    if (c.rd(kOnePlayer) != 1) return leaveStart(c);
    if (2300.0 > varF(c, 0x17)) return c.sleep(1, 0x7E07);
    const uint32_t wait = rnd(c, 10) + 10;
    c.setVar(0x22, wait);
    cpu.r[4] = wait;
    cpu.r[0] = c.var(0x2B) + 1;
    if (s(cpu.r[0]) > s(c.var(0x2D))) return leaveStart(c);
    c.setVar(0x2B, cpu.r[0]);
    c.sleep(1, 0x7E07);
}

/// `0x7D39`: começa a passada em sentido contrário.
void reversePass(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    zeroMotion(c);
    cpu.ar[2] = c.rd(kFocusData);
    setVarF(c, 0x1B, -c.rf(cpu.ar[2] + 0x46));
    cpu.ar[2] = c.rd(cpu.ar[2] + 0x37);
    cpu.ar[2] = c.rd(cpu.ar[2] + 0x17);
    placeOnNode(c, cpu.ar[2], 0x6740);
    setVarF(c, 0x20, 60.0);
    c.setVar(0x22, 0);
    c.setVar(0x1F, 2);
    reverseLoop(c);
}

// --- Saída de cena (`0x7E08`–`0x7E28`) ---------------------------------------------------------

/// Laço da saída de cena (101 quadros, caindo).
void leaveLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x66BC);
    c.setVar(0x17, cpu.r[0]);
    c.call(0x7EC6);
    double r7 = R(c, 7) - c.k(0x7E16);
    if (r7 < c.k(0x7E17)) r7 = c.k(0x7E18);
    setR(c, 7, r7);
    setVarF(c, 0x26, varF(c, 0x26) + r7);
    accelerate(c);
    drive(c, c.var(0x25), c.var(0x21));
    c.sleep(1, 0x7E27);
}

/// `0x7E08`: começa a saída de cena.
void leaveStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[6] = 0;
    cpu.ar[5] = 100;
    setR(c, 7, c.k(0x7E0A));
    setR(c, 0, c.k(0x7E0B));
    c.call(0x8ED1);
    double turn = R(c, 0);
    turn = turn < 0.0 ? turn - c.k(0x7E11) : turn + c.k(0x7E0F);
    setR(c, 0, turn);
    setVarF(c, 0x25, turn);
    leaveLoop(c);
}

// --- Laço externo e entrada -------------------------------------------------------------------

/// `0x7D24`: laço externo; escolhe a próxima passada ou sai.
void outerLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kStop) != 0 || c.var(0x2E) != 0) return carExit(c);
    cpu.r[0] = c.var(0x2A) + 1;
    if (s(cpu.r[0]) <= s(c.var(0x2C))) {
        c.setVar(0x2A, cpu.r[0]);
        return forwardPass(c);
    }
    c.setVar(0x2E, 1);
    cpu.r[0] = rnd(c, 2);
    c.setVar(0x2F, cpu.r[0]);
    if (cpu.r[0] != 0) return forwardPass(c);
    c.setVar(0x2D, rnd(c, 0x14) + 0x14);
    reversePass(c);
}

/// `0x7CFA`: entrada do processo; pede o objeto e monta o carro.
void carStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(0xE877);
    c.call(0xA91F);
    c.call(0x7025);
    if (cpu.carry()) return c.die();
    cpu.ar[4] = cpu.ar[0];
    c.wr(obj(c, 0xD), 0xC5F2C7);
    c.wf(obj(c, 1), 0.0);
    c.wf(obj(c, 2), -20.0);
    c.wf(obj(c, 3), 3500.0);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x7093);
    c.call(0x2945);
    cpu.ar[5] = c.rd(obj(c, 0x1B));
    c.call(0x7E3F);
    setR(c, 2, 0.0);
    c.wr(obj(c, 0x14), cpu.r[2]);
    c.wr(obj(c, 0x15), cpu.r[2]);
    c.wr(obj(c, 0x1E), cpu.r[2]);
    cpu.ar[2] = cpu.ar[4] + 4;
    c.call(0x938A);
    c.setVar(0x2A, 0);
    c.setVar(0x2B, 0);
    cpu.r[0] = 0x270;
    c.wr(obj(c, 0xF), 0x270);
    c.setVar(3, 0x270);
    c.wr(obj(c, 0x17), cpu.ar[7]);
    c.setVar(0x2C, rnd(c, 2) + 1);
    c.setVar(0x2F, 0);
    c.setVar(0x2E, 0);
    outerLoop(c);
}

// --- Sub-rotinas ------------------------------------------------------------------------------

/// `0x7E3F`: bit 0x800 em `+E`, nós das rodas da tabela `[B11B]` encadeados a partir de `+1A` e
/// processo `[B11C]` (ID 0x230) em `*+AR7(1E)` e `+16`.
void wheelNodes(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(obj(c, 0xE)) | 0x800;
    c.wr(obj(c, 0xE), cpu.r[0]);
    cpu.ar[2] = c.rd(0xB11B);
    cpu.ar[3] = cpu.ar[4] + 0x1A;
    cpu.rc = c.rd(cpu.ar[2]++);
    const uint32_t count = cpu.rc + 1;
    for (uint32_t i = 0; i < count; ++i) {
        c.call(0x9001);
        const uint32_t n = cpu.ar[0];
        c.wr(cpu.ar[3], n);
        for (uint32_t k = 1; k <= 3; ++k) {
            cpu.r[0] = c.rd(cpu.ar[2]++);
            c.wr(n + k, cpu.r[0]);
            c.wr(n + k + 3, cpu.r[0]);
        }
        cpu.r[0] = c.rd(cpu.ar[2]++);
        c.wr(n + 0x10, cpu.r[0]);
        cpu.r[0] = c.rd(cpu.ar[2]++);
        c.wr(n + 0x12, cpu.r[0]);
        c.wr(n + 0x11, cpu.ar[4]);
        cpu.ar[3] = n;
    }
    cpu.rc = static_cast<uint32_t>(-1);
    cpu.r[0] = 0;
    c.wr(cpu.ar[3], 0);
    cpu.ar[0] = cpu.ar[3] + 7;
    c.call(0x93D7);
    cpu.ar[2] = c.rd(0xB11C);
    cpu.r[2] = 0x230;
    c.call(0xA868);
    c.setVar(0x1E, cpu.ar[0]);
    c.wr(obj(c, 0x16), cpu.ar[0]);
}

/// `0x7E64`: dirige o carro com volante R2 e velocidade R3 (ver o cabeçalho).
void driveStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t steer = cpu.r[2];
    const uint32_t speed = cpu.r[3];
    // altura: +12 = 0.85·+12 + 0.15·(*+AR7(26) − y), até ±60
    double vy = c.rf(obj(c, 0x12)) * c.k(0x7E67) + (varF(c, 0x26) - c.rf(obj(c, 2))) * c.k(0x7E68);
    if (vy > c.k(0x7E6A)) vy = c.k(0x7E6B);
    if (vy < c.k(0x7E6C)) vy = c.k(0x7E6D);
    c.wf(obj(c, 0x12), vy);
    setR(c, 2, tmsToDouble(steer) + c.rf(obj(c, 0x15)));
    c.wr(obj(c, 0x15), cpu.r[2]);
    cpu.ar[2] = c.rd(kMatA);
    c.call(0x938A);
    cpu.r[2] = cpu.ar[2];
    c.call(0x9414);
    c.wr(cpu.ar[2] + 2, speed);
    c.wr(cpu.ar[2] + 1, c.rd(obj(c, 0x12)));
    cpu.r[3] = cpu.ar[2];
    c.call(0x941E);
    const uint32_t v = cpu.ar[2];
    const double t = ticks(c);
    double m = c.rf(v) * c.k(0x7E7D) + c.rf(obj(c, 0x11)) * c.k(0x7E7E);
    c.wf(obj(c, 0x11), m);
    c.wf(obj(c, 1), m * t + c.rf(obj(c, 1)));
    m = c.rf(v + 1) * c.k(0x7E87) + c.rf(obj(c, 0x12)) * c.k(0x7E88);
    c.wf(obj(c, 0x12), m);
    c.wf(obj(c, 2), m + c.rf(obj(c, 2)));
    m = c.rf(v + 2) * c.k(0x7E8F) + c.rf(obj(c, 0x13)) * c.k(0x7E90);
    c.wf(obj(c, 0x13), m);
    setR(c, 1, t);
    c.wf(obj(c, 3), m * t + c.rf(obj(c, 3)));
    cpu.r[2] = c.rd(obj(c, 0x15));
    cpu.ar[2] = cpu.ar[4] + 4;
    c.call(0x938A);
    cpu.r[0] = c.var(0x23);
    cpu.r[2] = c.rd(obj(c, 0x14));
    cpu.ar[2] = c.rd(kMatB);
    c.call(0x9377);
    // inclinação: +1E = 0.1·25·volante + 0.9·+1E, até ±0.4
    double roll = tmsToDouble(steer) * c.k(0x7EA0) * c.k(0x7EA2) + c.rf(obj(c, 0x1E)) * c.k(0x7EA3);
    if (roll > c.k(0x7EA5)) roll = c.k(0x7EA6);
    if (roll < c.k(0x7EA7)) roll = c.k(0x7EA8);
    setR(c, 2, roll);
    c.wr(obj(c, 0x1E), cpu.r[2]);
    cpu.ar[2] = c.rd(kMatA);
    c.call(0x93C4);
    cpu.ar[2] = c.rd(kMatB);
    cpu.r[2] = c.rd(kMatA);
    cpu.r[3] = cpu.ar[2];
    c.call(0x947F);
    cpu.r[2] = cpu.ar[4] + 4;
    cpu.r[3] = cpu.r[2];
    c.call(0x947F);
    const double vx = c.rf(obj(c, 0x11)), vv = c.rf(obj(c, 0x12)), vz = c.rf(obj(c, 0x13));
    setR(c, 2, vx * vx + vv * vv + vz * vz);
    c.call(0xA5F1);
    cpu.r[1] = c.var(0x23);
    c.setVar(0x23, cpu.r[0]);
    c.setVar(0x24, cpu.r[1]);
    c.call(0xA509);
    double pitch = R(c, 0);
    if (pitch > c.k(0x7EC1)) pitch = c.k(0x7EC2);
    pitch *= c.k(0x7EC3);
    setR(c, 0, pitch);
    c.wf(obj(c, 0x14), pitch);
}

/// `0x7EC6`: volume do motor 0x135 conforme a distância `*+AR7(17)`.
void engineSound(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    auto channel = [&](uint32_t scale, uint32_t ch, uint32_t pc) {
        double d = varF(c, 0x17);
        setR(c, 1, 20000.0);
        if (d > 20000.0) d = 20000.0;
        d = (20000.0 - d) * c.rf(scale) * c.k(pc) + c.k(pc + 1);
        setR(c, 0, d);
        cpu.r[1] = tmsFix(d);
        cpu.r[0] = ch;
        c.call(0x9114);
    };
    cpu.ar[2] = kEngine;
    if (c.rd(0xE8FF) == cpu.ar[2]) return channel(0xB11D, 1, 0x7ED4);
    if (c.rd(0xE908) == cpu.ar[2]) return channel(0xB11E, 2, 0x7EE2);
    c.call(0x9188);
    if (c.rd(0xE8FF) == cpu.ar[2]) return channel(0xB11D, 1, 0x7ED4);
    if (c.rd(0xE908) == cpu.ar[2]) return channel(0xB11E, 2, 0x7EE2);
}

/// `0xC10001`: gira o primeiro nó de roda (`+1A`) 0.6·`[C92F]` por quadro.
void wheelLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t node = c.rd(obj(c, 0x1A));
    cpu.ar[0] = node;
    setR(c, 0, ticks(c) * c.k(0xC10005));
    setR(c, 2, R(c, 6) + R(c, 0));
    c.call(0x9308);
    cpu.r[6] = cpu.r[2];
    cpu.ar[0] = node;
    cpu.ar[2] = node + 7;
    c.call(0x938A);
    c.sleep(1, 0xC1000E);
}

} // namespace

void registerSpecialCar(SubroutineDispatcher& d) {
    addProc(d, 0x7CFA, carStart);
    addProc(d, 0x7D24, outerLoop);
    addProc(d, 0x7CF9, forwardLoop);
    addProc(d, 0x7E07, reverseLoop);
    addProc(d, 0x7E08, leaveStart);
    addProc(d, 0x7E27, [](ProcCtx& c) {
        c.cpu.ar[5] -= 1;
        if (s(c.cpu.ar[5]) >= 0) return leaveLoop(c);
        outerLoop(c);
    });
    addProc(d, 0x7E29, carExit);
    addProc(d, 0x7E3F, wheelNodes);
    addProc(d, 0x7E64, driveStep);
    addProc(d, 0x7EC6, engineSound);
    addProc(d, 0x7E63, [](ProcCtx& c) {
        setR(c, 6, 0.0);
        wheelLoop(c);
    });
    addProc(d, 0xC10000, [](ProcCtx& c) {
        setR(c, 6, 0.0);
        wheelLoop(c);
    });
    addProc(d, 0xC1000E, wheelLoop);
}

} // namespace rea::core::game
