/**
 * @file car_dynamics.cpp
 * @brief Tradução de `0x2C18`–`0x2C45`, `0x2C92`–`0x2D49`, `0x2D83`–`0x2E0B`, `0x2E73`–`0x2E86`,
 *        `0x2F2C`–`0x3030`, `0x3031`–`0x3049`, `0x3159`–`0x317A` (com `0x3160`) e
 *        `0x9081`–`0x90A5`.
 */
#include "core/game/car_dynamics.hpp"

#include <cmath>

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

/// Contexto: acesso a D (AR5), ao objeto (AR4) e às constantes curtas das instruções.
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
    /// Imediato float curto da instrução em @p pc.
    double K(uint32_t pc) const { return shortFloat(static_cast<uint16_t>(bus.readWord(pc) & 0xFFFFu)); }
    /// Endereço do campo @p o dos dados do carro (AR5).
    uint32_t D(uint32_t o) const { return cpu.ar[5] + o; }
    /// Endereço do campo @p o do objeto (AR4).
    uint32_t O(uint32_t o) const { return cpu.ar[4] + o; }
    /// Float no campo @p o dos dados do carro (AR5).
    double d(uint32_t o) const { return rf(D(o)); }
    /// Grava o float @p v no campo @p o dos dados do carro.
    void setD(uint32_t o, double v) const { wf(D(o), v); }
    /// Chama a sub-rotina traduzida no endereço da ROM indicado (o `CALL` original).
    void call(uint32_t a) const { subs.call(a, cpu, bus); }
    /// `0x9308` (sinal) ou `0x9313` (0..2π) sobre @p v.
    double wrap(double v, bool signedRange = true) const {
        const uint32_t r2 = cpu.r[2];
        cpu.r[2] = doubleToTms(v);
        call(signedRange ? 0x9308 : 0x9313);
        const double out = tmsToDouble(cpu.r[2]);
        cpu.r[2] = r2;
        return out;
    }
    /// Seno/cosseno pela rotina @p a do ângulo @p angle (preserva R2).
    double trig(uint32_t a, double angle) const {
        const uint32_t r2 = cpu.r[2];
        cpu.r[2] = doubleToTms(angle);
        call(a);
        cpu.r[2] = r2;
        return tmsToDouble(cpu.r[0]);
    }
    /// sen(@p a) (`0x92E2`).
    double sinA(double a) const { return trig(0x92E2, a); }
    /// cos(@p a) (`0x92DF`).
    double cosA(double a) const { return trig(0x92DF, a); }
    /// Lê o registrador R@p i como float TMS.
    double R(int i) const { return tmsToDouble(cpu.r[i]); }
    /// Grava @p v, convertido para float TMS, no registrador R@p i.
    void setR(int i, double v) const { cpu.r[i] = doubleToTms(v); }
    /// Quadros da última iteração (`[C92F]`), como double.
    double fc92f() const { return static_cast<double>(static_cast<int32_t>(rd(0xC92F))); }
};

/// 0303B  R0 = rumo de AR2 para AR0 (atan2 − π/2). Preserva R2 e R3.
void headingTo(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    const uint32_t r2 = cpu.r[2], r3 = cpu.r[3];
    x.setR(2, x.rf(cpu.ar[0] + 1) - x.rf(cpu.ar[2] + 1));
    x.setR(3, x.rf(cpu.ar[0] + 3) - x.rf(cpu.ar[2] + 3));
    x.call(0x931A);
    cpu.r[2] = r2;
    cpu.r[3] = r3;
    x.setR(0, x.R(0) - x.K(0x3048));
}

/// 03037  R0 = rumo da pista no nó `[D+0x37]` (0 inteiro se não há nó).
void trackHeading(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = bus.readWord(cpu.ar[5] + 0x37);
    if (cpu.r[0] == 0) return;
    cpu.ar[2] = cpu.r[0];
    cpu.ar[0] = bus.readWord(cpu.ar[2] + 0x21);
    headingTo(cpu, bus, subs);
}

