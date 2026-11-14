/**
 * @file roadside_events.cpp
 * @brief Tradução de `0x69A8`–`0x6C51` e `0x6C59`–`0x6C8B` (eventos das pistas 9 e 11).
 */
#include "core/game/roadside_events.hpp"

#include <cmath>

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kTicks = 0xC92F;  ///< Quadros (VBLANKs) gastos na última iteração do loop principal.
constexpr uint32_t kFocusCar = 0xE876;  ///< objeto do carro focado (jogador/câmera)
constexpr uint32_t kFocusData = 0xE878;  ///< dados D do carro focado
constexpr uint32_t kRaceOn = 0xC8C5;   ///< bit 0x200: corrida em andamento.
constexpr uint32_t kAltMode = 0xE871;  ///< ≠ 0: modo em que os eventos param (e o carro gira na batida).
constexpr uint32_t kPath = 0xAA2F;     ///< `[AA2F]`: pontos (x, y, z, trecho) da linha da pista.
constexpr uint32_t kMatA = 0x005A;  ///< `[0x5A]`: matriz de rascunho.
constexpr uint32_t kVecPtr = 0x005D;  ///< `[0x5D]`: vetor de rascunho.
constexpr uint32_t kBit23 = 1u << 23;  ///< Bit 23 da palavra de flags do objeto (`+0xE`).

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
double ifl(ProcCtx& c, uint32_t a) { return static_cast<double>(s(c.rd(a))); }  ///< `FLOAT *a`.

/// true durante a corrida (bit 0x200 de `[C8C5]`).
bool raceOn(ProcCtx& c) { return (c.rd(kRaceOn) & 0x200u) != 0; }
/// `0x8EE9`: true com chance @p n em 1000.
bool chance(ProcCtx& c, uint32_t n) {
    c.cpu.ar[2] = n;
    c.call(0x8EE9);
    return c.cpu.carry();
}
/// Aleatório em [0, @p n) (`0x8EDB`).
uint32_t rnd(ProcCtx& c, uint32_t n) {
    c.cpu.ar[2] = n;
    c.call(0x8EDB);
    return c.cpu.r[0];
}
/// Aleatório float em ±@p k (`0x8ED1`).
double rndSpread(ProcCtx& c, double k) {
    setR(c, 0, k);
    c.call(0x8ED1);
    return R(c, 0);
}
/// Aleatório float em [0, @p k) (`0x8EC2`).
double rndScale(ProcCtx& c, double k) {
    setR(c, 0, k);
    c.call(0x8EC2);
    return R(c, 0);
}
/// Cria o processo `[ptr]` com o ID @p id (`0xA844`).
void spawn(ProcCtx& c, uint32_t ptr, uint32_t id) {
    c.cpu.ar[2] = c.rd(ptr);
    c.cpu.r[2] = id;
    c.call(0xA844);
}
/// Chama @p fn com AR2 = @p ar2.
void callAr2(ProcCtx& c, uint32_t fn, uint32_t ar2) {
    c.cpu.ar[2] = ar2;
    c.call(fn);
}
/// `CALL $92E2` / `$92DF` com o ângulo R2 → R0.
double trig(ProcCtx& c, uint32_t fn, uint32_t angle) {
    c.cpu.r[2] = angle;
    c.call(fn);
    return R(c, 0);
}
/// `PUSH AR4 / ADDI 1,AR4 / CALL $1F8B / POP AR4`: altura do chão sob o objeto (carry = achou).
bool ground(ProcCtx& c) {
    const uint32_t ar4 = c.cpu.ar[4];
    c.cpu.ar[4] = ar4 + 1;
    c.call(0x1F8B);
    c.cpu.ar[4] = ar4;
    return c.cpu.carry();
}

// --- Sub-rotinas --------------------------------------------------------------------------------

/// `0x69A8`: anda `[5D]` = matriz·`+11`, assenta no chão com afastamento R1 (carry = assentou).
void pushAndSettle(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = cpu.ar[4] + 0x11;
    cpu.r[2] = cpu.ar[4] + 4;
    cpu.r[3] = c.rd(kVecPtr);
    c.call(0x941E);
    cpu.ar[0] = c.rd(kVecPtr);
    c.wf(obj(c, 1), c.rf(cpu.ar[0]) + c.rf(obj(c, 1)));
    setR(c, 0, c.rf(cpu.ar[0] + 2) + c.rf(obj(c, 3)));
    c.wr(obj(c, 3), cpu.r[0]);
    const uint32_t r1 = cpu.r[1];
    const bool found = ground(c);
    cpu.r[1] = r1;
    if (!found) return;
    setR(c, 0, R(c, 0) - R(c, 1) + c.rf(obj(c, 2)));
    c.wr(obj(c, 2), cpu.r[0]);
    cpu.st |= Tms32031State::kStCarry;
}

