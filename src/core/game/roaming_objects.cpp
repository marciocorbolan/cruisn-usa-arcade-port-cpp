/**
 * @file roaming_objects.cpp
 * @brief Tradução de `0x9B43`–`0x9CDF`, `0xC101A4`–`0xC10252`, `0x7EE9`, `0x9032`, `0x676B` e
 *        `0x4440`–`0x44BA`.
 */
#include "core/game/roaming_objects.hpp"

#include <cmath>

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kBit23 = 1u << 23;  ///< Bit 23 da palavra de flags do objeto (`+0xE`).
constexpr uint32_t kLinked = 0xC8E3;  ///< 1 = corrida com link em andamento (tipo 5); 0 nos tipos 6 e 8.
constexpr uint32_t kGo = 0xE8E6;       ///< ≠ 0: o grupo anda.
constexpr uint32_t kTargetX = 0xE8E7;  ///< Alvo do líder (x, z).
constexpr uint32_t kTargetZ = 0xE8E9;  ///< z do alvo do bando.
constexpr uint32_t kSpeed = 0xE8EA;    ///< Velocidade do grupo.
constexpr uint32_t kGone = 0xE8EB;     ///< ≠ 0: o grupo some.
constexpr uint32_t kTicks = 0xC92F;  ///< Quadros (VBLANKs) gastos na última iteração do loop principal.
constexpr uint32_t kMatPtr = 0x005A;  ///< `[0x5A]`: matriz de rascunho.
constexpr uint32_t kVecPtr = 0x005D;  ///< `[0x5D]`: vetor de rascunho.
constexpr uint32_t kFarLimit = static_cast<uint32_t>(-6000);  ///< `CMPI −$1770,*+AR4(1C)`.
constexpr uint32_t kHerdSound = 0x114;  ///< Som do bando.

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

/// Marca o objeto @p obj como ocupado (bit 23 de `+0xE`).
void setBusy(ProcCtx& c, uint32_t obj) { c.wr(obj + 0xE, c.rd(obj + 0xE) | kBit23); }
/// Desmarca o objeto @p obj.
void clearBusy(ProcCtx& c, uint32_t obj) { c.wr(obj + 0xE, c.rd(obj + 0xE) & ~kBit23); }

/// `LDF k,R0 / CALL $8ED1` → aleatório em [−k, +k).
double randSpread(ProcCtx& c, double k) {
    setR(c, 0, k);
    c.call(0x8ED1);
    return R(c, 0);
}
/// `LDF k,R0 / CALL $8EC2` → k · aleatório(0..1).
double randScale(ProcCtx& c, double k) {
    setR(c, 0, k);
    c.call(0x8EC2);
    return R(c, 0);
}

/// `+1C` < −6000: o objeto ficou para trás.
bool behind(ProcCtx& c) { return s(c.rd(c.cpu.ar[4] + 0x1C)) < s(kFarLimit); }

/// `LDI ptr,AR2 / LDI id,R2 / CALL $A844 / STI AR0,*+AR4(17)` + bit 23.
void spawnFor(ProcCtx& c, uint32_t pcPtr, uint32_t id) {
    c.cpu.ar[2] = c.rd(pcPtr);
    c.cpu.r[2] = id;
    c.call(0xA844);
    c.wr(c.cpu.ar[4] + 0x17, c.cpu.ar[0]);
    setBusy(c, c.cpu.ar[4]);
}

// --- Listas e busca ----------------------------------------------------------------------------

/// `0x7EE9`: AR0 = objeto de tipo 0x300 da lista `[E403]` mais perto (x, z) de AR4.
void nearestRoad(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[0] = bus.readWord(0xE403);
    if (cpu.r[0] == 0) return;
    cpu.ar[0] = 0;
    double best = 32767.0 * 32767.0;
    const double x = tmsToDouble(bus.readWord(cpu.ar[4] + 1)), z = tmsToDouble(bus.readWord(cpu.ar[4] + 3));
    for (uint32_t o = cpu.r[0]; o != 0; o = bus.readWord(o + 0x20)) {
        if ((bus.readWord(o + 0xF) & 0xFF0u) != 0x300) continue;
        const double dx = x - tmsToDouble(bus.readWord(o + 1)), dz = z - tmsToDouble(bus.readWord(o + 3));
        const double d = dx * dx + dz * dz;
        cpu.r[1] = doubleToTms(d);
        if (best > d) {
            cpu.ar[0] = o;
            best = d;
        }
    }
    cpu.r[2] = doubleToTms(best);
    cpu.r[3] = bus.readWord(cpu.ar[4] + 1);
    cpu.r[4] = bus.readWord(cpu.ar[4] + 3);  // R4 é sobrescrito pela rotina original
    cpu.ar[2] = 0;
    cpu.r[0] = 0;
}