/// 02E73  marcha automática: R0 = `[D+0x38]` ± 1 pela rotação, em 1..4.
void gearSelect(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    int32_t r2 = 0;
    double thr = x.d(0x24);
    if (thr > 1.0) thr = 1.0;
    int32_t r3 = static_cast<int32_t>(tmsFix(thr * 11.0)) + 12;
    int32_t r0 = static_cast<int32_t>(x.rd(x.D(0x38)));
    const int32_t r1 = static_cast<int32_t>(tmsFix(x.d(0x39)));
    if (r1 < r3) r2 = -1;
    r3 += 18;
    if (r1 > r3) r2 = 1;
    r0 += r2;
    if (r0 < 1) r0 = 1;
    if (r0 > 4) r0 = 4;
    cpu.r[0] = static_cast<uint32_t>(r0);
}

/// 02C92  rotação do motor `[D+0x39]` = velocidade × `[[2C46] + marcha]`, até 47.
void engineRpm(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    cpu.ar[0] = x.rd(0x2C46) + x.rd(x.D(0x38));
    x.setR(0, x.d(0x26) * x.rf(cpu.ar[0]));
    if (x.R(0) > 47.0) x.setR(0, 47.0);
    if (static_cast<int32_t>(cpu.r[0]) < 0) cpu.r[0] = 0;  // CMPI/LDILT inteiros sobre o float
    x.wr(x.D(0x39), cpu.r[0]);
}

/// 02C9C  derrapagem `[D+0x25]`.
void slipFactor(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    double r0;
    if (x.rd(x.D(0x31)) != 0) {
        r0 = 1.0;
    } else if (x.rd(x.D(0x38)) != 4 && !(x.d(0x39) < 51.0)) {
        r0 = x.K(0x2CA6);
    } else {
        double r2 = x.wrap(x.d(0x2C) - x.d(0x2D));
        if (r2 > x.K(0x2CAB)) r2 = x.K(0x2CAC);
        if (r2 < x.K(0x2CAD)) r2 = x.K(0x2CAE);
        r0 = std::fabs(r2 * x.K(0x2CB0) + x.d(0x21));
        if (r0 > x.K(0x2CB3)) r0 = x.K(0x2CB4);
        r0 *= x.d(0x26);
        const uint32_t gear = x.rd(x.D(0x38));
        if (!(x.d(0x24) < x.K(0x2CB7)) && gear != 0 && static_cast<int32_t>(gear) <= 2) {
            double r1 = x.d(0x26) - 100.0;
            if (r1 > 0.0) r1 = 0.0;
            r0 += std::fabs(r1) * x.K(0x2CC1);
        }
        r0 *= x.d(0x2B) * 1.0 + 1.0;
        r0 *= x.d(0x24) * 0.25 + 1.0;
        r0 -= 22.0;
        if (r0 < 0.0) r0 = 0.0;
        r0 *= x.K(0x2CCD);
        if (r0 > 1.0) r0 = 1.0;
        r0 *= x.d(0x22);
    }
    x.setR(0, r0);
    x.setD(0x25, r0);
}

