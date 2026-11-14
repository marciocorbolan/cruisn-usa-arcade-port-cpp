/**
 * @file drone_ai.cpp
 * @brief Tradução de `0x513A`–`0x5254`, `0x52B8`–`0x535F`, `0x5375`–`0x54FC`, `0x66A7`–`0x66BB`
 *        e `0x94A7`–`0x94B3`.
 */
#include "core/game/drone_ai.hpp"

#include <cmath>

#include "core/link/link_car_sync.hpp"
#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

constexpr uint32_t kLaneMapPtr = 0x538C; ///< `[538C]`: 50 faixas de ocupação lateral.
constexpr uint32_t kLanes      = 50;  ///< Faixas no mapa de ocupação.

/// Contexto da IA dos carros: CPU, barramento, sub-rotinas e chamadas matemáticas.
struct Ctx {
    Tms32031State& cpu;  ///< Registradores da CPU TMS320C31.
    MemoryBus& bus;  ///< Barramento de memória simulado.
    ISubroutines& subs;  ///< Despachante das sub-rotinas traduzidas.
    /// Lê o float TMS em @p a e o converte para double.
    double rf(uint32_t a) const { return tmsToDouble(bus.readWord(a)); }
    /// Converte @p v para float TMS e grava em @p a.
    void wf(uint32_t a, double v) const { bus.writeWord(a, doubleToTms(v)); }
    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) const { return bus.readWord(a); }
    /// Grava a palavra @p v no endereço @p a.
    void wr(uint32_t a, uint32_t v) const { bus.writeWord(a, v); }
    /// Lê a palavra em @p a como inteiro com sinal.
    int32_t ri(uint32_t a) const { return static_cast<int32_t>(bus.readWord(a)); }
    /// Float curto de 16 bits (imediato da instrução) na palavra @p pc da ROM.
    double K(uint32_t pc) const { return shortFloat(static_cast<uint16_t>(bus.readWord(pc) & 0xFFFFu)); }
    /// Endereço do campo @p o dos dados do carro (AR5).
    uint32_t D(uint32_t o) const { return cpu.ar[5] + o; }
    /// Endereço do campo @p o do objeto (AR4).
    uint32_t O(uint32_t o) const { return cpu.ar[4] + o; }
    /// Endereço da variável @p o do processo (AR7).
    uint32_t P(uint32_t o) const { return cpu.ar[7] + o; }
    /// Chama a sub-rotina traduzida no endereço da ROM indicado (o `CALL` original).
    void call(uint32_t a) const { subs.call(a, cpu, bus); }
    /// Lê o registrador R@p i como float TMS.
    double R(int i) const { return tmsToDouble(cpu.r[i]); }
    /// Grava @p v, convertido para float TMS, no registrador R@p i.
    void setR(int i, double v) const { cpu.r[i] = doubleToTms(v); }
    /// Quadros da última iteração (`[C92F]`), como double.
    double fc92f() const { return static_cast<double>(ri(0xC92F)); }
    /// Seno/cosseno pela rotina @p a (`0x92DF`/`0x92E2`) do ângulo @p angle.
    double trig(uint32_t a, double angle) const {
        cpu.r[2] = doubleToTms(angle);
        call(a);
        return R(0);
    }
    /// Raiz quadrada (`0xA5F1`).
    double sqrtR(double v) const {
        setR(2, v);
        call(0xA5F1);
        return R(0);
    }
    /// @p num / @p den (`0xA509`).
    double divide(double num, double den) const {
        setR(0, num);
        setR(1, den);
        call(0xA509);
        return R(0);
    }
    /// Rumo de (@p dx, @p dz) (`0x931A`).
    double atan2R(double dx, double dz) const {
        setR(2, dx);
        setR(3, dz);
        call(0x931A);
        return R(0);
    }
    /// Aleatório float pela rotina @p routine com o limite @p v.
    double rnd(uint32_t routine, double v) const {
        setR(0, v);
        call(routine);
        return R(0);
    }
    /// Dorme 1 quadro e retoma em @p pc (`0xA874`).
    void sleep(uint32_t pc) const {
        cpu.ar[2] = 1;
        cpu.ret = pc;
        call(0xA874);
    }
    /// Com link (`[C8E3]`), envia o estado do carro ao outro gabinete (`0x7AD7`).
    void linkTick() const {
        if (rd(0xC8E3) != 0) call(0x7AD7);
    }
};

