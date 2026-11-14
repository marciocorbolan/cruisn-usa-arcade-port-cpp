/**
 * @file demo_camera.cpp
 * @brief Tradução de `0x5624`–`0x58DC` e `0x58EF`–`0x5942` (roteiro de câmera da demo).
 */
#include "core/game/demo_camera.hpp"

#include <cmath>

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

/// Acesso compacto ao estado durante a tradução.
struct X {
    Tms32031State& c;  ///< Registradores da CPU TMS320C31.
    MemoryBus& b;  ///< Barramento de memória simulado.
    ISubroutines& s;  ///< Despachante das sub-rotinas traduzidas.

    uint32_t p(uint32_t n) const { return c.ar[7] + n; }                      ///< AR7 + n
    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) { return b.readWord(a); }
    /// Lê a palavra em @p a como inteiro com sinal.
    int32_t ri(uint32_t a) { return static_cast<int32_t>(b.readWord(a)); }
    /// Lê o float TMS em @p a.
    double rf(uint32_t a) { return tmsToDouble(b.readWord(a)); }
    double fl(uint32_t a) { return static_cast<double>(ri(a)); }               ///< FLOAT *a
    /// Grava a palavra @p v em @p a.
    void wr(uint32_t a, uint32_t v) { b.writeWord(a, v); }
    /// Grava o float @p v em @p a.
    void wf(uint32_t a, double v) { b.writeWord(a, doubleToTms(v)); }
    /// Lê o registrador R@p n como float TMS.
    double R(int n) const { return tmsToDouble(c.r[n]); }
    /// Grava @p v, convertido para float TMS, em R@p n.
    void setR(int n, double v) { c.r[n] = doubleToTms(v); }
    /// Chama a sub-rotina traduzida em @p a.
    void call(uint32_t a) { s.call(a, c, b); }
};

constexpr uint32_t kTrackNodes = 0xAA2F;  ///< `[AA2F]`: nós da pista (4 palavras).
constexpr uint32_t kTrackList  = 0xE46E;  ///< Lista de trechos (`+0x1E` índice, `+0x21` próximo).
constexpr uint32_t kObjects    = 0x0057;  ///< `[0x57]`: lista de objetos (próximo em `+0x20`).
constexpr uint32_t kMatA       = 0x005A;  ///< `[0x5A]`: matriz de rascunho.
constexpr uint32_t kMatB       = 0x005C;  ///< `[0x5C]`: matriz de rascunho.
constexpr uint32_t kVecIn      = 0x005D;  ///< `[0x5D]`: vetor de rascunho.
constexpr uint32_t kVecOut     = 0x005E;  ///< `[0x5E]`: vetor de rascunho.
constexpr uint32_t kCarType    = 0x216;   ///< `[obj+0xF]` dos carros.

/// Imediatos curtos usados aqui.
inline double k(uint16_t v) { return shortFloat(v); }

// ---------------------------------------------------------------------------------------------
// Pista e carros

/// 0585A  AR4 = primeiro nó com índice (+3) ≥ R2.
void nodeByIndex(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    cpu.ar[4] = x.rd(kTrackNodes) + 3;
    do {
        cpu.r[0] = x.rd(cpu.ar[4]);
        cpu.ar[4] += 4;
    } while (static_cast<int32_t>(cpu.r[0]) < static_cast<int32_t>(cpu.r[2]));
    cpu.ar[4] -= 7;
}

/// 05847  AR2 = trecho de `[E46E]` com índice R2 (0 se não houver).
void findSegment(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    cpu.ar[2] = x.rd(kTrackList);
    for (;;) {
        cpu.r[0] = x.rd(cpu.ar[2] + 0x1E);
        if (cpu.r[2] == cpu.r[0]) return;
        cpu.ar[2] = x.rd(cpu.ar[2] + 0x21);
        if (cpu.ar[2] == 0) return;
    }
}

/// 0584F  AR4 = nó da pista do carro AR0. Preserva R2.
void carNode(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    const uint32_t r2 = cpu.r[2];
    cpu.ar[2] = x.rd(cpu.ar[0] + 0x17);
    cpu.ar[4] = x.rd(cpu.ar[2] + 0x12);
    cpu.r[0] = x.rd(cpu.ar[2] + 0x25);
    if (cpu.r[0] == 0) {
        cpu.ar[4] = x.rd(cpu.ar[0] + 0x1B);
        cpu.ar[4] = x.rd(cpu.ar[4] + 0x37);
        cpu.r[2] = x.rd(cpu.ar[4] + 0x1E);
        x.call(0x585A);
    }
    cpu.r[2] = r2;
}