/// 02D83  velocidade `[D+0x26]` e velocidade efetiva `[D+0x27]`.
void speedUpdate(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    double r0 = 0.0, r3 = 0.0;
    const bool player = cpu.ar[4] == x.rd(0xE876);
    if (x.rd(x.D(0x20)) != 0) {
        x.wf(x.O(0x12), x.fc92f() * 4.0 + x.rf(x.O(0x12)));
    } else if (x.rd(x.D(0x31)) != 0) {
        r3 = x.rf(0x2917);
    } else {
        r0 = x.d(0x24) * x.d(0x23) * (player ? x.rf(0xE87D) : 1.0);
        double r1 = x.d(0x39) * x.K(0x2D98);
        int32_t i = static_cast<int32_t>(tmsFix(r1));
        if (i > 0x12) i = 0x12;
        const uint32_t torque = x.rd(0x2D68);
        double r2 = x.rf(torque + static_cast<uint32_t>(i));
        ++i;
        double t3 = x.rf(torque + static_cast<uint32_t>(i));
        r1 = static_cast<double>(i) - r1;  // SUBF3 R4,R1,R1 = R4 − R1
        if (r1 < 0.0) r1 = 0.0;
        if (r1 > 1.0) r1 = 1.0;
        r1 = t3 * (1.0 - r1) + r2 * r1;
        r1 *= x.rf(x.rd(0x2D62) + x.rd(x.D(0x38)));
        r1 *= x.rd(x.D(0x56)) == 0 ? x.K(0x2DAE) : 1.0;
        r0 *= r1;
        r0 *= 1.0 - x.d(0x25) * 0.25;
        double slope = 2.0;
        if (x.rd(0xE604) == 0) slope = 0.0;
        if ((x.rd(0xC8C5) & 0x200u) == 0) slope = 0.0;
        if (x.d(0x2B) > 0.5) slope = 0.0;
        r0 += x.rf(x.O(9)) * slope;
        for (uint32_t p = 0; p < 5; ++p) {
            const uint32_t g = x.rd(x.D(5 + 6 * p));
            r3 += (x.rd(g + 0xF) & 0xFF0u) == 0x300u ? x.d(0x44) : x.d(0x45);
        }
        r3 *= x.K(0x2DCD);
        if (player && x.rf(0xE87E) > x.K(0x2DD1)) r3 = (r3 + x.d(0x44)) * 0.5;
        const double slip = x.d(0x25);
        const double r5 = slip * x.rf(0x2916);
        const int32_t gear = static_cast<int32_t>(x.rd(x.D(0x38)));
        if (x.d(0x26) > 100.0 || x.d(0x24) < x.K(0x2DDC) || gear > 2) r3 += r5;
        double brake = x.d(0x2B) * x.rf(0x2915) * (1.0 - slip);
        double m = 1.0;
        if (x.d(0x26) < 40.0) m = 1.5;
        if (x.d(0x26) < 20.0) m = 2.0;
        r3 += brake * m;
        double eng = x.d(0x39) - 42.0;
        if (eng < 0.0) eng = 0.0;
        r3 += (eng * x.K(0x2DF5) + 1.0) * x.rf(x.rd(0x2D7D) + x.rd(x.D(0x38)));
    }
    // 02DF9
    double r5 = 0.0, r1 = x.d(0x26);
    int32_t n = static_cast<int32_t>(x.rd(0xC92F));
    if (n > 6) n = 6;
    for (int32_t k = 0; k < (n < 1 ? 1 : n); ++k) {
        r1 = r1 - r3 * r1 + r0;
        r5 += r1;
    }
    if (r5 < 0.5) r5 = 0.0;
    if (r1 < 0.0) r1 = 0.0;
    x.setD(0x27, r5 * 1.5);
    x.setD(0x26, r1);
}

/// 02CEB  esterço R0 → rumo; devolve em R0 o giro do quadro. Trata o rodopio (`[D+0x31]`).
void steer(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    if (x.rd(x.D(0x1F)) != 0) {
        x.setR(0, 0.0);
        x.setD(0x2C, x.d(0x2E) + x.d(0x2C));
        return;
    }
    const uint32_t spin = x.rd(x.D(0x31));
    if (spin == 0) {
        const double slip = x.d(0x25);
        double r0 = x.R(0) * ((1.0 - slip) * x.K(0x2CF6) + x.K(0x2CF7));
        double r3 = x.d(0x2D), r1 = x.d(0x2C);
        double r2 = x.wrap(r1 - r3) * x.K(0x2CFD);
        r1 += -r2 * 0.5;
        r3 += r2 + r0;
        x.setD(0x2D, r3);
        double r4 = slip * x.d(0x2E) * x.K(0x2D05) * ((x.d(0x24) * 0.5 + 0.5) * x.K(0x2D09));
        r0 += r4;
        x.setD(0x2E, r0);
        x.setD(0x2C, r1 + r0);
        x.setR(0, r0);
        return;
    }
    // 02D10  rodopio
    int32_t ar3 = static_cast<int32_t>(x.rd(0xC92F)) - 1;
    int32_t r1i = static_cast<int32_t>(spin);
    auto finish = [&] {
        // 02D40
        const double h = x.wrap(x.d(0x2C), false);
        x.setD(0x2C, h);
        x.setD(0x2D, h);
        x.wr(x.D(0x31), 0);
        x.setD(0x32, 0.0);
        x.setD(0x2E, 0.0);
        x.setR(0, 0.0);
    };
    if (r1i >= 2) {
        r1i = r1i - 1 - ar3;
        if (r1i <= 2) {
            finish();
            return;
        }
        x.wr(x.D(0x31), static_cast<uint32_t>(r1i));
        x.setD(0x2C, x.d(0x2E) + x.d(0x2C));
        x.setR(0, 0.0);
        return;
    }
    do {
        // 02D1D
        const double w = x.d(0x2E);
        x.setD(0x2C, w + x.d(0x2C));
        const double left = x.d(0x32) - std::fabs(w);
        x.setD(0x32, left);
        if (left >= 0.0) continue;
        x.call(0x3037);
        double r0 = x.R(0);
        const uint32_t f = x.rd(x.O(0xF));
        if ((f & 0xF0Fu) == 0x203u) {
            if ((f & 0xF0u) == 0x20u) {
                double s = x.d(0x2E) * x.K(0x2D2F);
                if (std::fabs(s) < x.K(0x2D31)) s = 0.0;
                x.setD(0x2E, s);
                continue;
            }
            r0 += x.K(0x2D35);
        }
        if (std::fabs(x.wrap(r0 - x.d(0x2C))) < x.K(0x2D3A)) {
            x.setD(0x2C, r0);
            finish();
            return;
        }
    } while (--ar3 >= 0);
    x.setR(0, 0.0);
}