/// `0x9032`: devolve o bloco de dados de carro AR2 à lista livre `[E3F6]` e decrementa `[E3F7]`.
void freeCarData(Tms32031State& cpu, MemoryBus& bus) {
    bus.writeWord(cpu.ar[2], bus.readWord(0xE3F6));
    bus.writeWord(0xE3F6, cpu.ar[2]);
    bus.writeWord(0xE3F7, bus.readWord(0xE3F7) - 1);
}

/// `0x676B`: tira AR4 da lista ligada por `+20` cuja cabeça é `[[0x57]]` (`[E5EE]`−−).
void unlinkMoving(Tms32031State& cpu, MemoryBus& bus) {
    uint32_t prev = bus.readWord(0x57) - 0x20;
    for (;;) {
        const uint32_t next = bus.readWord(prev + 0x20);
        if (next == 0) return;
        if (next == cpu.ar[4]) {
            bus.writeWord(prev + 0x20, bus.readWord(cpu.ar[4] + 0x20));
            bus.writeWord(0xE5EE, bus.readWord(0xE5EE) - 1);
            return;
        }
        prev = next;
    }
}

// --- Grupo 0x260/0x261 -------------------------------------------------------------------------

void herdLoop(ProcCtx& c);
void bumpLoop(ProcCtx& c);
void rollState(ProcCtx& c);

/// `0x9CBA`: sai da lista, solta o objeto e os dados de carro e morre.
void herdExit(ProcCtx& c) {
    c.call(0x676B);
    clearBusy(c, c.cpu.ar[4]);
    c.cpu.ar[2] = c.cpu.ar[4];
    c.call(0x7100);
    c.cpu.ar[2] = c.cpu.ar[5];
    c.call(0x9032);
    c.die();
}

/// `0x9CDF` (→ `0xC10248`): matriz do objeto = rotZ(`*+AR7(F)`) × rotY(`*+AR7(10)`).
void tumbleMatrix(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = cpu.ar[4] + 4;
    cpu.r[2] = c.var(0xF);
    c.call(0x93C4);
    cpu.ar[2] = c.rd(kMatPtr);
    cpu.r[2] = c.var(0x10);
    c.call(0x938A);
    cpu.r[2] = cpu.ar[4] + 4;
    cpu.r[3] = cpu.r[2];
    c.call(0x9494);
}

/// Velocidade horizontal pelo rumo: `+13` = cos(`+2D` + π/2)·`+26`, `+11` = sen(…)·`+26`.
void headingVelocity(ProcCtx& c, uint32_t halfPiPc) {
    Tms32031State& cpu = c.cpu;
    setR(c, 2, c.rf(cpu.ar[5] + 0x2D) + c.k(halfPiPc));
    c.call(0x92E2);
    const double sp = c.rf(cpu.ar[5] + 0x26);
    c.wf(cpu.ar[4] + 0x13, R(c, 0) * sp);
    c.call(0x92DF);
    c.wf(cpu.ar[4] + 0x11, R(c, 0) * sp);
}

/// `R3 = *+AR4(1B) / CALL $2041 / AR5 = *+AR4(1B)`.
void groundPoints(ProcCtx& c) {
    c.cpu.r[3] = c.rd(c.cpu.ar[4] + 0x1B);
    c.call(0x2041);
    c.cpu.ar[5] = c.rd(c.cpu.ar[4] + 0x1B);
}