/// 057F0  AR4 = o R2-ésimo carro mais adiantado (R2 ≥ 1).
void nthLeader(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    cpu.ar[3] = x.p(0x2C);
    cpu.r[0] = 0;
    for (uint32_t i = 0; i < 10; ++i) x.wr(cpu.ar[3]++, cpu.r[0]);
    cpu.ar[3] = x.p(0x2C);
    cpu.ir0 = 0;
    cpu.ar[1] = 0;
    cpu.r[3] = 0x7FFFu * 0x7FFFu;
    do {
        // 057FB
        cpu.r[4] = 0;
        cpu.ar[0] = x.rd(kObjects) - 0x20;
        for (;;) {
            cpu.ar[0] = x.rd(cpu.ar[0] + 0x20);
            if (cpu.ar[0] == 0) break;
            if (x.rd(cpu.ar[0] + 0xF) != kCarType) continue;
            x.call(0x584F);
            const auto node = static_cast<int32_t>(cpu.ar[4]);
            if (static_cast<int32_t>(cpu.r[3]) < node) continue;
            if (static_cast<int32_t>(cpu.r[4]) > node) continue;
            // 05809  já escolhido?
            bool taken = false;
            for (cpu.ir1 = cpu.ir0 - 1; static_cast<int32_t>(cpu.ir1) >= 0; --cpu.ir1) {
                if (x.rd(cpu.ar[3] + cpu.ir1) == cpu.ar[0]) {
                    taken = true;
                    break;
                }
            }
            if (taken) continue;
            cpu.r[4] = cpu.ar[4];
            cpu.ar[1] = cpu.ar[0];
        }
        // 05813
        x.wr(cpu.ar[3] + cpu.ir0, cpu.ar[1]);
        cpu.ir0 += 1;
        cpu.r[3] = cpu.r[4];
        cpu.r[2] -= 1;
    } while (static_cast<int32_t>(cpu.r[2]) > 0);
    if (cpu.ar[1] == 0) {
        x.call(0x581B);  // 0581B: BU $ (trava sem carros)
        return;
    }
    cpu.ar[4] = cpu.ar[1];
}

/// 0581E  AR1 = carro mais próximo à frente da câmera (±0,78 rad); R0 = R5 = distância.
void nearestInView(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    double r5 = 30000.0;
    for (int i = 0; i < 4; ++i) r5 = tmsToDouble(doubleToTms(r5 * r5));  // satura no máximo
    x.setR(5, r5);
    cpu.ar[0] = x.rd(kObjects) - 0x20;
    cpu.ar[1] = 0;
    for (;;) {
        cpu.ar[0] = x.rd(cpu.ar[0] + 0x20);
        if (cpu.ar[0] == 0) break;
        if (x.rd(cpu.ar[0] + 0xF) != kCarType) continue;
        x.setR(2, x.rf(cpu.ar[0] + 3) - x.rf(x.p(0x19)));
        x.setR(3, x.rf(cpu.ar[0] + 1) - x.rf(x.p(0x17)));
        x.call(0x931A);
        x.call(0x58E8);
        x.setR(0, x.R(0) - x.rf(x.p(0x1B)));
        x.call(0x58E8);
        x.setR(0, x.R(0) - x.rf(x.p(0x12)));
        x.call(0x58E8);
        x.setR(0, std::fabs(x.R(0)));
        if (x.R(0) > k(0xF47B)) continue;
        const double dx = x.rf(cpu.ar[0] + 1) - x.rf(x.p(0x17));
        const double dz = x.rf(cpu.ar[0] + 3) - x.rf(x.p(0x19));
        x.setR(0, dx * dx);
        x.setR(1, dz * dz);
        x.setR(2, x.R(0) + x.R(1));
        x.call(0xA5F1);
        if (x.R(5) > x.R(0)) {
            cpu.r[5] = cpu.r[0];
            cpu.ar[1] = cpu.ar[0];
        }
    }
    cpu.r[0] = cpu.r[5];
}

/// 057DB  R0 = direção da pista no nó AR4 (atan2 de dx, dz até o nó seguinte, +8).
///        Preserva AR1, R2 e R3.
void trackHeading(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    const uint32_t ar1 = cpu.ar[1], r2 = cpu.r[2], r3 = cpu.r[3];
    cpu.ar[1] = cpu.ar[4] + 8;
    x.setR(2, static_cast<double>(x.ri(cpu.ar[1] + 2) - x.ri(cpu.ar[4] + 2)));
    x.setR(3, static_cast<double>(x.ri(cpu.ar[1]) - x.ri(cpu.ar[4])));
    x.call(0x931A);
    x.call(0x58E8);
    cpu.r[3] = r3;
    cpu.r[2] = r2;
    cpu.ar[1] = ar1;
}