void carLoop(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// 094A7  R0 = R0 − R2 dobrado em (−π, π] por `[9303]`/`[9304]`. Preserva R1.
void angleDiff(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    double r0 = tmsToDouble(cpu.r[0]) - tmsToDouble(cpu.r[2]);
    if (!(std::fabs(r0) < tmsToDouble(bus.readWord(0x9303))))
        r0 += r0 < 0.0 ? tmsToDouble(bus.readWord(0x9304)) : -tmsToDouble(bus.readWord(0x9304));
    cpu.r[0] = doubleToTms(r0);
}

/// 05375  compara o carro AR4 com o carro AR2 (dados AR3). < 0 → AR4 está à frente.
double compareCars(const Ctx& x) {
    const int32_t other = x.ri(x.rd(x.cpu.ar[3] + 0x37) + 0x1E);
    uint32_t n = x.rd(x.D(0x37));
    const int32_t mine = x.ri(n + 0x1E);
    x.cpu.ar[0] = n;
    if (other != mine) return static_cast<double>(other - mine);
    for (int i = 0; i < 2; ++i)
        if (const uint32_t nx = x.rd(n + 0x21); nx != 0) n = nx;
    x.cpu.ar[0] = n;
    auto d2 = [&](uint32_t a) {
        const double dx = x.rf(n + 1) - x.rf(a + 1), dz = x.rf(n + 3) - x.rf(a + 3);
        return dx * dx + dz * dz;
    };
    const double m = d2(x.cpu.ar[4]), o = d2(x.cpu.ar[2]);
    x.setR(0, m);
    x.setR(1, o);
    return m - o;
}

/// 051F4  compara com o jogador (sem efeito além dos registradores).
void comparePlayer(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    cpu.ar[2] = x.rd(0xE876);
    cpu.ar[3] = x.rd(cpu.ar[2] + 0x1B);
    if (compareCars(x) < 0.0) return;
    x.setR(6, x.rf(cpu.ar[2] + 1) - x.rf(x.O(1)));
    x.setR(7, x.rf(cpu.ar[2] + 3) - x.rf(x.O(3)));
}

/// 066A7  R0 = bit 7 de `[janela+3]` do trecho do nó AR2 (pista bloqueada), 0 se fora da janela.
void segmentBlocked(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    const uint32_t seg = bus.readWord(cpu.ar[2] + 0x1E) >> 8;
    uint32_t w = bus.readWord(0x3F69);
    for (int32_t n = static_cast<int32_t>(bus.readWord(0xE46C)) + 1; n >= 0; --n, w += 5) {
        if (bus.readWord(w + 4) == seg) {
            cpu.r[0] = (bus.readWord(w + 3) >> 7) & 1u;
            return;
        }
    }
    cpu.r[0] = 0;
}

/// 053A2  zera o mapa de ocupação e marca as faixas bloqueadas pela pista.
void laneMapInit(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    const uint32_t map = x.rd(kLaneMapPtr);
    for (uint32_t i = 0; i < kLanes; ++i) x.wr(map + i, 0);
    cpu.r[0] = 0;
    if (x.rd(0xE481) == 10) {
        const uint32_t v = 0x1000;
        const int32_t idx = x.ri(x.rd(x.P(0x10)) + 0x1E);
        if (idx < x.ri(0x53A0)) return;
        if (idx <= x.ri(0x53A1)) {
            const uint32_t start = (x.rd(x.P(0xE)) & 1u) ? 0u : 0x17u;
            for (uint32_t i = 0; i < 27; ++i) x.wr(map + start + i, v);
            return;
        }
        const int32_t e = x.ri(x.P(0xE));
        if (e >= 3 && (static_cast<uint32_t>(e) & 1u)) {
            for (uint32_t i : {0x24u, 0x25u, 0x26u, 0x27u, 0x28u, 9u, 0xAu, 0xBu, 0xCu, 0xDu}) x.wr(map + i, v);
            return;
        }
        for (uint32_t i = 0; i < 14; ++i) {
            x.wr(map + i + 0x24, v);
            x.wr(map + i, v);
        }
        return;
    }
    cpu.ar[2] = x.rd(x.P(0x10));
    x.call(0x66A7);
    if (cpu.r[0] != 0) return;
    for (uint32_t i = 0; i < 12; ++i) {
        x.wr(map + i + 0x26, 0x1000);
        x.wr(map + i, 0x1000);
    }
}

/// 054AA  velocidade lateral do carro AR2 em relação à pista.
double lateralSpeed(const Ctx& x) {
    const uint32_t ar2 = x.cpu.ar[2];
    x.cpu.ar[3] = x.rd(ar2 + 0x1B);
    x.cpu.ar[2] = x.rd(x.cpu.ar[3] + 0x37);
    x.call(0x303A);
    x.cpu.ar[2] = ar2;
    const double a = x.R(0) - x.rf(x.cpu.ar[3] + 0x2D);
    return x.trig(0x92E2, a) * x.rf(x.cpu.ar[3] + 0x26) * 1.5;
}

/// 054B5  largura lateral (R0) e comprimento (R1) projetados do carro AR3.
double projectedWidth(const Ctx& x) {
    const uint32_t ar2 = x.cpu.ar[2];
    x.cpu.ar[2] = x.rd(x.cpu.ar[3] + 0x37);
    x.call(0x303A);
    x.cpu.ar[2] = ar2;
    const double a = x.R(0) - x.rf(x.cpu.ar[3] + 0x2D);
    const double s = std::fabs(x.trig(0x92E2, a));
    const double c = std::fabs(x.trig(0x92DF, a));
    const uint32_t d3 = x.cpu.ar[3];
    const double w = x.rf(d3 + 0x3A) - x.rf(d3 + 0x3B);
    const double l = x.rf(d3 + 0x3E) - x.rf(d3 + 0x3F);
    x.setR(1, w * s + l * c);
    return w * c + l * s;
}

/// 05441  marca no mapa a faixa que o carro AR2 ocupará (R0 = marca). R0 = tempo ou 0x800.
void markCar(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    x.wr(0xE8E2, cpu.r[0]);
    cpu.ar[3] = x.rd(cpu.ar[2] + 0x1B);
    const uint32_t d3 = cpu.ar[3];
    auto none = [&] { cpu.r[0] = 0x800; };
    if (compareCars(x) < 0.0) return none();
    double r6 = x.rf(cpu.ar[2] + 1) - x.rf(x.O(1));
    double r7 = x.rf(cpu.ar[2] + 3) - x.rf(x.O(3));
    const double d2 = r6 * r6 + r7 * r7;
    if (30000.0 * 30000.0 < d2) return none();
    double time;
    const double reach = x.rf(d3 + 0x3E) + x.rf(x.D(0x3E)) + 200.0;
    if (!(d2 > reach * reach) && !(std::fabs(x.rf(x.D(0x46)) - x.rf(d3 + 0x46)) < 400.0)) {
        time = 10.0;
    } else {
        // 0545E
        const double dist = x.sqrtR(d2);
        double v = x.rf(x.D(0x26)), h = x.rf(x.D(0x2D));
        r6 -= v * -x.trig(0x92E2, h);
        r7 -= v * x.trig(0x92DF, h);
        v = x.rf(d3 + 0x26);
        h = x.rf(d3 + 0x2D);
        r6 += v * -x.trig(0x92E2, h);
        r7 += v * x.trig(0x92DF, h);
        const double closing = x.sqrtR(r6 * r6 + r7 * r7) - dist;
        if (closing >= 0.0) return none();
        const double rate = std::fabs(closing) * 1.5;
        r6 = std::fabs(r6) - 400.0;
        if (r6 < 0.0) r6 = 0.0;
        r7 = std::fabs(r7) - 1000.0;
        if (r7 < 0.0) r7 = 0.0;
        time = x.divide(x.sqrtR(r6 * r6 + r7 * r7), rate);
        if (time > 200.0) return none();
    }
    // 0548A
    const double lat = lateralSpeed(x) * time - x.rf(d3 + 0x46);
    const int32_t t = static_cast<int32_t>(tmsFix(time));
    int32_t idx = static_cast<int32_t>(tmsFix(lat * x.K(0x548F))) + 0x19;
    double w = std::fabs(projectedWidth(x) * x.K(0x5493));
    if (w > 20.0) w = 20.0;
    const int32_t span = static_cast<int32_t>(tmsFix(w)) + 3;
    idx -= static_cast<int32_t>(static_cast<uint32_t>(span) >> 1);
    const uint32_t map = x.rd(kLaneMapPtr);
    const uint32_t mark = x.rd(0xE8E2) + static_cast<uint32_t>(t);
    for (int32_t k = 0; k <= span; ++k, ++idx) {
        if (idx < 0) continue;
        if (idx > 0x31) break;
        x.wr(map + static_cast<uint32_t>(idx), mark);
    }
    cpu.r[0] = static_cast<uint32_t>(t);
}

/// 0538D  marca os outros carros da lista `[E404]`.
void markOthers(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    for (uint32_t o = x.rd(0xE404); o != 0; o = x.rd(o + 0x20)) {
        if (o == cpu.ar[4] || (x.rd(o + 0xE) & 0x10u)) continue;
        cpu.ar[2] = o;
        cpu.r[0] = 0x2000;
        x.call(0x5441);
    }
}

/// 0539A  marca o jogador; `[E8E1]` = tempo até ele.
void markPlayer(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    cpu.ar[2] = x.rd(0xE876);
    cpu.ar[3] = x.rd(cpu.ar[2] + 0x1B);
    cpu.r[0] = 0x4000;
    x.call(0x5441);
    x.wr(0xE8E1, cpu.r[0]);
}

/// 053DA  escolhe o deslocamento lateral livre mais próximo do alvo `[AR7+0x20]`.
void chooseLane(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    const uint32_t map = x.rd(kLaneMapPtr);
    auto keep = [&] { cpu.r[0] = x.rd(x.P(0x20)); };
    int32_t c = static_cast<int32_t>(tmsFix(-x.rf(x.D(0x46)) * x.K(0x53DD))) + 0x15;
    if (c > 0x29) c = 0x29;
    if (c < 0) c = 0;
    uint32_t any = 0;
    for (uint32_t i = 0; i < 8; ++i) any |= x.rd(map + static_cast<uint32_t>(c) + i);
    if (any == 0) return keep();
    if (static_cast<int32_t>(any) >= 0x4000) {
        const uint32_t t = any & 0xFFFu;
        if (static_cast<int32_t>(t) <= 0x14 && x.rd(x.D(0x40)) != 0x215 &&
            !(x.rf(x.rd(0xE878) + 0x26) - x.rf(x.D(0x26)) > 10.0))
            x.wf(x.D(0x26), (static_cast<double>(t) * x.K(0x53F8) + x.K(0x53F9)) * x.rf(x.D(0x26)));
    }
    // 053FC  procura 8 faixas livres seguidas para a direita
    uint32_t r6 = 0, r7 = 0;
    {
        int32_t n = 0x1E;
        const int32_t over = c + 0x1E - 0x31;
        if (over > 0) n -= over;
        uint32_t a = map + static_cast<uint32_t>(c);
        int32_t run = 0;
        do {
            if (x.rd(a++) != 0) run = 0;
            if (++run > 8) {
                r6 = a - 4;
                break;
            }
        } while (--n != 0);
    }
    // 0540F  e para a esquerda
    int32_t c2 = static_cast<int32_t>(tmsFix(-x.rf(x.D(0x46)) * x.K(0x5410))) + 0x1D;
    if (c2 > 0x31) c2 = 0x31;
    if (c2 >= 7) {
        uint32_t a = map + static_cast<uint32_t>(c2);
        int32_t n = 0x1E;
        if (c2 - 0x1E <= 0) n += c2 - 0x1E;
        int32_t run = 0;
        do {
            if (x.rd(a--) != 0) run = 0;
            if (++run > 8) {
                r7 = a + 4;
                break;
            }
        } while (--n != 0);
    }
    if (r6 + r7 == 0) return keep();
    if (r6 == 0) r6 = r7;
    if (r7 == 0) r7 = r6;
    const int32_t i6 = static_cast<int32_t>(r6 - map), i7 = static_cast<int32_t>(r7 - map);
    const int32_t t = static_cast<int32_t>(tmsFix(x.rf(x.P(0x20)) * x.K(0x5431)));
    const int32_t pick = std::abs(t - i7) < std::abs(t - i6) ? i7 : i6;
    x.setR(0, static_cast<double>(pick - 0x19) * 100.0);
}

/// 052E4  variação do acelerador: `[AR7+0xF]` = `[AR7+0x21]` · `[AR7+0x22]`. Preserva AR2.
void throttleVariation(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    const uint32_t ar2 = cpu.ar[2];
    int32_t n = x.ri(x.P(0x24)) - 1;
    x.wr(x.P(0x24), static_cast<uint32_t>(n));
    if (n < 0) {
        cpu.ar[2] = 0x23;
        x.call(0x8EE0);
        x.wr(x.P(0x24), cpu.r[0] + 10);
        const uint32_t pl = x.rd(0xE876);
        const double dx = x.rf(pl + 1) - x.rf(x.O(1)), dz = x.rf(pl + 3) - x.rf(x.O(3));
        double r0 = x.sqrtR(dx * dx + dz * dz);
        int32_t r1, r2;
        if (const uint32_t r3 = x.rd(x.P(0x25)); r3 != 0) {
            r1 = static_cast<int32_t>(r3);
            r2 = 0;
        } else {
            r2 = x.ri(x.rd(x.rd(pl + 0x1B) + 0x37) + 0x1E);
            r1 = x.ri(x.rd(x.D(0x37)) + 0x1E);
        }
        r0 *= x.K(0x52FF) * x.K(0x5300) * x.K(0x5301);
        if (r0 > x.K(0x5302)) r0 = x.K(0x5303);
        double r3 = 1.0;
        if (r1 == r2) r3 = 0.0;
        if (r1 > r2) {
            r3 = -1.0;
            if (r0 < x.K(0x5309)) r0 = x.K(0x530A);
        } else {
            if (r0 < x.K(0x530C)) r0 = x.K(0x530D);
        }
        r3 = r0 * r3;
        if (const int32_t e = x.ri(0xE87B); e != 0) {
            double v = x.rnd(0x8EC2, static_cast<double>(e) * x.K(0x5311) * x.K(0x5312));
            if (v > x.K(0x5314)) v = x.K(0x5315);
            r3 += v;
        }
        x.wf(x.P(0x22), r3 * x.rf(x.P(0x26)) + 1.0);
    }
    // 0531A
    n = x.ri(x.P(0x23)) - 1;
    x.wr(x.P(0x23), static_cast<uint32_t>(n));
    if (n < 0) {
        cpu.ar[2] = 0x5A;
        x.call(0x8EE0);
        cpu.ar[2] += 0x1E;  // a ROM soma em AR2, não em R0
        x.wr(x.P(0x23), cpu.r[0]);
        x.wf(x.P(0x21), x.rnd(0x8ED1, x.K(0x5322)) + 1.0);
    }
    x.wf(x.P(0xF), x.rf(x.P(0x21)) * x.rf(x.P(0x22)));
    cpu.ar[2] = ar2;
}

/// 0532B  velocidade simplificada (fora da janela). R5 = velocidade efetiva.
void simpleSpeed(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    double thr = x.rf(x.P(0xF));
    if ((x.rd(0xC8C5) & 0x200u) == 0) thr = 0.0;
    x.wf(x.D(0x24), thr);
    double r0 = thr * x.rf(x.D(0x23));
    double r1 = x.rf(x.D(0x39)) * x.K(0x5332);
    int32_t i = static_cast<int32_t>(tmsFix(r1));
    if (i > 0x12) i = 0x12;
    const uint32_t torque = x.rd(0x2D68);
    const double a = x.rf(torque + static_cast<uint32_t>(i));
    ++i;
    const double b = x.rf(torque + static_cast<uint32_t>(i));
    r1 = static_cast<double>(i) - r1;
    if (r1 < 0.0) r1 = 0.0;
    if (r1 > 1.0) r1 = 1.0;
    r1 = b * (1.0 - r1) + a * r1;
    const uint32_t gear = x.rd(x.D(0x38));
    r1 *= x.rf(x.rd(0x2D62) + gear) * x.K(0x5346);
    r0 *= r1;
    double drag = x.rf(x.D(0x44));
    double eng = x.rf(x.D(0x39)) - 42.0;
    if (eng < 0.0) eng = 0.0;
    drag += (eng * x.K(0x534F) + 1.0) * x.rf(x.rd(0x2D7D) + gear);
    double sum = 0.0, v = x.rf(x.D(0x26));
    const int32_t n = x.ri(0xC92F);
    for (int32_t k = 0; k < (n < 1 ? 1 : n); ++k) {
        v = v - drag * v + r0;
        sum += v;
    }
    sum *= 1.5;
    x.setR(5, sum);
    x.wf(x.D(0x27), sum);
    x.wf(x.D(0x26), v);
}

/// 054CB  som de ultrapassagem perto do jogador.
void passSound(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    if (const int32_t t = x.ri(x.P(0x28)); t != 0) {
        x.wr(x.P(0x28), static_cast<uint32_t>(t - 1 < 0 ? 0 : t - 1));
        return;
    }
    cpu.ar[2] = x.rd(0xE876);
    cpu.ar[3] = x.rd(cpu.ar[2] + 0x1B);
    double r3 = x.rf(x.D(0x26)) - x.rf(cpu.ar[3] + 0x26);
    const int32_t z = x.ri(x.O(0x1C));
    if (r3 > 40.0) {
        if (z < 0x5DC || z > 0x1388) return;
    } else {
        r3 = std::fabs(r3);
        if (r3 < 20.0) return;
        if (r3 > 60.0) r3 = 60.0;
        if (z < 0 || z > 0xBB8) return;
    }
    double r0 = x.divide(4000.0, std::fabs(x.rf(x.D(0x46)) - x.rf(cpu.ar[3] + 0x46)));
    if (r0 > 1.0) r0 = 1.0;
    cpu.r[0] = tmsFix(r0 * 255.0);
    cpu.ar[2] = 0x231;
    if (r3 > 40.0) cpu.ar[2] = 0x234;
    if (!(r3 < 60.0) && (tmsFix(r3) & 1u)) cpu.ar[2] = 0x237;
    x.call(0x9186);
    x.wr(x.P(0x28), 0x28);
}

/// 051EA  carro parado após a corrida: só física e rumo da pista.
void parkedLoop(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    x.call(0x2EFD);
    x.call(0x3037);
    x.wf(x.D(0x2C), x.R(0) + x.rf(x.P(0x2A)));
    x.call(0x2C39);
    x.linkTick();
    x.sleep(0x51EA);
}

/// 051C2  fim de corrida: contagem `[AR7+0x29]`. true = o carro foi para `0x51EA`.
bool raceEnd(const Ctx& x) {
    double r0 = x.rf(x.P(0x29));
    if (r0 == 0.0) {
        x.wf(x.P(0x2A), x.rnd(0x8ED1, x.K(0x51C4)));
        const uint32_t n = x.rd(0xE8A2) + 1;
        x.wr(0xE8A2, n);
        r0 = static_cast<double>(static_cast<int32_t>((0xAu - n) * 0xAF0u + 0xDACu));
    }
    r0 -= x.rf(x.D(0x26)) * x.fc92f();
    x.wf(x.P(0x29), r0);
    if (r0 >= 0.0) return false;
    // 051D5
    if (x.rd(x.P(0x25)) != 0 && x.rf(x.D(0x26)) != 0.0) {
        x.wf(x.O(1), x.rnd(0x8ED1, 300.0) + x.rf(x.O(1)));
        x.wf(x.O(3), x.rnd(0x8ED1, 300.0) + x.rf(x.O(3)));
    }
    x.wf(x.D(0x26), 0.0);
    x.wf(x.D(0x23), 0.0);
    if (x.rd(x.P(0x25)) != 0) return false;
    x.wr(x.O(0xE), x.rd(x.O(0xE)) | 0x10u);
    parkedLoop(x.cpu, x.bus, x.subs);
    return true;
}

/// 05220  carro além do fim do caminho: só espera.
void idleLoop(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    x.linkTick();
    x.sleep(0x5220);
}

/// 052B8  segue o caminho grosso `[AR7+0x12]`. true = fim do caminho (foi para `0x5220`).
bool followPath(const Ctx& x) {
    uint32_t e;
    double dx, dz;
    for (;;) {
        e = x.rd(x.P(0x12));
        dx = static_cast<double>(x.ri(e)) - x.rf(x.O(1));
        dz = static_cast<double>(x.ri(e + 2)) - x.rf(x.O(3));
        if (dx * dx + dz * dz > 5000.0 * 5000.0) break;
        e += 4;
        x.wr(x.P(0x12), e);
        x.wr(x.P(0x13), x.rd(e + 3));
        if (x.ri(e + 3) == -1) {
            idleLoop(x.cpu, x.bus, x.subs);
            return true;
        }
    }
    // 052CC
    x.wf(x.O(2), static_cast<double>(x.ri(e + 1)) - 250.0);
    const uint32_t v = x.rd(0x5D);
    x.cpu.ar[2] = v;
    x.wf(v, dx);
    x.wf(v + 1, 0.0);
    x.wf(v + 2, dz);
    x.call(0x943B);
    x.call(0x2C92);
    x.call(0x2E73);
    x.wr(x.D(0x38), x.cpu.r[0]);
    x.wr(x.D(0x37), x.rd(x.P(0x12)));
    x.call(0x52E4);
    x.call(0x532B);
    const double r5 = x.R(5);
    x.wf(x.O(1), r5 * x.rf(v) + x.rf(x.O(1)));
    x.wf(x.O(3), r5 * x.rf(v + 2) + x.rf(x.O(3)));
    return false;
}

/// 0523C  volta à janela: reposiciona no nó e no rumo da pista e segue em `0x513A`.
void backInWindow(const Ctx& x) {
    x.wr(x.P(0x25), 0);
    x.wr(x.O(0xE), x.rd(x.O(0xE)) & ~0x10u);
    x.cpu.r[2] = x.rd(x.P(0x13));
    x.call(0x5360);
    x.wr(x.P(0x10), x.cpu.ar[2]);
    x.call(0x2EFD);
    x.call(0x3037);
    const uint32_t h = x.cpu.r[0];
    x.cpu.r[2] = h;
    x.wr(x.O(0x15), h);
    x.wr(x.D(0x2C), h);
    x.wr(x.D(0x2D), h);
    x.wf(x.D(0x2E), 0.0);
    x.wf(x.D(0x32), 0.0);
    x.wf(x.D(0x25), 0.0);
    x.wr(x.D(0x31), 0);
    x.cpu.ar[2] = x.O(4);
    x.call(0x938A);
    carLoop(x.cpu, x.bus, x.subs);
}

/// 052A8  retomada pedida pelo tipo 0xD do link (`0x5278`): o carro volta a este gabinete por baixo da janela;
/// sem reposicionar no rumo da pista, zera o estado e segue em `0x513A`.
void handBackBehind(const Ctx& x) {
    x.wr(x.P(0x25), 0);
    x.wr(x.O(0xE), x.rd(x.O(0xE)) & ~0x10u);
    x.cpu.r[2] = x.rd(x.D(0x57));
    x.wr(x.P(0x13), x.cpu.r[2]);
    x.call(0x5360);
    x.wr(x.P(0x10), x.cpu.ar[2]);
    x.wf(x.D(0x2E), 0.0);
    x.wf(x.D(0x32), 0.0);
    x.wf(x.D(0x25), 0.0);
    x.wr(x.D(0x31), 0);
    carLoop(x.cpu, x.bus, x.subs);
}

/// 0520F  laço à frente da janela.
void aheadLoop(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    if (x.ri(x.P(0x13)) >> 8 >= x.ri(0xE853) && raceEnd(x)) return;
    if (x.ri(x.P(0x13)) < x.ri(x.rd(0xE470) + 0x1E)) return backInWindow(x);
    if (x.rd(0xC8E3) != 0 && link::trafficHandOff(x.cpu, x.bus, x.subs, false)) return x.call(0xA718);
    if (followPath(x)) return;
    x.linkTick();
    x.sleep(0x520F);
}

/// 0522C  laço atrás da janela.
void behindLoop(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    const uint32_t n3 = x.rd(x.rd(x.rd(x.rd(0xE46E) + 0x21) + 0x21) + 0x21);
    if (x.ri(x.P(0x13)) > x.ri(n3 + 0x1E)) return backInWindow(x);
    if (x.rd(0xC8E3) != 0 && link::trafficHandOff(x.cpu, x.bus, x.subs, true)) return x.call(0xA718);
    if (followPath(x)) return;
    x.linkTick();
    x.sleep(0x522C);
}

/// 05208 / 05225  saída da janela (R0 = 1 à frente, −1 atrás).
void leaveWindow(const Ctx& x, bool ahead) {
    x.wr(x.P(0x25), ahead ? 1u : 0xFFFFFFFFu);
    x.cpu.r[2] = x.rd(x.P(0x13));
    x.call(0x536C);
    x.wr(x.O(0xE), x.rd(x.O(0xE)) | 0x10u);
    if (ahead) aheadLoop(x.cpu, x.bus, x.subs);
    else behindLoop(x.cpu, x.bus, x.subs);
}

/// 05140  corpo da IA. false = o processo já dormiu em outro laço.
bool aiBody(const Ctx& x) {
    Tms32031State& cpu = x.cpu;
    if (static_cast<int32_t>(x.rd(x.rd(x.D(0x37)) + 0x1E) >> 8) >= x.ri(0xE853) && raceEnd(x)) return false;
    x.call(0x54CB);
    // 05146  avança o nó-alvo
    double d2;
    for (;;) {
        const int32_t lo = x.ri(x.rd(x.rd(x.rd(0xE46E) + 0x21) + 0x21) + 0x1E);
        const int32_t hi = x.ri(x.rd(0xE470) + 0x1E);
        const int32_t idx = x.ri(x.P(0x13));
        if (idx < lo) {
            leaveWindow(x, false);
            return false;
        }
        if (idx >= hi) {
            leaveWindow(x, true);
            return false;
        }
        const uint32_t n = x.rd(x.P(0x10));
        const double dx = x.rf(n + 1) - x.rf(x.O(1)), dz = x.rf(n + 3) - x.rf(x.O(3));
        d2 = dx * dx + dz * dz;
        bool advance = d2 < 5000.0 * 5000.0;
        if (!advance) {
            const uint32_t nx = x.rd(n + 0x21);
            if (nx == 0) break;
            const double ex = x.rf(nx + 1) - x.rf(x.O(1)), ez = x.rf(nx + 3) - x.rf(x.O(3));
            if (ex * ex + ez * ez > d2) break;
        }
        const uint32_t nx = x.rd(n + 0x21);
        if (nx == 0) {
            leaveWindow(x, true);
            return false;
        }
        x.wr(x.P(0x10), nx);
        x.wr(x.P(0x13), x.rd(nx + 0x1E));
    }
    // 0516F  quadros até o nó
    const double dist = x.sqrtR(d2);
    double sp = x.rf(x.D(0x26));
    if (sp <= 0.0) sp = 30.0;
    const double frames = x.divide(dist, sp * x.fc92f());
    x.call(0x51F4);
    x.call(0x53A2);
    x.call(0x538D);
    x.call(0x539A);
    x.call(0x53DA);
    x.wr(x.P(0x20), cpu.r[0]);
    const uint32_t n = x.rd(x.P(0x10));
    const uint32_t nx = x.rd(n + 0x21);
    if (nx == 0) return true;  // 0517F: a ROM trava aqui (BU $)
    // 05181  ponto-alvo = nó + deslocamento lateral
    const double road = x.atan2R(x.rf(nx + 1) - x.rf(n + 1), x.rf(nx + 3) - x.rf(n + 3)) - x.K(0x5186);
    const double s = x.trig(0x92E2, road);
    const double c = x.trig(0x92DF, road);
    const double off = x.rf(x.P(0x20));
    const double tx = x.rf(n + 1) - x.rf(x.O(1)) + c * off;
    const double tz = x.rf(n + 3) - x.rf(x.O(3)) + s * off;
    x.setR(0, x.atan2R(tx, tz) - x.K(0x5193));
    cpu.r[2] = x.rd(x.O(0x15));
    x.call(0x94A7);
    double turn = x.R(0);
    const double f = static_cast<double>(static_cast<int32_t>(tmsFix(frames))) - 1.0;
    if (f != 0.0) turn = x.divide(turn, f);
    x.call(0x52E4);
    double thr = x.rf(x.P(0xF));
    if ((x.rd(0xC8C5) & 0x200u) == 0) thr = 0.0;
    x.wf(x.D(0x24), thr);
    turn *= 2.5;
    if (turn > x.K(0x51A5)) turn = x.K(0x51A6);
    if (turn < x.K(0x51A7)) turn = x.K(0x51A8);
    double steerOut = 0.0;
    const double r1 = x.fc92f() * x.rf(x.D(0x26));
    if (r1 != 0.0) {
        steerOut = x.divide(turn, r1 * x.K(0x51AE) * x.rf(0x2E87));
        if (steerOut < x.K(0x51B1)) steerOut = x.K(0x51B2);
        if (steerOut > x.K(0x51B3)) steerOut = x.K(0x51B4);
    }
    x.wf(x.D(0x21), steerOut);
    x.call(0x67E3);
    x.wr(x.D(0x46), cpu.r[0]);
    x.call(0x2EFD);
    x.setR(2, turn);
    x.call(0x2C18);
    x.wf(x.D(0x21), -x.rf(x.D(0x21)));
    return true;
}

/// 0513A  laço do carro.
void carLoop(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    if (x.rd(0xE5EC) == 0x33BB) {
        x.call(0x2EFD);
        x.call(0x2C39);
    } else if (!aiBody(x)) {
        return;
    }
    // 051BD
    x.linkTick();
    x.sleep(0x513A);
}

} // namespace