/// `0x9B4E`: prepara o objeto como carro do grupo e entra no laço `0x9B74`.
void herdStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t obj = cpu.ar[4];
    cpu.ir0 = c.rd(obj + 0xF) & 0xFu;
    c.wr(obj + 0x1E, cpu.ir0);
    cpu.r[0] = 0xD;
    c.call(0x2945);
    if (!cpu.carry()) {
        clearBusy(c, obj);
        return c.die();
    }
    // 09B5A
    cpu.ar[5] = cpu.ar[0];
    cpu.r[0] = 0x260;
    c.wr(obj + 0xF, cpu.r[0]);
    c.wr(cpu.ar[5] + 0x40, cpu.r[0]);
    c.setVar(3, cpu.r[0]);
    c.call(0x6762);
    cpu.r[0] = c.rd(obj + 0x1F) + 0x500;
    c.setVar(0xE, cpu.r[0]);
    c.wr(obj + 0x1F, 0);
    c.wf(cpu.ar[5] + 0x2B, 0.0);
    c.wr(cpu.ar[5] + 0x45, c.rd(cpu.ar[5] + 0x44));
    cpu.r[2] = c.rd(obj + 0x15);
    c.call(0x9308);
    c.wr(obj + 0x15, cpu.r[2]);
    c.wr(cpu.ar[5] + 0x2C, cpu.r[2]);
    c.wr(cpu.ar[5] + 0x2D, cpu.r[2]);
    cpu.ar[2] = obj + 4;
    c.call(0x938A);
    setVarF(c, 0x17, c.rf(0x9B42) * 2.0);
    herdLoop(c);
}

/// `0x9B49`: seguidor — vai para logo depois do líder (ID 0x261) e segue como `0x9B4E`.
void followerStart(ProcCtx& c) {
    c.cpu.r[0] = 0x261;
    c.cpu.r[1] = 0xFFFFFFFFu;
    c.call(0xA912);
    c.cpu.ar[2] = c.cpu.ar[0];
    c.call(0xA91F);
    herdStart(c);
}

/// `x² + z²` até o alvo e `CALL $A5F1` → distância.
double distToTarget(ProcCtx& c, uint32_t obj) {
    const double dx = c.rf(kTargetX) - c.rf(obj + 1);
    const double dz = c.rf(kTargetZ) - c.rf(obj + 3);
    setR(c, 2, dx * dx + dz * dz);
    c.call(0xA5F1);
    return R(c, 0);
}

/// `0x9B7E`–`0x9BC4`: o líder regula a velocidade e toca os sons do grupo.
void leaderPace(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const double d = distToTarget(c, cpu.ar[4]);
    const double before = varF(c, 0x17);
    setVarF(c, 0x17, d);
    if (d > before && !(d < 15000.0 * c.k(0x9B8C))) {
        c.wr(kGone, 1);
        cpu.ar[2] = kHerdSound;
        c.call(0x91C1);
    }
    // 09B93  tempo do jogador até o alvo × tempo do grupo
    cpu.ar[0] = c.rd(0xE876);
    double tPlayer = distToTarget(c, cpu.ar[0]);
    cpu.ar[0] = c.rd(0xE878);
    setR(c, 0, tPlayer);
    setR(c, 1, c.rf(cpu.ar[0] + 0x26));
    c.call(0xA509);
    tPlayer = R(c, 0);
    setR(c, 0, d + 25000.0);
    setR(c, 1, c.rf(kSpeed));
    c.call(0xA509);
    const double tHerd = R(c, 0);
    if (tHerd < tPlayer) {
        // 09BB3  mais devagar (mínimo 170)
        double v = c.rf(kSpeed) * c.k(0x9BB4);
        if (v < 170.0) v = 170.0;
        c.wf(kSpeed, v);
    } else if (tHerd != tPlayer) {
        // 09BA9  mais depressa (máximo 280)
        double v = c.k(0x9BAC) - randScale(c, c.k(0x9BA9) * c.k(0x9BAA));
        v *= c.rf(kSpeed);
        if (v > 280.0) v = 280.0;
        c.wf(kSpeed, v);
    }
    // 09BB9  sons
    cpu.ar[2] = kHerdSound;
    c.call(0x3143);
    cpu.ar[2] = 1000;
    c.call(0x8EDB);
    if (s(cpu.r[0]) > 5) return;
    cpu.ar[2] = 2;
    c.call(0x8EDB);
    cpu.ar[2] = cpu.r[0] == 0 ? 0x117u : 0x11Au;
    c.call(0x3143);
}