/// 057C2  anda R3 na direção da pista (rotação Y por −direção) e soma em `AR7+0x17`/`+0x19`.
void moveAlongTrack(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    const uint32_t dist = cpu.r[3];
    cpu.ar[1] = cpu.ar[4] + 8;
    x.call(0x57DB);
    x.setR(0, -x.R(0));
    cpu.r[2] = cpu.r[0];
    cpu.ar[2] = x.rd(kMatA);
    x.call(0x938A);
    cpu.ar[2] = x.rd(kVecIn);
    x.wf(cpu.ar[2] + 1, 0.0);
    x.wf(cpu.ar[2] + 2, 0.0);
    cpu.r[0] = dist;
    x.wr(cpu.ar[2], cpu.r[0]);
    cpu.r[3] = x.rd(kVecOut);
    cpu.r[2] = x.rd(kMatA);
    x.call(0x941E);
    cpu.ar[2] = x.rd(kVecOut);
    x.setR(0, x.rf(cpu.ar[2]) + x.rf(x.p(0x17)));
    x.wr(x.p(0x17), cpu.r[0]);
    x.setR(0, x.rf(cpu.ar[2] + 2) + x.rf(x.p(0x19)));
    x.wr(x.p(0x19), cpu.r[0]);
}

/// 057AE  `[AR7+0x37]` = recuo lateral: ±1000 conforme o lado (`[[[AR7+0x28]+0x17]+0x20]`),
///        aproximado em 30 %; com R2 ≠ 1, interpola com o valor antigo por R2.
void lateralOffset(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    cpu.ar[0] = x.rd(x.p(0x28));
    cpu.ar[0] = x.rd(cpu.ar[0] + 0x17);
    double r1 = 1000.0;
    double r0 = x.rf(cpu.ar[0] + 0x20);
    if (!(r0 > 0.0)) r1 = -r1;
    x.setR(1, r1);
    r0 = (r0 - r1) * k(0xE19A) + r1;
    if (x.R(2) != 1.0) r0 = (r0 - x.rf(x.p(0x37))) * x.R(2) + x.rf(x.p(0x37));
    x.setR(0, r0);
    x.wr(x.p(0x37), cpu.r[0]);
}

/// 0576E  avança AR4 enquanto o nó seguinte está mais perto da câmera (AR5 −1 por nó).
void advanceNode(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    for (;;) {
        const double cx = x.rf(x.p(0x17)), cz = x.rf(x.p(0x19));
        const double ax = x.fl(cpu.ar[4]) - cx, az = x.fl(cpu.ar[4] + 2) - cz;
        x.setR(0, ax * ax + az * az);
        cpu.ar[2] = cpu.ar[4] + 4;
        const double bx = x.fl(cpu.ar[2]) - cx, bz = x.fl(cpu.ar[2] + 2) - cz;
        x.setR(2, bz * bz);
        x.setR(1, bx * bx + bz * bz);
        if (x.R(0) <= x.R(1)) return;
        cpu.ar[4] += 4;
        cpu.ar[5] -= 1;
    }
}

/// 058D0  R0 = wrap(R0); amortece por R2 se `[AR5+0x31]` ≠ 1 e |R0| < 0,78; senão
///        mantém R0 e `[AR7+0xD]` = 1 (corte seco).
void dampTurn(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    x.call(0x58E8);
    cpu.r[3] = x.rd(cpu.ar[5] + 0x31);
    if (cpu.r[3] != 1) {
        cpu.r[3] = cpu.r[0];
        x.setR(0, x.R(0) * x.R(2));
        x.setR(2, std::fabs(x.R(3)));
        if (x.R(2) < k(0xF47B)) return;
        cpu.r[0] = cpu.r[3];
    }
    cpu.r[3] = 1;
    x.wr(x.p(0xD), cpu.r[3]);
}

/// 05784  mira o carro: rumo da câmera para (nó+8 + recuo lateral), amortecido por
///        R4 = `[AR7+0x27]`·`[C92F]`/5000.
void aimAtCar(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    x.setR(0, x.rf(x.p(0x27)) * static_cast<double>(x.ri(0xC92F)));
    x.setR(1, 5000.0);
    x.call(0xA509);
    cpu.r[4] = cpu.r[0];
    x.call(0x57DB);
    x.setR(0, -x.R(0));
    cpu.r[2] = cpu.r[0];
    cpu.ar[2] = x.rd(kMatA);
    x.call(0x938A);
    cpu.ar[2] = x.rd(kVecIn);
    x.wf(cpu.ar[2] + 1, 0.0);
    x.wf(cpu.ar[2] + 2, 0.0);
    x.wr(cpu.ar[2], x.rd(x.p(0x37)));
    cpu.r[3] = x.rd(kVecOut);
    cpu.r[2] = x.rd(kMatA);
    x.call(0x941E);
    cpu.ar[1] = cpu.ar[4] + 8;
    cpu.ar[2] = x.rd(kVecOut);
    x.setR(2, x.fl(cpu.ar[1] + 2) + x.rf(cpu.ar[2] + 2) - x.rf(x.p(0x19)));
    x.setR(3, x.fl(cpu.ar[1]) + x.rf(cpu.ar[2]) - x.rf(x.p(0x17)));
    x.call(0x931A);
    x.call(0x58E8);
    x.setR(0, x.R(0) + x.rf(x.p(0x12)));
    x.call(0x58E8);
    x.setR(0, x.R(0) - x.rf(x.p(0x1B)));
    cpu.r[2] = cpu.r[4];
    x.call(0x58D0);
    x.setR(0, x.R(0) + x.rf(x.p(0x1B)));
    x.wr(x.p(0x1B), cpu.r[0]);
    x.wf(x.p(0x1A), 0.0);
    x.wf(x.p(0x1C), 0.0);
}