/// `0x69C1`: AR0 = trecho até 20 à frente do jogador (`+21`); rumo do último passo em `+15` e na matriz.
void aheadOfPlayer(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(kFocusData);
    cpu.r[0] = c.rd(cpu.ar[2] + 0x37);
    cpu.ar[0] = cpu.r[0];
    cpu.ar[1] = 0x14;
    for (;;) {
        cpu.ar[2] = cpu.ar[0];
        cpu.ar[0] = cpu.r[0];
        cpu.r[0] = c.rd(cpu.ar[0] + 0x21);
        if (cpu.r[0] == 0) break;
        cpu.ar[1] -= 1;
        if (s(cpu.ar[1]) < 0) break;
    }
    const uint32_t node = cpu.ar[0];
    setR(c, 2, c.rf(node + 1) - c.rf(cpu.ar[2] + 1));
    setR(c, 3, c.rf(node + 3) - c.rf(cpu.ar[2] + 3));
    c.call(0x931A);
    setR(c, 0, R(c, 0) - c.k(0x69CF));
    cpu.r[2] = cpu.r[0];
    c.wr(obj(c, 0x15), cpu.r[2]);
    cpu.ar[2] = cpu.ar[4] + 4;
    c.call(0x938A);
    cpu.ar[0] = node;
}

/// `0x6A42`: objeto do modelo AR2 (`0x700A`) 20 trechos à frente, ±500, empurrado 750·R6 e assentado
/// com o afastamento R4. Carry = sem objeto.
void placeAhead(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    setR(c, 6, c.k(0x6A42));
    c.call(0x700A);
    if (cpu.carry()) return;
    cpu.ar[4] = cpu.ar[0];
    c.call(0xAF58);
    cpu.r[0] = c.rd(obj(c, 0xE)) | kBit23;
    c.wr(obj(c, 0xE), cpu.r[0]);
    c.wr(obj(c, 0x17), cpu.ar[7]);
    cpu.r[0] = c.var(3);
    c.wr(obj(c, 0xF), cpu.r[0]);
    aheadOfPlayer(c);
    const uint32_t node = cpu.ar[0];
    const double dx = rndSpread(c, 500.0);
    const double dz = rndSpread(c, 500.0);
    setR(c, 1, dx);
    setR(c, 2, dz);
    c.wf(obj(c, 1), c.rf(node + 1) + dx);
    c.wr(obj(c, 2), c.rd(node + 2));
    setR(c, 0, c.rf(node + 3) + dz);
    c.wr(obj(c, 3), cpu.r[0]);
    c.wf(obj(c, 0x11), 750.0 * R(c, 6));
    cpu.ar[0] = node;
    cpu.r[1] = cpu.r[4];
    pushAndSettle(c);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x7093);
    cpu.st &= ~Tms32031State::kStCarry;
}

/// `0x6C85`: AR2 = primeiro ponto de `[AA2F]` cujo trecho (palavra 3) é ≥ R2.
void pathPoint(Tms32031State& cpu, MemoryBus& bus) {
    cpu.ar[2] = bus.readWord(kPath) + 3;
    do {
        cpu.r[0] = bus.readWord(cpu.ar[2]);
        cpu.ar[2] += 4;
    } while (s(cpu.r[0]) < s(cpu.r[2]));
    cpu.ar[2] -= 7;
}

/// `0x6C76`: R2 = rumo do ponto AR2 até o ponto 2·R2 à frente (−π/2, dobrado).
void pathHeading(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[2] *= 8;
    cpu.ar[1] = cpu.ar[2] + cpu.r[2];
    setR(c, 3, static_cast<double>(s(c.rd(cpu.ar[1] + 2) - c.rd(cpu.ar[2] + 2))));
    setR(c, 2, static_cast<double>(s(c.rd(cpu.ar[1]) - c.rd(cpu.ar[2]))));
    c.call(0x931A);
    cpu.r[2] = cpu.r[0];
    c.call(0x9308);
    setR(c, 2, R(c, 2) - c.k(0x6C82));
    c.call(0x9308);
}