/// `0x9B74`: um quadro do grupo.
void herdLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t obj = cpu.ar[4];
    cpu.r[0] = c.var(0xE) >> 8;
    cpu.r[1] = c.rd(0xE474) - c.rd(0xE46C);
    if (s(cpu.r[0]) < s(cpu.r[1])) return herdExit(c);
    if (c.rd(kGo) != 0) {
        if (c.var(0xC) != 0) leaderPace(c);
        // 09BC5
        if (c.rd(kGone) != 0) return herdExit(c);
        cpu.r[2] = c.rd(obj + 0x15);
        cpu.ar[2] = c.rd(kMatPtr);
        c.call(0x938A);
        c.call(0x9414);
        const uint32_t v = cpu.ar[2];
        c.wf(v + 2, c.rf(kSpeed) * ticks(c));
        cpu.r[3] = v;
        cpu.r[2] = c.rd(kMatPtr);
        c.call(0x941E);
        for (uint32_t i = 0; i < 3; ++i) {
            c.wr(obj + 0x11 + i, c.rd(v + i));
            c.wf(obj + 1 + i, c.rf(v + i) + c.rf(obj + 1 + i));
        }
        c.call(0x7EE9);
        c.wr(cpu.ar[5] + 0x37, cpu.ar[0]);
        cpu.r[2] = c.rd(obj + 0x15);
        c.wr(cpu.ar[5] + 0x2C, cpu.r[2]);
        c.wr(cpu.ar[5] + 0x2D, cpu.r[2]);
        c.wr(cpu.ar[5] + 0x26, c.rd(kSpeed));
        c.wf(cpu.ar[5] + 0x30, 2.0);
    }
    c.sleep(1, 0x9BE9);
}

// --- Derrubado: 0x9BF1 --------------------------------------------------------------------------

/// `0x9BF1`: giros aleatórios e voo.
void bumpStart(ProcCtx& c) {
    setVarF(c, 0xC, randSpread(c, c.k(0x9BF1)));
    setVarF(c, 0xD, randSpread(c, c.k(0x9BF4)));
    setVarF(c, 0xE, 0.0);
    setVarF(c, 0xF, 0.0);
    setVarF(c, 0x11, 0.0);
    c.setVar(0x10, c.rd(c.cpu.ar[5] + 0x2C));
    bumpLoop(c);
}

/// `0x9BFE`: um quadro do voo (gravidade +2/quadro; quica no chão abaixo de 35).
void bumpLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t obj = cpu.ar[4];
    if (behind(c)) return herdExit(c);
    groundPoints(c);
    const double t = ticks(c);
    setVarF(c, 0xF, varF(c, 0xC) * t + varF(c, 0xF));
    setVarF(c, 0x10, varF(c, 0xD) * t + varF(c, 0x10));
    c.call(0x9CDF);
    headingVelocity(c, 0x9C0F);
    c.call(0x908B);
    const double vy = t * 2.0 + c.rf(obj + 0x12);
    c.wf(obj + 0x12, vy);
    if (vy > 300.0) return herdExit(c);
    c.call(0x2EFD);
    if (c.rf(obj + 0x12) >= 0.0) {
        c.call(0x1FB6);
        if (cpu.carry() && !(R(c, 0) > 0.0)) {
            c.wf(obj + 2, R(c, 0) + c.rf(obj + 2));
            const double fall = c.rf(obj + 0x12);
            if (fall < 35.0) return rollState(c);
            c.wf(obj + 0x12, fall * -0.5);
            c.wf(cpu.ar[5] + 0x26, c.rf(cpu.ar[5] + 0x26) * 0.5);
            setVarF(c, 0xC, varF(c, 0xC) * 0.5);
            setVarF(c, 0xD, varF(c, 0xD) * 0.5);
            cpu.ar[2] = c.rd(0x3085);
            cpu.r[0] = 3;
            c.call(0x3168);
        }
    }
    c.sleep(1, 0x9C3B);
}