/// 05719  perseguição: anda `[AR7+0x27]`·`[C92F]`, acompanha a altura e mira.
///        Parado quando `[C941]` = 0 e `[E5EC]` = 0x33BB.
void chaseFrame(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    if (x.rd(0xC941) == 0 && x.rd(0xE5EC) == 0x33BB) return;
    x.call(0x5784);
    // 0571F  direção de avanço: ângulos (X, Y − Y0, Z)
    cpu.ar[2] = x.rd(kVecOut);
    x.wr(cpu.ar[2], x.rd(x.p(0x1A)));
    x.wr(cpu.ar[2] + 2, x.rd(x.p(0x1C)));
    x.setR(0, x.rf(x.p(0x1B)) - x.rf(x.p(0x12)));
    x.call(0x58E8);
    x.setR(0, -x.R(0));
    x.wr(cpu.ar[2] + 1, cpu.r[0]);
    cpu.r[2] = x.rd(kVecOut);
    cpu.ar[2] = x.rd(kMatA);
    x.call(0x9343);
    cpu.ar[2] = x.rd(kVecIn);
    x.wf(cpu.ar[2], 0.0);
    x.wf(cpu.ar[2] + 1, 0.0);
    x.setR(1, static_cast<double>(x.ri(0xC92F)));
    x.setR(0, x.rf(x.p(0x27)) * x.R(1));
    x.wr(cpu.ar[2] + 2, cpu.r[0]);
    cpu.r[3] = x.rd(kVecOut);
    cpu.r[2] = x.rd(kMatA);
    x.call(0x941E);
    cpu.ar[2] = x.rd(kVecOut);
    x.setR(0, x.rf(cpu.ar[2]) + x.rf(x.p(0x17)));
    x.wr(x.p(0x17), cpu.r[0]);
    x.setR(0, x.rf(cpu.ar[2] + 2) + x.rf(x.p(0x19)));
    x.wr(x.p(0x19), cpu.r[0]);
    // 0573E  altura: chão (0x1F8B, carry) ou rampa entre os nós
    const uint32_t ar4 = cpu.ar[4];
    cpu.ar[4] = x.p(0x17);
    x.call(0x1F8B);
    cpu.ar[4] = ar4;
    if (cpu.carry()) {
        cpu.r[1] = x.rd(x.p(0x39));
        x.setR(0, x.R(0) + x.R(1) + x.rf(x.p(0x18)));
        x.wr(x.p(0x18), cpu.r[0]);
    } else {
        // 05749
        cpu.ar[2] = cpu.ar[4] + 4;
        const double dz = static_cast<double>(x.ri(cpu.ar[2] + 2) - x.ri(cpu.ar[4] + 2));
        const double dx = static_cast<double>(x.ri(cpu.ar[2]) - x.ri(cpu.ar[4]));
        x.setR(0, dz * dz);
        x.setR(1, dx * dx);
        x.setR(2, x.R(0) + x.R(1));
        x.call(0xA5F1);
        cpu.r[1] = cpu.r[0];
        x.setR(0, static_cast<double>(x.ri(cpu.ar[2] + 1) - x.ri(cpu.ar[4] + 1)) * k(0x0066));
        x.call(0xA509);
        cpu.ar[0] = x.rd(kVecIn);
        x.setR(0, x.R(0) * x.rf(cpu.ar[0] + 2) + x.rf(x.p(0x18)));
        x.wr(x.p(0x18), cpu.r[0]);
        x.setR(1, (static_cast<double>(x.ri(cpu.ar[4] + 1) - 400) - x.rf(x.p(0x18))) * k(0xB23D) +
                      x.rf(x.p(0x18)));
        x.wr(x.p(0x18), cpu.r[1]);
    }
    // 05767
    cpu.r[2] = x.p(0x1A);
    cpu.ar[2] = x.p(0x1D);
    x.call(0x9343);
    x.call(0x576E);
}