/// `0x6C59`: avança AR2 de 4·R2 em 4·R2 palavras enquanto o ponto seguinte fica mais perto de AR4
/// (para no início de `[AA2F]`). R2 = passos dados.
void pathAdvance(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[4] = cpu.r[2] * 4;
    cpu.r[3] = 0;
    const double x = c.rf(obj(c, 1)), z = c.rf(obj(c, 3));
    for (;;) {
        const double ax = ifl(c, cpu.ar[2]) - x, az = ifl(c, cpu.ar[2] + 2) - z;
        const double d0 = ax * ax + az * az;
        cpu.ar[0] = cpu.ar[2] + cpu.r[4];
        const double bx = ifl(c, cpu.ar[0]) - x, bz = ifl(c, cpu.ar[0] + 2) - z;
        const double d1 = bx * bx + bz * bz;
        setR(c, 0, d0);
        setR(c, 1, d1);
        if (!(d0 > d1)) break;
        cpu.ar[2] += cpu.r[4];
        if (s(cpu.ar[2]) < s(c.rd(kPath))) {
            cpu.ar[2] = c.rd(kPath);
            break;
        }
        cpu.r[3] += 1;
    }
    cpu.r[2] = cpu.r[3];
}

/// `0x6AA6`: pedaço solto: tipo 0x753, sem o bit 0x10.
void looseChunk(ProcCtx& c) {
    c.wr(obj(c, 0xF), 0x753);
    c.cpu.r[0] = c.rd(obj(c, 0xE)) & ~0x10u;
    c.wr(obj(c, 0xE), c.cpu.r[0]);
}

/// `0x6AD9`: lança AR4 na direção do carro AR5 (`+2D`, velocidade `+26` ≥ 100) e cria o processo de
/// voo `[2333]` (ID 0x290) em `+17`.
void launch(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const double f = c.rd(kAltMode) == 0 ? c.k(0x6ADA) : c.k(0x6ADB);
    setR(c, 6, f);
    const uint32_t car = cpu.ar[5];
    const double ang = rndSpread(c, c.k(0x6ADD) * f) + c.rf(car + 0x2D);
    const double up = rndScale(c, c.k(0x6AE2) * f) + c.k(0x6AE6);
    const uint32_t angW = doubleToTms(ang);
    const double side = -trig(c, 0x92E2, angW);
    const double fwd = trig(c, 0x92DF, angW);
    double speed = c.rf(car + 0x26);
    if (speed < c.k(0x6AEB)) speed = c.k(0x6AEC);
    const double mag = up * speed;
    c.wf(obj(c, 0x11), side * mag);
    c.wf(obj(c, 0x13), fwd * mag);
    double vy = (rndScale(c, c.k(0x6AF2) * f) + c.k(0x6AF5) * f) * c.k(0x6AF8) * speed;
    const double floor = c.k(0x6AFA) * f;
    if (vy < floor) vy = floor;
    setR(c, 0, vy);
    c.wr(obj(c, 0x12), cpu.r[0]);
    cpu.r[0] = kBit23;
    if (c.rd(obj(c, 0xE)) & kBit23) callAr2(c, 0xA8AC, c.rd(obj(c, 0x17)));
    cpu.ar[2] = c.rd(0x2333);
    cpu.r[2] = 0x290;
    c.call(0xA868);
    if (cpu.carry()) return;
    c.wr(obj(c, 0x17), cpu.ar[0]);
    cpu.r[0] = c.rd(obj(c, 0xE)) | (1u << 27) | kBit23 | 0x10u;
    c.wr(obj(c, 0xE), cpu.r[0]);
    c.wr(obj(c, 0x1F), 0);
}