/// 02F2C  carry = algum dos pontos 1..4 sem chão; senão R1 = 1 se algum não é estrada (0x300).
void wheelsOnRoad(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    uint32_t r1 = 0;
    for (uint32_t p = 1; p <= 4; ++p) {
        const uint32_t g = bus.readWord(cpu.ar[5] + 5 + 6 * p);
        if (g == 0) {
            cpu.st |= Tms32031State::kStCarry;
            return;
        }
        if ((bus.readWord(g + 0xF) & 0xF00u) != 0x300u) r1 = 1;
    }
    cpu.st &= ~Tms32031State::kStCarry;
    cpu.r[1] = r1;
}

/// 0316E  som AR2 com volume pela distância `[AR4+0x1C]`: 220 · (1 − d / 50000), limitado a 220;
///        d < 0 ou d > 50000 → mudo (`RETSLT`).
void distanceSound(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    const double v = static_cast<double>(static_cast<int32_t>(x.rd(x.O(0x1C))));
    if (v < 0.0) return;
    double r0 = 1.0 - v / (10000.0 * 5.0);
    if (r0 < 0.0) return;
    int32_t vol = static_cast<int32_t>(tmsFix(r0 * 220.0));
    if (vol > 0xDC) vol = 0xDC;
    cpu.r[0] = static_cast<uint32_t>(vol);
    x.call(0x9186);
}

/// 03159 / 03168  som aleatório da tabela AR2 (R0 entradas); 03168 cai na `0x316E` (volume pela distância).
void randomSound(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, bool byspeed) {
    Ctx x{cpu, bus, subs};
    const uint32_t table = cpu.ar[2];
    cpu.ar[2] = cpu.r[0];
    x.call(0x8EDB);
    cpu.ar[2] = x.rd(table + cpu.r[0]);
    if (!byspeed) {
        x.call(0x9188);
        return;
    }
    distanceSound(cpu, bus, subs);
}

/// 02FC4  rodopio após a batida: rumo desejado = pista + R2, giro aleatório e ângulo a girar.
void wallSpin(Ctx& x, double r2, double track) {
    x.setD(0x2D, r2 + track);
    const double w = x.wrap(track - x.d(0x2C));
    x.setR(0, x.K(0x2FC9));
    x.call(0x8EC2);
    double spinRate = x.R(0) + x.K(0x2FCB);
    if (w >= 0.0) spinRate = -spinRate;
    x.setD(0x2E, spinRate);
    const double turn = std::fabs(w) + x.K(0x2FD1);
    double total = turn;
    if (turn > 1.0) total = x.K(0x2FD4);
    if (x.d(0x26) < 30.0) total = turn;
    x.setD(0x32, total);
    x.wr(x.D(0x31), 1);
}