/// `0x9C52`–`0x9C99`: um passo da rolagem. Devolve −1 para seguir, 1 (de lado) ou 2 (de pé).
int rollStep(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t obj = cpu.ar[4], car = cpu.ar[5];
    double v = c.rf(obj + 0x11) * c.k(0x9C53);
    c.wf(obj + 0x11, v);
    c.wf(obj + 1, v + c.rf(obj + 1));
    c.wf(obj + 2, c.rf(obj + 0x12) + c.rf(obj + 2));
    v = c.rf(obj + 0x13) * c.k(0x9C5B);
    c.wf(obj + 0x13, v);
    c.wf(obj + 3, v + c.rf(obj + 3));
    v = varF(c, 0xC) * c.k(0x9C60);
    setVarF(c, 0xC, v);
    setVarF(c, 0xF, v + varF(c, 0xF));
    v = varF(c, 0xD) * c.k(0x9C65);
    setVarF(c, 0xD, v);
    c.wf(car + 0x2E, v);
    setVarF(c, 0x10, v + varF(c, 0x10));
    cpu.r[2] = c.var(0xF);
    c.call(0x9308);
    const double roll = R(c, 2);
    const double absRoll = std::fabs(roll);
    const double speed = c.rf(car + 0x26) * c.k(0x9C6E);
    c.wf(car + 0x26, speed);
    if (!(speed > 10.0)) {
        const double spin = (std::fabs(c.rf(car + 0x2E)) + std::fabs(varF(c, 0xC))) * 10.0;
        if (!(spin > c.k(0x9C76))) {
            if (absRoll > c.k(0x9C78)) {
                if (!(absRoll < c.k(0x9C7E))) return 1;  // 09C80  de lado (capotado)
            } else {
                setVarF(c, 0xF, 0.0);  // 09C7A  de pé
                return 2;
            }
        }
    }
    // 09C82  empurra o giro para o lado em que o objeto vai cair
    const double lim = roll >= 0.0 ? c.k(0x9C84) : c.k(0x9C86);
    double r0 = roll < lim ? c.k(0x9C87) : c.k(0x9C88);
    r0 = r0 * c.k(0x9C89) + varF(c, 0xC);
    const double r1 = varF(c, 0xC);
    if ((r0 < 0.0) != (r1 < 0.0)) {
        cpu.r[2] = doubleToTms(std::fabs(varF(c, 0xF)));
        c.call(0x9308);
        const double a = std::fabs(R(c, 2));
        if (!(a < c.k(0x9C91)) && !(a > c.k(0x9C93))) {
            cpu.ar[2] = 0x1EF;
            c.call(0x316E);
        }
    }
    setVarF(c, 0xC, r0);
    return -1;
}

void sideWait(ProcCtx& c);

/// `0x9CC5`: de pé — volta a ser um carro (tipo 0x2x0) que só desacelera (`0x2C18`).
void uprightStart(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t obj = cpu.ar[4], car = cpu.ar[5];
    cpu.r[0] = (c.rd(obj + 0xF) & ~0xF0u) | 0x20u;
    c.wr(obj + 0xF, cpu.r[0]);
    c.wr(car + 0x40, cpu.r[0]);
    c.setVar(3, cpu.r[0]);
    c.wf(car + 0x24, 0.0);
    c.wf(car + 0x2E, 0.0);
    c.wf(car + 0x26, 0.0);
    c.wf(car + 0x2B, 1.0);
    c.wr(car + 0x2D, c.var(0x10));
    c.wr(car + 0x2C, c.var(0x10));
    c.wr(car + 0x31, 0);
    // 09CD6
    if (behind(c)) return herdExit(c);
    setR(c, 2, 0.0);
    c.call(0x2C18);
    c.call(0x2EFD);
    c.sleep(1, 0x9CDE);
}

/// `0x9CDE`: laço do animal em pé; para quando fica para trás.
void uprightLoop(ProcCtx& c) {
    if (behind(c)) return herdExit(c);
    setR(c, 2, 0.0);
    c.call(0x2C18);
    c.call(0x2EFD);
    c.sleep(1, 0x9CDE);
}

/// `0x9C3C`: rolagem amortecida no chão, `[C92F]` passos por quadro.
void rollState(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t obj = cpu.ar[4];
    if (behind(c)) return herdExit(c);
    c.wf(obj + 0x12, 500.0);
    headingVelocity(c, 0x9C42);
    c.wr(cpu.ar[7] + 0xD, c.rd(cpu.ar[5] + 0x2E));
    groundPoints(c);
    int32_t rc = s(c.rd(kTicks)) - 1;
    int code = 0;
    do {
        const int r = rollStep(c);
        if (r >= 0) {
            code = r;
            break;
        }
    } while (--rc >= 0);
    // 09C9B
    c.call(0x2EFD);
    c.call(0x9CDF);
    c.call(0x1FB6);
    if (cpu.carry()) {
        c.wf(obj + 2, R(c, 0) + c.rf(obj + 2));
        if (code == 0) return c.sleep(1, 0x9CA7);
    }
    // 09CA8
    if (code == 2) return uprightStart(c);
    c.wf(cpu.ar[5] + 0x26, 0.0);  // (o tipo |= 0x20 é calculado e descartado no original)
    c.sleep(1, 0x9CB1);
}