/// `0x6A66`: estilhaça o objeto 0x750 AR1 atingido pelo carro AR5.
void shatter(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const Tms32031State saved = cpu;  // 0x9042
    cpu.ar[6] = cpu.ar[1];
    const uint32_t hit = cpu.ar[6];
    cpu.r[0] = ((c.rd(hit + 0xF) & 0xFu) - 1u) * 5u;
    cpu.ar[1] = c.rd(c.rd(0xC62B) + cpu.r[0] + 2);
    c.wr(hit + 0xF, 0x750);
    for (;;) {
        cpu.r[0] = c.rd(cpu.ar[1]++);
        if (s(cpu.r[0]) < 0) break;
        cpu.ar[2] = cpu.r[0];
        c.call(0x700A);
        if (cpu.carry()) break;
        const uint32_t list = cpu.ar[1];
        cpu.ar[4] = cpu.ar[0];
        c.call(0xAF58);
        c.wr(obj(c, 0xF), 0x750);
        for (uint32_t k = 1; k <= 3; ++k) c.wr(obj(c, k), c.rd(hit + k));
        launch(c);
        cpu.ar[2] = cpu.ar[4];
        c.call(0x7093);
        cpu.ar[1] = list;
        cpu.r[0] = c.rd(cpu.ar[1]++);
        if (cpu.r[0] != 0) looseChunk(c);
    }
    // 06A88
    cpu.ar[4] = hit;
    double r7 = -c.rf(obj(c, 0x11));
    launch(c);
    cpu.ar[2] = c.rd(0xC633);
    cpu.r[2] = 0x500;
    c.call(0xA868);
    const uint32_t car = cpu.ar[5];
    if (c.rd(kAltMode) != 0) {
        r7 -= c.rf(car + 0x46);
        double spin = c.rf(car + 0x26);
        if (r7 < 0.0) spin = -spin;
        spin = (spin + r7) * c.rf(0xC632);
        if (!(std::fabs(spin) < c.k(0x6A9A))) {
            c.wf(car + 0x2E, spin);
            c.wf(car + 0x32, c.k(0x6A9D));
            c.wr(car + 0x31, 1);
        }
    }
    c.wf(car + 0x26, c.rf(car + 0x26) * c.k(0x6AA2));
    const uint32_t st = cpu.st;
    cpu = saved;  // 0x905C
    cpu.st = st;
}

// --- Processos ----------------------------------------------------------------------------------

/// Espera do início da corrida e laço "a cada 600 quadros, chance 500, um só vivo" de `0x69E1`/`0x6A1D`.
void spawnerLoop(ProcCtx& c, uint32_t resume) { c.sleep(600, resume); }

/// Tenta criar o evento: chance 500, no máximo um processo @p id vivo (`0x69D6`); senão volta a @p loopPc.
void spawnerTry(ProcCtx& c, uint32_t loopPc, uint32_t id, uint32_t ptr) {
    if (!chance(c, 0x1F4)) return spawnerLoop(c, loopPc);
    c.cpu.r[2] = id;
    c.call(0x69D6);
    if (s(c.cpu.r[0]) >= 1) return spawnerLoop(c, loopPc);
    spawn(c, ptr, id);
    spawnerLoop(c, loopPc);
}

/// `0x69FF`: R0 = quadros até o jogador chegar (`+1C` / (média de `[C92F]`·velocidade·1.5)).
void cowWatch(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    setVarF(c, 0xC, c.k(0x6A08) + varF(c, 0xC));
    setVarF(c, 0xD, ticks(c) + varF(c, 0xD));
    cpu.r[0] = c.var(0xD);
    cpu.r[1] = c.var(0xC);
    c.call(0xA509);
    cpu.ar[5] = c.rd(kFocusData);
    setR(c, 1, R(c, 0) * c.rf(cpu.ar[5] + 0x26) * c.k(0x6A12));
    setR(c, 0, ifl(c, obj(c, 0x1C)));
    c.call(0xA509);
    if (R(c, 0) > c.k(0x6A15)) return c.sleep(1, 0x6A08);
    callAr2(c, 0x9188, 0);
    c.sleep(100, 0x6A1B);
}

/// `0x6A38`: próximo modelo da tabela `[C630]` (negativo = volta ao início), 3 quadros cada.
void animStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[6] += 1;
    cpu.r[0] = c.rd(cpu.ar[6]);
    if (s(cpu.r[0]) < 0) cpu.ar[6] = c.rd(0xC630);
    cpu.r[0] = c.rd(cpu.ar[6]);
    c.wr(obj(c, 0xD), cpu.r[0]);
    c.sleep(3, 0x6A3E);
}

/// `0x6AC9`: próximo quadro da animação AR6 do pedaço `0x6AAC` (negativo → solta e morre).
void chunkStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(cpu.ar[6]++);
    if (s(cpu.r[0]) < 0) {
        callAr2(c, 0x7100, cpu.ar[4]);
        return c.die();
    }
    c.wr(obj(c, 0xD), cpu.r[0]);
    c.call(0x908B);
    c.sleep(1, 0x6ACB);
}