/// 0569E  posiciona a câmera de perseguição. R0 = passo de nós, R1 = nº de passos para trás,
///        R2 = posição do carro (`0x57F0`), R3 = ângulo Y relativo.
void chaseSetup(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    x.wr(x.p(0xF), cpu.r[0]);
    x.wr(x.p(0x10), cpu.r[1]);
    x.wr(x.p(0x11), cpu.r[2]);
    x.wr(x.p(0x12), cpu.r[3]);
    x.wf(x.p(0x2A), 0.0);
    x.wr(x.p(0xD), 1);
    cpu.r[2] = x.rd(x.p(0x11));
    x.call(0x57F0);
    cpu.ar[0] = cpu.ar[4];
    x.wr(x.p(0x28), cpu.ar[4]);
    x.wr(0xE876, cpu.ar[4]);
    cpu.ar[1] = x.rd(cpu.ar[4] + 0x1B);
    x.wr(0xE878, cpu.ar[1]);
    x.call(0x584F);
    cpu.r[0] = static_cast<uint32_t>(x.ri(x.p(0x10)) * x.ri(x.p(0xF)));
    cpu.ar[4] -= cpu.r[0];
    if (x.rd(0xC941) == 0) {
        for (;;) {
            cpu.r[2] = x.rd(cpu.ar[4] + 3);
            x.call(0x5847);
            if (cpu.ar[2] != 0) break;
            cpu.ar[4] += x.rd(x.p(0xF));
        }
    }
    // 056BA
    x.call(0x57DB);
    x.wr(x.p(0x1B), cpu.r[0]);
    x.wf(x.p(0x17), x.fl(cpu.ar[4]));
    cpu.r[0] = tmsFix(x.rf(x.p(0x39))) + x.rd(cpu.ar[4] + 1);
    x.wf(x.p(0x18), static_cast<double>(static_cast<int32_t>(cpu.r[0])));
    x.setR(0, x.fl(cpu.ar[4] + 2));
    x.wr(x.p(0x19), cpu.r[0]);
}

/// 05624/05671/05680/0568F  início de perseguição (altura, R0..R3 de 0x569E, por quadro).
void chaseStart(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, int32_t height, int32_t step,
                uint32_t carPos, uint16_t yaw, uint32_t follow) {
    X x{cpu, bus, subs};
    cpu.r[0] = static_cast<uint32_t>(height);
    x.setR(1, static_cast<double>(height));
    x.wr(x.p(0x39), cpu.r[1]);
    cpu.r[0] = static_cast<uint32_t>(step);
    cpu.r[1] = 1;
    cpu.r[2] = carPos;
    x.setR(3, k(yaw));
    x.call(0x569E);
    x.setR(2, 1.0);
    x.call(0x57AE);
    x.call(0x57BF);
    x.call(follow);
    x.call(0x5943);
    x.call(0x7298);
}

/// 056E6/05710  velocidade = velocidade do carro (`[[car+0x1B]+0x26]`) × fator; 0x5719.
void matchSpeed(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint16_t factor) {
    X x{cpu, bus, subs};
    cpu.ar[0] = x.rd(x.p(0x28));
    cpu.ar[1] = x.rd(cpu.ar[0] + 0x1B);
    x.setR(0, x.rf(cpu.ar[1] + 0x26) * k(factor));
    x.wr(x.p(0x27), cpu.r[0]);
    x.setR(2, k(0xF19A));
    x.call(0x57AE);
    x.call(0x5719);
}

/// 056EF  troca para o carro mais próximo à frente e regula a velocidade pela distância.
void followNearest(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    x.call(0x581E);
    if (cpu.ar[1] == 0) {
        cpu.ar[5] = 0;
    } else {
        if (cpu.ar[1] != x.rd(x.p(0x28))) {
            x.setR(2, 0.0);
            x.wr(x.p(0x2A), cpu.r[2]);
            x.wr(x.p(0x28), cpu.ar[1]);
            x.wr(0xE876, cpu.ar[1]);
            cpu.ar[0] = x.rd(cpu.ar[1] + 0x1B);
            x.wr(0xE878, cpu.ar[0]);
            x.wr(x.p(0x29), cpu.r[0]);
        }
        // 056FC  aceleração ±[B0AF] conforme a distância passa de 0,6 × a inicial
        x.setR(3, x.rf(0xB0AF));
        double r2 = x.rf(x.p(0x2A));
        x.setR(1, x.rf(x.p(0x29)) * k(0xF19A));
        if (!(x.R(1) < x.R(0))) x.setR(3, -x.R(3));
        r2 += x.R(3);
        if (r2 < 0.0) r2 = 0.0;
        x.setR(2, r2);
        x.wr(x.p(0x2A), cpu.r[2]);
        x.setR(2, x.R(2) * x.R(0) + k(0x04CD));
        cpu.ar[0] = x.rd(cpu.ar[1] + 0x1B);
        x.setR(0, x.rf(cpu.ar[0] + 0x26) * x.R(2));
        x.wr(x.p(0x27), cpu.r[0]);
    }
    // 0570C
    x.setR(2, k(0xF19A));
    x.call(0x57AE);
    x.call(0x5719);
}