/// 02FB0  cauda comum: R2 = desvio em relação à pista (@p track), @p a = |desvio| (R3).
///        Também é o destino do salto `BU $2FB0` de `0x3022` (com R2 sem normalizar).
void wallTail(Ctx& x, double r2, double a, double track) {
    if (a > x.K(0x2FB0)) return wallSpin(x, r2, track);
    if (a > x.K(0x2FB2)) {
        // 02FDC
        x.setD(0x2D, r2 + track);
        const double w = x.wrap(track - x.d(0x2C));
        x.setD(0x32, std::fabs(w));
        x.setD(0x2E, w * x.K(0x2FE3));
        x.wr(x.D(0x31), 1);
        return;
    }
    r2 *= x.K(0x2FB4);
    a *= x.K(0x2FB5);
    if (a < x.K(0x2FB6)) {
        if (r2 < 0.0) r2 = x.K(0x2FB9);
        else if (r2 > 0.0) r2 = x.K(0x2FBA);
    }
    if (a > x.K(0x2FBB)) {
        if (r2 < 0.0) r2 = x.K(0x2FBE);
        else if (r2 > 0.0) r2 = x.K(0x2FBF);
    }
    r2 += track;
    x.setD(0x2D, r2);
    x.setD(0x2C, r2);
}

/// 02F75  impacto no muro (algum ponto 1..4 sem chão): som, empurrão de volta para a pista e
///        alinhamento do rumo ou rodopio.
void wallImpact(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    cpu.ar[2] = x.rd(0x3089);
    cpu.r[0] = 3;
    if ((x.rd(x.O(0xF)) & 0xF00u) != 0x100u) {
        x.call(0x3168);
        x.setD(0x26, x.d(0x26) * x.K(0x2F7D));
    } else {
        x.call(0x3159);
    }
    // 02F83
    x.call(0x3031);
    const double toNext = x.R(0);
    x.call(0x3037);
    const double track = x.R(0);
    const double r3 = x.wrap(track - toNext);
    const uint32_t r3bits = doubleToTms(r3);
    const double r1 = x.d(0x2D);
    const double diff = x.wrap(track - r1);
    double v = x.d(0x26);
    if (v < 15.0) v = 15.0;
    x.setD(0x26, v);
    double r5 = x.fc92f() * v;
    const double push = track + (r3 < 0.0 ? x.K(0x2F96) : x.K(0x2F94));
    r5 *= std::fabs(x.sinA(push - r1));
    const double dz = r5 * x.cosA(push);
    const double dx = -x.sinA(push) * r5;
    x.wf(x.O(1), dx + x.rf(x.O(1)));
    x.wf(x.O(3), dz + x.rf(x.O(3)));
    x.call(0x88EB);
    double r2 = diff;
    if (static_cast<int32_t>(doubleToTms(r2) ^ r3bits) >= 0) r2 = -r2;  // XOR inteiro dos floats
    // 02FAD
    if (x.rd(x.D(0x31)) != 0) return wallSpin(x, r2, track);
    wallTail(x, r2, std::fabs(r2), track);
}

/// 02F6B  batida no muro: algum ponto 1..4 fora do chão.
void wallHit(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    if (static_cast<int32_t>(x.rd(x.O(0x1C))) >> 1 > 0x7D00) return;
    if ((x.rd(0xC8C5) & 0x200u) == 0) return;
    x.call(0x2F2C);
    if (!cpu.carry()) return;
    wallImpact(cpu, bus, subs);
}