/// Trecho traduzido da ROM em `0x6AAC`.
void chunkStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[6] = c.rd(0xC634);
    cpu.ar[2] = c.rd(cpu.ar[6]++);
    c.call(0x700A);
    if (cpu.carry()) return c.die();
    const uint32_t o = cpu.ar[0];
    c.wr(o + 0xE, c.rd(o + 0xE) | 0x18);
    c.wr(o + 1, c.rd(obj(c, 1)));
    c.wf(o + 2, c.rf(obj(c, 2)) + c.k(0x6AB6));
    c.wr(o + 3, c.rd(obj(c, 3)));
    cpu.ar[4] = o;
    const uint32_t ang = c.rd(cpu.ar[5] + 0x2D);
    const double side = -trig(c, 0x92E2, ang);
    const double fwd = trig(c, 0x92DF, ang);
    const double v = c.rf(cpu.ar[5] + 0x26) * c.k(0x6AC0) * c.k(0x6AC1);
    c.wf(obj(c, 0x11), side * v);
    c.wf(obj(c, 0x13), fwd * v);
    c.call(0x908B);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x7093);
    c.sleep(1, 0x6ACB);
}

/// `0x6B5E`: um quadro do objeto que segue a linha `[AA2F]`.
void walkerFrame(ProcCtx& c);
/// Trecho traduzido da ROM em `0x6BD1`.
void walkerRemove(ProcCtx& c) {
    callAr2(c, 0x7100, c.cpu.ar[4]);
    callAr2(c, 0x91C1, 0x72);
    c.die();
}

void walkerFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(cpu.ar[5]++);
    if (s(cpu.r[0]) < 0) {
        cpu.ar[5] = c.rd(0xC635);
        cpu.r[0] = c.rd(cpu.ar[5]++);
    }
    c.wr(obj(c, 0xD), cpu.r[0]);
    cpu.r[2] = c.var(0xD);
    cpu.ar[2] = cpu.ar[6];
    c.call(0x6C76);
    const double heading = R(c, 2) + varF(c, 0xE);
    setR(c, 2, heading + c.k(0x6B66));
    c.wr(obj(c, 0x15), cpu.r[2]);
    cpu.ar[2] = cpu.ar[4] + 4;
    c.call(0x938A);
    setR(c, 2, heading);
    cpu.ar[2] = c.rd(kMatA);
    c.call(0x938A);
    c.call(0x9414);
    cpu.ar[0] = c.rd(kFocusData);
    const double steps = static_cast<double>(s(c.var(0xD) * c.rd(kTicks)));
    setR(c, 1, steps);
    c.wf(cpu.ar[2] + 2, c.rf(cpu.ar[0] + 0x26) * c.k(0x6B71) * steps + varF(c, 0xC));
    cpu.r[2] = c.rd(kMatA);
    cpu.r[3] = cpu.ar[2];
    c.call(0x941E);
    c.wf(obj(c, 1), c.rf(obj(c, 1)) + c.rf(cpu.ar[2]));
    setR(c, 0, c.rf(obj(c, 3)) + c.rf(cpu.ar[2] + 2));
    c.wr(obj(c, 3), cpu.r[0]);
    if (!ground(c)) return walkerRemove(c);
    setR(c, 1, 3000.0);
    setR(c, 0, R(c, 0) - 3000.0 + c.rf(obj(c, 2)));
    c.wr(obj(c, 2), cpu.r[0]);
    cpu.ar[2] = cpu.ar[6];
    cpu.r[2] = c.var(0xD);
    c.call(0x6C59);
    cpu.ar[6] = cpu.ar[2];
    if (s(cpu.ar[6]) <= s(c.rd(kPath))) return walkerRemove(c);
    cpu.ar[2] = c.rd(c.rd(kFocusData) + 0x37);
    cpu.r[2] = c.rd(cpu.ar[2] + 0x1E);
    c.call(0x6C85);
    cpu.ar[0] = cpu.ar[2];
    cpu.ar[2] -= 0x28;
    if (s(cpu.ar[6]) <= s(cpu.ar[2])) return walkerRemove(c);
    cpu.ar[0] += 0x78;
    if (s(cpu.ar[6]) >= s(cpu.ar[0])) return walkerRemove(c);
    c.sleep(1, 0x6B9D);
}