/// 05633  panorâmica de um nó fixo (arg: índice do nó); segue o 4º carro.
void panStart(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    x.wr(x.p(0xD), 1);
    x.wf(x.p(0x12), 0.0);
    cpu.r[2] = x.rd(cpu.ar[6]++);
    x.call(0x585A);
    for (;;) {
        cpu.r[2] = x.rd(cpu.ar[4] + 3);
        x.call(0x5847);
        if (cpu.ar[2] != 0) break;
        cpu.ar[4] -= 4;
    }
    const uint32_t node = cpu.ar[4];
    cpu.r[2] = 4;
    x.call(0x57F0);
    x.wr(x.p(0x28), cpu.ar[4]);
    x.wr(0xE876, cpu.ar[4]);
    cpu.ar[0] = x.rd(cpu.ar[4] + 0x1B);
    x.wr(0xE878, cpu.ar[0]);
    cpu.ar[4] = node;
    // 05647  posição: nó, 300 acima
    cpu.r[0] = static_cast<uint32_t>(-300);
    x.setR(1, -300.0);
    x.wr(x.p(0x39), cpu.r[1]);
    cpu.r[0] += x.rd(cpu.ar[4] + 1);
    x.wf(x.p(0x18), static_cast<double>(static_cast<int32_t>(cpu.r[0])));
    x.wf(x.p(0x17), x.fl(cpu.ar[4]));
    x.wf(x.p(0x19), x.fl(cpu.ar[4] + 2));
    // 05651  inclinação da rampa até o nó anterior
    cpu.ar[2] = cpu.ar[4] - 4;
    const double dx = static_cast<double>(x.ri(cpu.ar[2]) - x.ri(cpu.ar[4]));
    const double dz = static_cast<double>(x.ri(cpu.ar[2] + 2) - x.ri(cpu.ar[4] + 2));
    x.setR(0, dx * dx);
    x.setR(1, dz * dz);
    x.setR(2, x.R(0) + x.R(1));
    x.call(0xA5F1);
    cpu.r[2] = cpu.r[0];
    x.setR(3, static_cast<double>(x.ri(cpu.ar[2] + 1) - x.ri(cpu.ar[4] + 1)) * k(0x0666));
    x.call(0x931A);
    x.call(0x58E8);
    x.setR(0, -x.R(0) + k(0xC385));
    x.wr(x.p(0x1A), cpu.r[0]);
    x.setR(3, -2500.0);
    x.call(0x57C2);
    x.setR(6, -100.0);
    x.call(0x56C5);
    cpu.r[6] = x.rd(x.p(0x1B));
    x.call(0x5943);
    const uint32_t r6 = cpu.r[6];
    x.call(0x7298);
    cpu.r[6] = r6;
}

/// 056C5  panorâmica: gira Y para o carro `[AR7+0x28]`; AR5 −= `[C92F]`; som 0x30 na pista 2.
void panFrame(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    cpu.ar[0] = x.rd(x.p(0x28));
    x.setR(2, x.rf(cpu.ar[0] + 1) - x.rf(x.p(0x17)));
    x.setR(3, x.rf(cpu.ar[0] + 3) - x.rf(x.p(0x19)));
    x.call(0x931A);
    x.call(0x58E8);
    x.setR(0, x.R(0) - k(0x0491));
    x.call(0x58E8);
    x.setR(0, -x.R(0));
    x.wr(x.p(0x1B), cpu.r[0]);
    cpu.r[2] = x.p(0x1A);
    cpu.ar[2] = x.p(0x1D);
    x.call(0x9343);
    cpu.r[0] = x.rd(0xC92F);
    cpu.ar[5] -= cpu.r[0];
    if (x.rd(0xC8CF) != 2) return;
    cpu.ar[2] = 0x1E;
    x.call(0x99E9);
    if (cpu.r[0] == 0) return;
    x.setR(0, x.R(6) - x.rf(x.p(0x1B)));
    if (x.R(0) < k(0xD385)) return;
    x.setR(6, -100.0);
    cpu.ar[2] = 0x30;
    x.call(0x91E0);
}

// ---------------------------------------------------------------------------------------------
// Câmera presa ao carro