/// `0x9CB1`: parado de lado até ser empurrado de novo.
void sideWait(ProcCtx& c) {
    if (behind(c)) return herdExit(c);
    if (c.rf(c.cpu.ar[5] + 0x26) == 0.0) return c.sleep(1, 0x9CB1);
    setVarF(c, 0xC, randSpread(c, c.k(0x9CB6)));
    rollState(c);
}

// --- Tratadores e batida (ROM alta) ------------------------------------------------------------

/// `0xC101A4`: cria o líder e marca o alvo do grupo. Preserva R0–R3, AR0 e AR2.
void leaderSpawn(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const Tms32031State saved = cpu;
    const uint32_t obj = cpu.ar[4];
    c.wr(kGo, 0);
    spawnFor(c, 0xC628, 0x261);
    c.wr(cpu.ar[0] + 0xC, 1);
    const uint32_t v = c.rd(0x5E);
    c.wf(v, 0.0);
    c.wf(v + 1, 0.0);
    c.wr(v + 2, c.rd(0x9B42));
    cpu.r[2] = c.rd(obj + 0x15);
    cpu.ar[2] = c.rd(0x5B);
    c.call(0x938A);
    cpu.r[2] = cpu.ar[2];
    cpu.ar[2] = v;
    cpu.r[3] = v;
    c.call(0x941E);
    c.wf(kTargetX, c.rf(v) + c.rf(obj + 1));
    c.wf(kTargetZ, c.rf(v + 2) + c.rf(obj + 3));
    c.wf(kSpeed, 240.0);
    for (int i = 0; i < 4; ++i) cpu.r[i] = saved.r[i];
    cpu.ar[0] = saved.ar[0];
    cpu.ar[2] = saved.ar[2];
}

/// `0xC101DA`: cria um seguidor (`+C` = 0). Preserva R0, R2 e AR2.
void followerSpawn(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t r0 = cpu.r[0], r2 = cpu.r[2], ar2 = cpu.ar[2];
    spawnFor(c, 0xC629, 0x260);
    c.wr(cpu.ar[0] + 0xC, 0);
    cpu.r[0] = r0;
    cpu.r[2] = r2;
    cpu.ar[2] = ar2;
}

/// `0xC101FB`: AR0 (A) bateu no objeto do grupo AR1 (B). Preserva AR0, AR1, AR3, AR4 e AR5.
void herdBumped(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar0 = cpu.ar[0], ar1 = cpu.ar[1], ar3 = cpu.ar[3], ar4 = cpu.ar[4], ar5 = cpu.ar[5];
    c.sound(0x1EC);
    cpu.ar[3] = c.rd(cpu.ar[0] + 0x1B);
    cpu.ar[5] = c.rd(cpu.ar[1] + 0x1B);
    if (!(c.rf(cpu.ar[3] + 0x26) > 150.0)) {
        c.wr(cpu.ar[5] + 0x33, 1);
    } else {
        for (;;) {
            // 1020B
            cpu.ar[0] = c.rd(0xE876);
            if ((c.rd(cpu.ar[1] + 0xE) & kBit23) != 0 && c.rd(cpu.ar[1] + 0x17) != 0) {
                cpu.ar[2] = c.rd(cpu.ar[1] + 0x17);
                c.call(0xA8AC);
                c.wr(cpu.ar[1] + 0x17, 0);
            }
            cpu.ar[3] = c.rd(cpu.ar[0] + 0x1B);
            cpu.ar[5] = c.rd(cpu.ar[1] + 0x1B);
            cpu.ar[4] = cpu.ar[1];
            c.wf(cpu.ar[5] + 0x2D, randScale(c, c.k(0xC10219)) + c.rf(cpu.ar[5] + 0x2D));
            double up = (randScale(c, c.k(0xC1021D)) + c.k(0xC1021F)) * c.k(0xC10220);
            up *= c.rf(cpu.ar[3] + 0x26);
            if (up < c.k(0xC10222)) up = c.k(0xC10223);
            c.wf(cpu.ar[1] + 0x12, up);
            cpu.ar[2] = c.rd(0x9BF0);
            c.call(0xA868);
            c.wr(cpu.ar[1] + 0x17, cpu.ar[0]);
            setBusy(c, cpu.ar[1]);
            cpu.r[0] = c.rd(cpu.ar[1] + 0x20);
            if (cpu.r[0] == 0) break;
            cpu.ar[1] = cpu.r[0];
            if (c.rd(cpu.ar[1] + 0xF) != 0x260) break;
        }
        // 10232
        c.wf(cpu.ar[3] + 0x26, c.rf(cpu.ar[3] + 0x26) * 0.5);
    }
    // 10235  o carro gira meia volta, mínimo 80, rodopio de 12
    c.wf(cpu.ar[3] + 0x2D, c.rf(cpu.ar[3] + 0x2D) + c.k(0xC10236));
    double v = c.rf(cpu.ar[3] + 0x26);
    if (v < 80.0) v = 80.0;
    c.wf(cpu.ar[3] + 0x26, v);
    c.wr(cpu.ar[3] + 0x31, 1);
    c.wf(cpu.ar[3] + 0x32, 12.0);
    c.wf(cpu.ar[3] + 0x2E, c.k(0xC10240));
    cpu.ar[0] = ar0;
    cpu.ar[1] = ar1;
    cpu.ar[3] = ar3;
    cpu.ar[4] = ar4;
    cpu.ar[5] = ar5;
}