/// `0x6B9D`: perto do jogador solta até 7 objetos `0x6BF5`; depois o quadro seguinte.
void walkerCheck(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.item(0x23) == 0 || c.rd(kAltMode) != 0 || s(c.rd(obj(c, 0x1C))) < 0) return walkerFrame(c);
    c.call(0x66BC);
    if (R(c, 0) > 5000.0) return walkerFrame(c);
    cpu.ar[2] = cpu.ar[6];
    cpu.r[2] = 1;
    c.call(0x6C76);
    setR(c, 2, -R(c, 2));
    cpu.ar[2] = c.rd(kMatA);
    c.call(0x938A);
    c.call(0x9414);
    c.wf(cpu.ar[2], c.rf(obj(c, 1)) - ifl(c, cpu.ar[6]));
    c.wf(cpu.ar[2] + 2, c.rf(obj(c, 3)) - ifl(c, cpu.ar[6] + 2));
    cpu.r[2] = c.rd(kMatA);
    cpu.r[3] = c.rd(kVecPtr);
    cpu.ar[3] = cpu.r[3];
    c.call(0x941E);
    cpu.ar[0] = c.rd(kVecPtr);
    const double side = std::fabs(c.rf(cpu.ar[0]) - c.rf(c.rd(kFocusData) + 0x46));
    setR(c, 0, side);
    if (side > 2000.0) return walkerFrame(c);
    if (!chance(c, 0x320)) return walkerFrame(c);
    cpu.r[0] = c.var(0xF);
    if (s(cpu.r[0]) > 6) return walkerFrame(c);
    c.setVar(0xF, cpu.r[0] + 1);
    cpu.r[5] = c.rd(0x6B2C);
    spawn(c, 0xC638, 0x500);
    walkerFrame(c);
}

/// Trecho traduzido da ROM em `0x6B2D`.
void walkerStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    setR(c, 4, static_cast<double>(s(cpu.r[4])));
    c.setVar(0xC, cpu.r[4]);
    c.setVar(0xD, cpu.r[5]);
    c.setVar(0xE, cpu.r[6]);
    c.setVar(0xF, 0);
    c.call(0x7025);
    if (cpu.carry()) return c.die();
    cpu.ar[4] = cpu.ar[0];
    callAr2(c, 0x9188, 0x72);
    cpu.ar[2] = c.rd(c.rd(kFocusData) + 0x37);
    cpu.r[2] = c.rd(cpu.ar[2] + 0x1E);
    c.call(0x6C85);
    cpu.ar[2] += 0x50;
    cpu.ar[6] = cpu.ar[2];
    setR(c, 0, ifl(c, cpu.ar[6]));
    c.wr(obj(c, 1), cpu.r[0]);
    if (varF(c, 0xE) != 0.0) {
        cpu.r[2] = c.var(0xD);
        cpu.ar[2] = cpu.ar[6];
        c.call(0x6C76);
        setR(c, 2, R(c, 2) + c.k(0x6B46));
        cpu.ar[2] = c.rd(kMatA);
        c.call(0x938A);
        c.call(0x9414);
        c.wf(cpu.ar[2] + 2, 2500.0);
        cpu.r[2] = c.rd(kMatA);
        cpu.r[3] = cpu.ar[2];
        c.call(0x941E);
        c.wf(obj(c, 1), c.rf(obj(c, 1)) + c.rf(cpu.ar[2]));
        c.wf(obj(c, 3), c.rf(obj(c, 3)) + c.rf(cpu.ar[2] + 2));
    }
    // 06B55: a altura vem do ponto − 3000 e o z do ponto (o deslocamento lateral em z se perde)
    setR(c, 1, -3000.0);
    c.wf(obj(c, 2), ifl(c, cpu.ar[6] + 1) - 3000.0);
    setR(c, 0, ifl(c, cpu.ar[6] + 2));
    c.wr(obj(c, 3), cpu.r[0]);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x7093);
    cpu.ar[5] = c.rd(0xC635);
    walkerFrame(c);
}

// `0x6BF5`: objeto de tela 0x103.
/// Trecho traduzido da ROM em `0x6C4D`.
void diverEnd(ProcCtx& c) {
    callAr2(c, 0x7100, c.cpu.ar[4]);
    callAr2(c, 0x91C1, 0x24);
    c.die();
}

/// Trecho traduzido da ROM em `0x6C0F`.
void diverFall(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kAltMode) != 0) return diverEnd(c);
    double dz = ticks(c) * c.rf(cpu.ar[5] + 0x26) * c.k(0x6C13);
    if (dz > c.k(0x6C14)) dz = c.k(0x6C15);
    c.wf(obj(c, 2), -dz * R(c, 6) + c.rf(obj(c, 2)));
    double z = dz + c.rf(obj(c, 3));
    const bool above = z > 368.0;
    if (z < 368.0) z = 368.0;
    setR(c, 0, z);
    c.wr(obj(c, 3), cpu.r[0]);
    if (above) return c.sleep(1, 0x6C0F);
    callAr2(c, 0x91C1, 0x24);
    callAr2(c, 0x9188, 0x6C);
    cpu.r[4] = c.rd(cpu.ar[6]++);
    c.wr(obj(c, 0xD), cpu.r[4]);
    c.sleep(2, 0x6C28);
}