/// 0591A  interpola o deslocamento (`+0xF..0x12` → `+0x13..0x16`) e liga/desliga o carro.
void blendOffset(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    if (x.rd(x.p(0xD)) != 0) return;
    double t = x.rf(x.p(0xE)) + x.rf(0x5919);
    if (t > k(0xD4CD)) t = k(0xD4CD);
    x.setR(0, t);
    x.wr(x.p(0xE), cpu.r[0]);
    cpu.ar[0] = x.p(0xF);
    cpu.ar[1] = x.p(0x13);
    for (int i = 0; i < 4; ++i) {
        x.setR(2, (x.rf(cpu.ar[1]++) - x.rf(cpu.ar[0])) * x.R(0) + x.rf(cpu.ar[0]));
        x.wr(cpu.ar[0]++, cpu.r[2]);
    }
    const bool mode = (x.rd(cpu.ar[4] + 0xE) & 0x3000u) != 0;
    const double side = std::fabs(x.rf(x.p(0x11)));
    if (x.rd(x.p(0x26)) == 0) {
        if (side > 100.0 || !mode) return;
        cpu.ar[2] = cpu.ar[4];
        x.call(0x715F);
    } else {
        if (side < 100.0 || mode) return;
        cpu.ar[2] = cpu.ar[4];
        x.call(0x7093);
        x.wr(x.p(0xD), 1);
    }
}

/// 05883  orientação a partir do carro AR4 (dados AR5 = `[AR4+0x1B]`). Preserva AR5.
void carOrientation(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    const uint32_t ar5 = cpu.ar[5];
    cpu.ar[5] = x.rd(cpu.ar[4] + 0x1B);
    if ((x.rd(cpu.ar[4] + 0xE) & 0x3000u) == 0) {
        x.wr(0xE871, 0);
        cpu.ar[2] = x.p(0x1D);
        cpu.r[2] = cpu.ar[4] + 4;
        x.call(0x93FD);
        x.wf(cpu.ar[5] + 0x2F, 0.0);
        x.wf(x.p(0x1B), 0.0);
        x.setR(0, x.rf(cpu.ar[5] + 0x42) * k(0xFCCD) - x.rf(x.p(0x1A)));
        x.setR(2, k(0xD4CD));
        x.call(0x58D0);
        x.setR(0, x.R(0) + x.rf(x.p(0x1A)));
        x.wr(x.p(0x1A), cpu.r[0]);
        x.setR(0, x.rf(cpu.ar[5] + 0x43) * k(0xE800));
        x.wr(x.p(0x1C), cpu.r[0]);
        cpu.ar[2] = x.rd(kMatB);
        cpu.r[2] = x.p(0x1A);
        x.call(0x9343);
        cpu.ar[2] = x.p(0x1D);
        cpu.r[3] = cpu.ar[2];
        cpu.r[2] = x.rd(kMatB);
        x.call(0x9494);
        x.setR(0, -x.rf(cpu.ar[5] + 0x2C));
        x.wr(x.p(0x1B), cpu.r[0]);
        if (x.rd(cpu.ar[5] + 0x31) != 0) x.wr(x.p(0xD), 1);
    } else {
        // 058AD
        x.wr(0xE871, 1);
        if (x.rd(cpu.ar[5] + 0x31) == 0) {
            cpu.r[3] = x.rd(cpu.ar[4] + 0x11);
            cpu.r[2] = x.rd(cpu.ar[4] + 0x13);
            x.setR(1, x.R(3) + x.R(2));
            if (x.R(1) == 0.0) {
                x.setR(0, -x.rf(cpu.ar[5] + 0x2C));
            } else {
                x.call(0x931A);
            }
            x.call(0x58E8);
            x.setR(0, x.R(0) + x.rf(x.p(0x12)));
            if (x.rd(x.p(0xD)) == 0) {
                x.setR(0, x.R(0) - x.rf(x.p(0x1B)));
                x.setR(2, k(0xC4CD));
                x.call(0x58D0);
                x.setR(0, x.R(0) + x.rf(x.p(0x1B)));
            }
            x.wr(x.p(0x1B), cpu.r[0]);
            x.wf(x.p(0x1A), 0.0);
            x.wf(x.p(0x1C), 0.0);
            cpu.r[2] = x.p(0x1A);
            cpu.ar[2] = x.p(0x1D);
            x.call(0x9343);
            cpu.ar[0] = x.p(0x17);
            x.call(0x2BE5);
        }
    }
    cpu.ar[5] = ar5;
}