/// 02FE8  algum ponto fora da estrada (mas com chão): puxa o rumo `[D+0x2C]`/`[D+0x2D]` de volta
///        para o rumo da pista, limita a velocidade a 20 e, se o desvio passar de 0,6, cai na
///        cauda da batida (`BU $2FB0`).
void offRoad(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    x.call(0x3037);
    const double r1 = x.R(0);
    x.call(0x3031);
    const double r4 = x.wrap(x.R(0) - r1);  // SUBF3 R0,R1,R2 = R0 − R1
    double r5 = r4 < 0.0 ? x.K(0x2FEE) : x.K(0x2FEF);
    const int32_t spin = static_cast<int32_t>(x.rd(x.D(0x31)));
    if (spin != 0) {
        if (spin < 2) {
            double r2 = std::fabs(x.wrap(x.d(0x2D) - r1));
            if (r2 < x.K(0x2FF8)) r2 = x.K(0x2FF9);
            r2 = r2 * (r4 < 0.0 ? x.K(0x2FFB) : x.K(0x2FFC)) + r1;
            x.setD(0x2D, r2);
            return;
        }
        if (x.d(0x26) < 80.0) return;
    } else {
        // 03005
        const double r2 = x.wrap(x.d(0x2C) - r1);
        double r6 = r5 - r2;  // SUBF3 R5,R2,R6
        bool apply = true;
        if (r4 < 0.0) {
            if (r6 > 0.0) apply = false;
            else if (r6 < x.K(0x3011)) r6 = x.K(0x3012);
        } else {
            if (r6 < 0.0) apply = false;
            else if (r6 > x.K(0x300C)) r6 = x.K(0x300D);
        }
        if (apply) x.setD(0x2C, r6 + x.d(0x2C));
    }
    // 03015
    if (x.d(0x26) < 20.0) x.setD(0x26, 20.0);
    const double r2 = x.wrap(x.d(0x2D) - r1);
    if (std::fabs(r2) >= x.K(0x301D)) {
        // 0301F  R0 = rumo da pista, R2 = R0 − [D+0x2D] (sem normalizar), R3 = |R2 anterior|
        const double d2d = x.d(0x2D);
        return wallTail(x, r1 - d2d, std::fabs(r2), r1);
    }
    r5 -= r2;
    if (r4 < 0.0) {
        if (r5 > 0.0) return;
        if (r5 < x.K(0x302C)) r5 = x.K(0x302D);
    } else {
        if (r5 < 0.0) return;
        if (r5 > x.K(0x3027)) r5 = x.K(0x3028);
    }
    x.setD(0x2D, r5 + x.d(0x2D));
}

/// 02F43  com o bit 0x400 do modo, sem rodopio e `[obj+8]` ≤ 0,5: alinha o carro com a pista
///        (15 quadros de giro, velocidade mínima 40).
void autoAlign(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    if ((x.rd(0xC8C5) & 0x400u) == 0) return;
    if (x.rd(0xE873) != 0) return;
    if (x.rf(x.O(8)) > 0.5) return;
    x.call(0x3031);
    const double a = x.R(0);
    x.call(0x3037);
    x.setD(0x2D, x.wrap((a + x.R(0)) * 0.5));
    x.call(0x3037);
    x.setR(0, x.wrap(x.R(0) - x.d(0x2C)));
    x.setR(1, 15.0);
    x.call(0xA509);
    x.wr(x.D(0x2E), cpu.r[0]);
    x.wr(x.D(0x31), 15);
    x.setD(0x32, 0.0);
    if (x.d(0x26) < 40.0) x.setD(0x26, 40.0);
}

/// 02F63  colisões do carro do jogador com as bordas: só com o bit 0x200 do modo.
///        `0x2F43`, depois `0x2F2C`: ponto sem chão → `0x2F75`; ponto fora da estrada → `0x2FE8`.
void playerEdges(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    if ((bus.readWord(0xC8C5) & 0x200u) == 0) return;
    subs.call(0x2F43, cpu, bus);
    subs.call(0x2F2C, cpu, bus);
    if (cpu.carry()) return wallImpact(cpu, bus, subs);
    if (cpu.r[1] != 0) offRoad(cpu, bus, subs);
}