/// Trecho traduzido da ROM em `0x6C28`.
void diverAnim(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kAltMode) != 0) return diverEnd(c);
    cpu.r[4] = c.rd(cpu.ar[6]++);
    if (s(cpu.r[4]) > 0) {
        c.wr(obj(c, 0xD), cpu.r[4]);
        return c.sleep(2, 0x6C28);
    }
    cpu.r[2] = c.rd(obj(c, 1));
    cpu.r[3] = c.rd(obj(c, 2));
    c.call(0x931A);
    const uint32_t a = cpu.r[0];
    c.wf(obj(c, 0x11), trig(c, 0x92DF, a));
    c.wf(obj(c, 0x12), trig(c, 0x92E2, a));
    c.sleep(1, 0x6C36);
}

/// Trecho traduzido da ROM em `0x6C36`.
void diverExit(ProcCtx& c) {
    if (c.rd(kAltMode) != 0) return diverEnd(c);
    const double t = ticks(c);
    const double vx = c.rf(obj(c, 0x11)) * c.k(0x6C3A) + c.rf(obj(c, 0x11));
    c.wf(obj(c, 0x11), vx);
    const double x = vx * t + c.rf(obj(c, 1));
    c.wf(obj(c, 1), x);
    const double vy = c.rf(obj(c, 0x12)) * c.k(0x6C41) + c.rf(obj(c, 0x12));
    c.wf(obj(c, 0x12), vy);
    const double y = vy * t + c.rf(obj(c, 2));
    c.wf(obj(c, 2), y);
    setR(c, 0, std::fabs(x));
    setR(c, 1, y);
    // sai da tela: |x| > 300 ou y > 300
    if (std::fabs(x) > 300.0 || y > 300.0) return diverEnd(c);
    c.sleep(1, 0x6C36);
}

/// Trecho traduzido da ROM em `0x6BF5`.
void diverStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[5] = c.rd(kFocusData);
    cpu.ar[6] = cpu.r[5];
    cpu.ar[2] = c.rd(cpu.ar[6]++);
    c.call(0x700A);
    cpu.ar[4] = cpu.ar[0];
    c.wr(obj(c, 0xF), 0x103);
    c.wf(obj(c, 1), rndSpread(c, c.k(0x6BFC)));
    c.wf(obj(c, 2), -300.0);
    c.wf(obj(c, 3), 2000.0);
    c.wr(obj(c, 0xE), c.rd(obj(c, 0xE)) | 0xBB);
    cpu.ar[2] = cpu.ar[4];
    c.call(0x7070);
    setR(c, 6, (rndScale(c, c.k(0x6C08)) + c.k(0x6C0A)) * c.k(0x6C0C));
    c.sleep(1, 0x6C0F);
}

/// Registra as rotinas auxiliares dos eventos de beira de estrada.
void registerHelpers(SubroutineDispatcher& d) {
    addProc(d, 0x69A8, pushAndSettle);
    addProc(d, 0x69C1, aheadOfPlayer);
    addProc(d, 0x6A40, [](ProcCtx& c) {
        c.cpu.ar[6] = c.rd(0xC630);
        c.cpu.ar[2] = c.rd(c.cpu.ar[6]);
        placeAhead(c);
    });
    addProc(d, 0x6A42, placeAhead);
    addProc(d, 0x6A66, shatter);
    addProc(d, 0x6AA6, looseChunk);
    addProc(d, 0x6AD3, [](ProcCtx& c) {
        const Tms32031State saved = c.cpu;  // 0x9042
        c.cpu.ar[4] = c.cpu.ar[1];
        launch(c);
        looseChunk(c);
        const uint32_t st = c.cpu.st;
        c.cpu = saved;  // 0x905C
        c.cpu.st = st;
    });
    addProc(d, 0x6AD9, launch);
    d.add(0x6C85, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { pathPoint(c, b); });
    addProc(d, 0x6C76, pathHeading);
    addProc(d, 0x6C59, pathAdvance);
}