/// 058FC  por quadro: posição = carro + matriz × deslocamento; acaba no nó AR5.
void attachedFrame(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    x.call(0x591A);
    x.call(0x5883);
    cpu.ar[2] = x.p(0xF);
    cpu.r[2] = x.p(0x1D);
    cpu.r[3] = x.p(0x17);
    x.call(0x941E);
    x.setR(0, x.rf(cpu.ar[4] + 1) - x.rf(x.p(0x17)));
    x.wr(x.p(0x17), cpu.r[0]);
    x.setR(0, x.rf(cpu.ar[4] + 2) + x.rf(x.p(0x18)));
    x.wr(x.p(0x18), cpu.r[0]);
    x.setR(0, x.rf(cpu.ar[4] + 3) + x.rf(x.p(0x19)));
    x.wr(x.p(0x19), cpu.r[0]);
    cpu.ar[0] = x.rd(cpu.ar[4] + 0x1B);
    cpu.ar[0] = x.rd(cpu.ar[0] + 0x37);
    cpu.r[1] = x.rd(cpu.ar[0] + 0x1E);
    if (static_cast<int32_t>(cpu.r[1]) < static_cast<int32_t>(cpu.ar[5])) return;
    cpu.ar[5] = 0;
    if ((x.rd(cpu.ar[4] + 0xE) & 0x3000u) == 0) x.wr(x.p(0x38), cpu.ar[4]);
}

/// 058EF  câmera presa ao carro AR4: deslocamento-alvo R0..R3 (`+0x13..0x16`).
void attachStart(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    X x{cpu, bus, subs};
    cpu.ar[5] = 0;
    for (uint32_t i = 0; i < 4; ++i) x.wr(x.p(0x13 + i), cpu.r[i]);
    x.setR(4, 0.0);
    x.wr(x.p(0xE), cpu.r[4]);
    cpu.r[4] = x.rd(x.p(0xD));
    if (cpu.r[4] != 0)
        for (uint32_t i = 0; i < 4; ++i) x.wr(x.p(0xF + i), cpu.r[i]);
    attachedFrame(cpu, bus, subs);
}

/// 05861/05874/0587C  deslocamentos fixos (0, Y, Z, 0) e `[AR7+0x26]`.
void attachPreset(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t mode26, double y,
                  double z, bool focusCar) {
    X x{cpu, bus, subs};
    x.wr(x.p(0x26), mode26);
    if (focusCar) cpu.ar[4] = x.rd(0xE876);
    x.setR(0, 0.0);
    x.setR(1, y);
    x.setR(2, z);
    x.setR(3, 0.0);
    attachStart(cpu, bus, subs);
}

} // namespace

void registerDemoCamera(SubroutineDispatcher& d) {
    d.add(0x5624, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        chaseStart(c, b, s, -350, 4, 8, 0x8000, 0x56EF);
    });
    d.add(0x5671, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        chaseStart(c, b, s, -350, 4, 5, 0x1491, 0x56E6);
    });
    d.add(0x5680, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        chaseStart(c, b, s, -350, 4, 5, 0x8000, 0x56EF);
    });
    d.add(0x568F, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        chaseStart(c, b, s, -300, -4, 0, 0x1491, 0x5710);
    });
    d.add(0x5633, panStart);
    d.add(0x569E, chaseSetup);
    d.add(0x56C5, panFrame);
    d.add(0x56E6, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { matchSpeed(c, b, s, 0x0666); });
    d.add(0x56EF, followNearest);
    d.add(0x5710, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { matchSpeed(c, b, s, 0x0414); });
    d.add(0x5719, chaseFrame);
    d.add(0x576E, advanceNode);
    d.add(0x5784, aimAtCar);
    d.add(0x57AE, lateralOffset);
    // 057BF  anda o recuo lateral [AR7+0x37]
    d.add(0x57BF, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.r[3] = b.readWord(c.ar[7] + 0x37);
        s.call(0x57C2, c, b);
    });
    d.add(0x57C2, moveAlongTrack);
    d.add(0x57DB, trackHeading);
    d.add(0x57F0, nthLeader);
    d.add(0x581E, nearestInView);
    d.add(0x5847, findSegment);
    d.add(0x584F, carNode);
    d.add(0x585A, nodeByIndex);
    d.add(0x5861, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.r[1] = 0;
        attachPreset(c, b, s, c.r[0], -150.0, 0.0, true);
    });
    // 05869  presa ao 4º carro, atrás e acima
    d.add(0x5869, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        b.writeWord(c.ar[7] + 0xD, 1);
        c.r[2] = 4;
        s.call(0x57F0, c, b);
        b.writeWord(0xE876, c.ar[4]);
        c.ar[0] = b.readWord(c.ar[4] + 0x1B);
        b.writeWord(0xE878, c.ar[0]);
        s.call(0x5874, c, b);
        s.call(0x5943, c, b);
        s.call(0x7298, c, b);
    });
    d.add(0x5874, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.r[0] = 1;
        attachPreset(c, b, s, 1, -400.0, -2200.0, true);
    });
    d.add(0x587C, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.r[1] = 8;
        attachPreset(c, b, s, c.r[0], -900.0, -3840.0, false);
    });
    d.add(0x5883, carOrientation);
    d.add(0x58D0, dampTurn);
    d.add(0x58EF, attachStart);
    d.add(0x58FC, attachedFrame);
    d.add(0x591A, blendOffset);
}

} // namespace rea::core::game