/// 02C18  passo de direção do carro (R2 = esterço pedido).
void driveStep(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx x{cpu, bus, subs};
    const uint32_t steerIn = cpu.r[2];
    x.call(0x2E73);
    x.wr(x.D(0x38), cpu.r[0]);
    x.call(0x2D83);
    x.call(0x2C9C);
    x.call(0x2C92);
    cpu.r[0] = steerIn;
    x.call(0x2CEB);
    const uint32_t turn = cpu.r[0];
    x.call(0x2F6B);
    cpu.r[2] = turn;
    cpu.ar[2] = x.rd(0x5A);
    x.call(0x938A);
    cpu.r[2] = x.O(4);
    cpu.r[3] = cpu.r[2];
    x.call(0x9494);
    x.setR(2, x.d(0x2D) - x.d(0x2C));
    cpu.ar[2] = x.rd(0x5B);
    x.call(0x938A);
    cpu.r[2] = x.O(4);
    cpu.r[3] = cpu.ar[2];
    x.call(0x9494);
    cpu.r[2] = x.rd(x.D(0x27));
    cpu.ar[2] = x.rd(0x5B);
    if (x.rd(x.D(0x20)) == 0) {
        cpu.r[3] = x.O(0x11);
        x.call(0x9099);  // [obj+0x11..0x13] = [0x5B]·(0, 0, R2)
    }
    x.call(0x9081);
    // 02C39  sem RETS: segue direto na física do carro (contato, rumo +0x15, matriz, distância lateral)
    x.call(0x2C39);
}

} // namespace

void registerCarDynamics(SubroutineDispatcher& d) {
    d.add(0x2C18, driveStep);
    d.add(0x2C92, engineRpm);
    d.add(0x2C9C, slipFactor);
    d.add(0x2CEB, steer);
    d.add(0x2D83, speedUpdate);
    d.add(0x2E73, gearSelect);
    d.add(0x2F2C, wheelsOnRoad);
    d.add(0x2F6B, wallHit);
    d.add(0x2F75, wallImpact);
    d.add(0x2F43, autoAlign);
    d.add(0x2F63, playerEdges);
    d.add(0x2FE8, offRoad);
    d.add(0x3037, trackHeading);
    d.add(0x303A, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[0] = b.readWord(c.ar[2] + 0x21);
        headingTo(c, b, s);
    });
    d.add(0x3031, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[2] = b.readWord(c.ar[5] + 0x37);
        c.ar[0] = b.readWord(c.ar[2] + 0x21);
        c.ar[2] = c.ar[4];
        headingTo(c, b, s);
    });
    d.add(0x3035, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[0] = c.ar[4];
        headingTo(c, b, s);
    });
    d.add(0x3159, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { randomSound(c, b, s, false); });
    d.add(0x3168, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { randomSound(c, b, s, true); });
    d.add(0x316E, distanceSound);
    // 03160  som aleatório da tabela AR2 (R0 entradas) com o volume R1 (`0x9186`).
    d.add(0x3160, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t table = c.ar[2];
        c.ar[2] = c.r[0];
        s.call(0x8EDB, c, b);
        c.ar[2] = b.readWord(table + c.r[0]);
        c.r[0] = c.r[1];
        s.call(0x9186, c, b);
    });
    // 09099  [R3..R3+2] = matriz AR2 · (0, 0, R2) (vetor montado na pilha + 0x941E). R0 = 0,0;
    //        preserva AR2 e R2.
    d.add(0x9099, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        const double z = tmsToDouble(c.r[2]);
        const uint32_t m = c.ar[2];
        double out[3];
        for (uint32_t row = 0; row < 3; ++row) out[row] = tmsToDouble(b.readWord(m + row * 3 + 2)) * z;
        for (uint32_t row = 0; row < 3; ++row) b.writeWord(c.r[3] + row, doubleToTms(out[row]));
        c.r[0] = doubleToTms(0.0);
    });
    d.add(0x9081, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        for (uint32_t i = 1; i <= 3; ++i)
            b.writeWord(c.ar[4] + i, doubleToTms(tmsToDouble(b.readWord(c.ar[4] + 0x10 + i)) +
                                                 tmsToDouble(b.readWord(c.ar[4] + i))));
    });
    d.add(0x908B, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        const double n = static_cast<double>(static_cast<int32_t>(b.readWord(0xC92F)));
        for (uint32_t i = 1; i <= 3; ++i)
            b.writeWord(c.ar[4] + i, doubleToTms(tmsToDouble(b.readWord(c.ar[4] + 0x10 + i)) * n +
                                                 tmsToDouble(b.readWord(c.ar[4] + i))));
    });
}

} // namespace rea::core::game