/// Registra os processos dos eventos de cada pista.
void registerProcesses(SubroutineDispatcher& d) {
    // 0x69E1 (pista 11)
    addProc(d, 0x69E1, [](ProcCtx& c) {
        if (c.item(0x23) == 0) return c.die();
        c.cpu.ar[2] = 0xB4;
        c.call(0x9ECD);
        if (!c.cpu.carry()) {
            c.cpu.r[2] = c.cpu.r[0];
            c.cpu.ar[2] = c.rd(0xC62D);
            c.cpu.r[3] = c.rd(c.cpu.ar[2]++);
            c.call(0x9F5C);
        }
        c.sleep(1, 0x69EE);
    });
    addProc(d, 0x69EE, [](ProcCtx& c) {
        if (!raceOn(c)) return c.sleep(1, 0x69EE);
        spawnerLoop(c, 0x69F3);
    });
    addProc(d, 0x69F3, [](ProcCtx& c) { spawnerTry(c, 0x69F3, 0x751, 0xC62E); });
    addProc(d, 0x69FF, [](ProcCtx& c) {
        c.cpu.ar[2] = c.rd(0xC62F);
        setR(c, 4, 10.0);
        c.call(0x6A42);
        if (c.cpu.carry()) return c.die();
        setVarF(c, 0xC, 0.0);
        setVarF(c, 0xD, 0.0);
        c.sleep(1, 0x6A08);
    });
    addProc(d, 0x6A08, cowWatch);
    addProc(d, 0x6A1B, [](ProcCtx& c) { c.sleep(100, 0x6A1B); });
    // 0x6A1D (pista 9)
    addProc(d, 0x6A1D, [](ProcCtx& c) {
        if (c.item(0x23) == 0) return c.die();
        c.sleep(1, 0x6A23);
    });
    addProc(d, 0x6A23, [](ProcCtx& c) {
        if (!raceOn(c)) return c.jump(0x6A1D);
        spawnerLoop(c, 0x6A28);
    });
    addProc(d, 0x6A28, [](ProcCtx& c) { spawnerTry(c, 0x6A28, 0x752, 0xC631); });
    addProc(d, 0x6A34, [](ProcCtx& c) {
        setR(c, 4, 219.0);
        c.call(0x6A40);
        if (c.cpu.carry()) return c.die();
        c.cpu.ar[6] = c.rd(0xC630);
        animStep(c);
    });
    addProc(d, 0x6A3E, animStep);
    addProc(d, 0x6AAC, chunkStart);
    addProc(d, 0x6ACB, chunkStep);
    // 0x6B14 (pistas 9 e 11)
    auto walkerSpawner = [](ProcCtx& c) {
        if (c.rd(kFocusCar) == 0 || !raceOn(c)) return c.sleep(1, 0x6B16);
        c.sleep(600, 0x6B1D);
    };
    addProc(d, 0x6B14, [](ProcCtx& c) { c.sleep(1, 0x6B16); });
    addProc(d, 0x6B16, walkerSpawner);
    addProc(d, 0x6B1D, [](ProcCtx& c) {
        Tms32031State& cpu = c.cpu;
        if (chance(c, 0x1F4)) {
            cpu.r[0] = rnd(c, 4) * 3;
            cpu.ar[0] = c.rd(0xC636) + cpu.r[0];
            cpu.r[4] = c.rd(cpu.ar[0]++);
            cpu.r[5] = c.rd(cpu.ar[0]++);
            cpu.r[6] = c.rd(cpu.ar[0]++);
            spawn(c, 0xC637, 0x500);
        }
        c.sleep(600, 0x6B1D);
    });
    addProc(d, 0x6B2D, walkerStart);
    addProc(d, 0x6B9D, walkerCheck);
    // 0x6BD7 (pistas 9 e 11)
    addProc(d, 0x6BD7, [](ProcCtx& c) {
        if (c.item(0x23) == 0) return c.die();
        c.sleep(1, 0x6BDD);
    });
    addProc(d, 0x6BDD, [](ProcCtx& c) {
        if (!raceOn(c)) return c.jump(0x6BD7);
        c.sleep(150, 0x6BE2);
    });
    addProc(d, 0x6BE2, [](ProcCtx& c) {
        if (c.rd(kAltMode) != 0 || !chance(c, 0x12C)) return c.sleep(150, 0x6BE2);
        callAr2(c, 0x9188, 0x24);
        callAr2(c, 0x9188, 0x24);
        c.sleep(10, 0x6BED);
    });
    addProc(d, 0x6BED, [](ProcCtx& c) {
        c.cpu.r[5] = c.rd(0x6BD6);
        spawn(c, 0xC639, 0x500);
        c.sleep(600, 0x6BF3);
    });
    addProc(d, 0x6BF3, [](ProcCtx& c) { c.sleep(150, 0x6BE2); });
    addProc(d, 0x6BF5, diverStart);
    addProc(d, 0x6C0F, diverFall);
    addProc(d, 0x6C28, diverAnim);
    addProc(d, 0x6C36, diverExit);
}

} // namespace

void registerRoadsideEvents(SubroutineDispatcher& d) {
    registerHelpers(d);
    registerProcesses(d);
}

} // namespace rea::core::game