/// `0xC101EC`: volume do som 0x114 −5 por quadro (R5) enquanto ele toca.
void herdSoundFade(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[5] -= 5;
    if (s(cpu.r[5]) < 0) {
        cpu.ar[2] = kHerdSound;
        c.call(0x91C1);
        return c.die();
    }
    cpu.r[2] = cpu.r[5];
    cpu.ar[2] = kHerdSound;
    if (cpu.ar[2] != c.rd(0xE8FF) && cpu.ar[2] != c.rd(0xE908)) return c.die();
    c.call(0x4330);
    c.sleep(1, 0xC101F7);
}

// --- Objetos 0x498 (deslizam e somem) -----------------------------------------------------------

/// `0x44B3`: solta o objeto e morre.
void slideEnd(ProcCtx& c) {
    c.wr(c.cpu.ar[4] + 0x17, 0);
    clearBusy(c, c.cpu.ar[4]);
    c.die();
}

/// `R6 −= |R7|·quadros`; devolve true quando acabou (`R6` < 0).
bool slideBudget(ProcCtx& c) {
    const double used = std::fabs(R(c, 7)) * ticks(c);
    setR(c, 6, R(c, 6) - used);
    return R(c, 6) < 0.0;
}

/// Soma `[[5D]]` à posição do objeto (com @p lift em y).
void addScratch(ProcCtx& c, double lift) {
    const uint32_t v = c.rd(kVecPtr), obj = c.cpu.ar[4];
    c.wf(obj + 1, c.rf(obj + 1) + c.rf(v));
    c.wf(obj + 2, c.rf(obj + 2) + c.rf(v + 1) + lift);
    c.wf(obj + 3, c.rf(obj + 3) + c.rf(v + 2));
}

/// `0x4454`: anda R7·quadros no rumo `+15` + π/2 (subindo 50 por quadro).
void slideUpLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    setR(c, 2, c.rf(cpu.ar[4] + 0x15) + c.k(0x4455));
    cpu.ar[2] = c.rd(kMatPtr);
    c.call(0x938A);
    c.call(0x9414);
    c.wf(cpu.ar[2] + 2, R(c, 7) * ticks(c));
    cpu.r[2] = c.rd(kMatPtr);
    cpu.r[3] = cpu.ar[2];
    c.call(0x941E);
    addScratch(c, 50.0);
    if (slideBudget(c)) return slideEnd(c);
    c.sleep(1, 0x4472);
}

/// `0x4498`: anda R7·quadros pela matriz do próprio objeto.
void slideLoop(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x9414);
    c.wf(cpu.ar[2] + 2, R(c, 7) * ticks(c));
    cpu.r[2] = cpu.ar[4] + 4;
    cpu.r[3] = cpu.ar[2];
    c.call(0x941E);
    addScratch(c, 0.0);
    if (slideBudget(c)) return slideEnd(c);
    c.sleep(1, 0x44B2);
}