void registerDroneAi(SubroutineDispatcher& d) {
    d.add(0x513A, carLoop);
    d.add(0x5140, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        Ctx x{c, b, s};
        if (aiBody(x)) {
            x.linkTick();
            x.sleep(0x513A);
        }
    });
    d.add(0x51C2, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { raceEnd(Ctx{c, b, s}); });
    d.add(0x51EA, parkedLoop);
    d.add(0x51F4, comparePlayer);
    d.add(0x5208, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { leaveWindow(Ctx{c, b, s}, true); });
    d.add(0x520F, aheadLoop);
    d.add(0x5220, idleLoop);
    d.add(0x5225, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { leaveWindow(Ctx{c, b, s}, false); });
    d.add(0x522C, behindLoop);
    d.add(0x523C, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { backInWindow(Ctx{c, b, s}); });
    d.add(0x52A8, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { handBackBehind(Ctx{c, b, s}); });
    d.add(0x52B8, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { followPath(Ctx{c, b, s}); });
    d.add(0x52E4, throttleVariation);
    d.add(0x532B, simpleSpeed);
    d.add(0x538D, markOthers);
    d.add(0x539A, markPlayer);
    d.add(0x53A2, laneMapInit);
    d.add(0x53DA, chooseLane);
    d.add(0x5441, markCar);
    d.add(0x54AA, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        Ctx x{c, b, s};
        x.setR(0, lateralSpeed(x));
    });
    d.add(0x54B5, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        Ctx x{c, b, s};
        x.setR(0, projectedWidth(x));
    });
    d.add(0x54CB, passSound);
    d.add(0x66A7, segmentBlocked);
    d.add(0x94A7, angleDiff);
}

} // namespace rea::core::game