/// Tratadores `0x4440` (som 0xB4), `0x4473`, `0x447F`: processo `[ptr]` (ID 0x500). Preservam AR0, AR2, R2.
void slideSpawn(ProcCtx& c, uint32_t ptr, bool withSound) {
    Tms32031State& cpu = c.cpu;
    const uint32_t ar0 = cpu.ar[0], ar2 = cpu.ar[2], r2 = cpu.r[2];
    if (withSound) c.sound(0xB4);
    spawnFor(c, ptr, 0x500);
    cpu.r[0] = c.rd(cpu.ar[4] + 0xE);
    cpu.ar[0] = ar0;
    cpu.ar[2] = ar2;
    cpu.r[2] = r2;
}

} // namespace

void registerRoamingObjects(SubroutineDispatcher& d) {
    // Tratadores da tabela [4500]: só sem link ([C8E3] = 0; RETSNE)
    addProc(d, 0x9B43, [](ProcCtx& c) {
        c.cpu.r[0] = c.rd(kLinked);
        if (c.cpu.r[0] == 0) leaderSpawn(c);
    });
    addProc(d, 0x9B46, [](ProcCtx& c) {
        c.cpu.r[0] = c.rd(kLinked);
        if (c.cpu.r[0] == 0) followerSpawn(c);
    });
    addProc(d, 0xC101A4, leaderSpawn);
    addProc(d, 0xC101DA, followerSpawn);
    addProc(d, 0x9B49, followerStart);
    addProc(d, 0x9B4E, herdStart);
    addProc(d, 0x9B74, herdLoop);
    addProc(d, 0x9BE9, herdLoop);
    addProc(d, 0x9BF1, bumpStart);
    addProc(d, 0x9BFE, bumpLoop);
    addProc(d, 0x9C3B, bumpLoop);
    addProc(d, 0x9C3C, rollState);
    addProc(d, 0x9CA7, rollState);
    addProc(d, 0x9CB1, sideWait);
    addProc(d, 0x9CC5, uprightStart);
    addProc(d, 0x9CDE, uprightLoop);
    addProc(d, 0x9CBA, herdExit);
    addProc(d, 0x9CDF, tumbleMatrix);
    addProc(d, 0xC10248, tumbleMatrix);
    // 09BEE → C101EB: processo do volume (R5 = 0xFF)
    addProc(d, 0x9BEE, [](ProcCtx& c) {
        c.cpu.r[5] = 0xFF;
        herdSoundFade(c);
    });
    addProc(d, 0xC101EB, [](ProcCtx& c) {
        c.cpu.r[5] = 0xFF;
        herdSoundFade(c);
    });
    addProc(d, 0xC101F7, herdSoundFade);
    addProc(d, 0x9BEF, herdBumped);
    addProc(d, 0xC101FB, herdBumped);
    d.add(0x7EE9, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { nearestRoad(c, b); });
    d.add(0x9032, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { freeCarData(c, b); });
    d.add(0x676B, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { unlinkMoving(c, b); });

    addProc(d, 0x4440, [](ProcCtx& c) { slideSpawn(c, 0xB0D2, true); });
    addProc(d, 0x4473, [](ProcCtx& c) { slideSpawn(c, 0xB0D3, false); });
    addProc(d, 0x447F, [](ProcCtx& c) { slideSpawn(c, 0xB0D4, false); });
    addProc(d, 0x4451, [](ProcCtx& c) {
        setR(c, 7, 250.0);
        setR(c, 6, 32000.0 * 4.0);
        slideUpLoop(c);
    });
    addProc(d, 0x4472, slideUpLoop);
    addProc(d, 0x448E, [](ProcCtx& c) {
        c.wr(c.cpu.ar[4] + 0x1E, c.rd(c.cpu.ar[4] + 0x1E) + 5);
        setR(c, 7, -150.0);
        setR(c, 6, 32000.0 * 8.0);
        slideLoop(c);
    });
    addProc(d, 0x4495, [](ProcCtx& c) {
        setR(c, 7, 150.0);
        setR(c, 6, 32000.0 * 3.0);
        slideLoop(c);
    });
    addProc(d, 0x44B2, slideLoop);
    addProc(d, 0x44B3, slideEnd);
}

} // namespace rea::core::game
