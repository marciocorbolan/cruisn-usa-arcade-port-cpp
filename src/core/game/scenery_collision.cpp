/**
 * @file scenery_collision.cpp
 * @brief Tradução de `0x221F`–`0x232C`, `0x2334`–`0x238B`, `0x239C`–`0x23E4` e `0x6959`–`0x69A7`.
 */
#include "core/game/scenery_collision.hpp"

#include <cmath>
#include <cstdint>

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

constexpr uint32_t kFocusCar   = 0xE876;  ///< objeto do carro focado (jogador/câmera)
constexpr uint32_t kBoxBuffer  = 0x004F; ///< `[0x4F]` = 0x809D00: cantos da caixa do carro.
constexpr uint32_t kEdgeTable  = 0x290C; ///< `[290C]` = 0x290D: 5 ponteiros para o y dos cantos 2, 6, 7, 3, 2.
constexpr uint32_t kRotTemp    = 0x005A; ///< `[0x5A]`: matriz de rascunho.
constexpr uint32_t kDirTemp    = 0x005D; ///< `[0x5D]`: direção da separação (`0x2633`).
constexpr uint32_t kObjCellPtr = 0x0058; ///< `[0x58]`: célula de cabeça dos objetos.
constexpr uint32_t kTicks      = 0xC92F; ///< quadros de vídeo por iteração do loop.
constexpr uint32_t kKindTable  = 0xC62B; ///< `[C62B]`: 5 palavras por tipo de objeto 0x750.
constexpr uint32_t kMinutes    = 0xC93A;  ///< Minutos acumulados (float TMS, += 1/3420 por quadro).
constexpr uint32_t kNext       = 0x20;  ///< Campo do próximo objeto na lista de colisão.
constexpr uint32_t kBit23      = 1u << 23;  ///< Bit 23 da palavra de flags do objeto (`+0xE`).
constexpr uint32_t kBit27      = 1u << 27;  ///< Bit 27 das flags (`+0xE`): objeto atingido (fora da lista de colisão).
constexpr uint32_t kBit31      = 1u << 31;  ///< Bit 31 das flags (`+0xE`): objeto na lista `[0x58]`.
constexpr uint32_t kProcId     = 0x290;  ///< ID do processo do objeto atingido.

/// Contexto com R0–R7 em ponto flutuante e acesso à memória.
struct Ctx {
    Tms32031State& c;  ///< Registradores da CPU TMS320C31.
    MemoryBus& b;  ///< Barramento de memória simulado.
    ISubroutines& s;  ///< Despachante das sub-rotinas traduzidas.
    /// Lê o registrador R@p i como float TMS.
    double R(int i) const { return tmsToDouble(c.r[i]); }
    /// Grava @p v, convertido para float TMS, no registrador R@p i.
    void setR(int i, double v) const { c.r[i] = doubleToTms(v); }
    /// Float curto de 16 bits (imediato da instrução) na palavra @p pc da ROM.
    double K(uint32_t pc) const { return shortFloat(static_cast<uint16_t>(b.readWord(pc) & 0xFFFFu)); }
    /// Lê o float TMS em @p a e o converte para double.
    double f(uint32_t a) const { return tmsToDouble(b.readWord(a)); }
    /// Converte @p v para float TMS e grava em @p a.
    void wf(uint32_t a, double v) const { b.writeWord(a, doubleToTms(v)); }
    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) const { return b.readWord(a); }
    /// Grava a palavra @p v no endereço @p a.
    void wr(uint32_t a, uint32_t v) const { b.writeWord(a, v); }
    /// Chama a sub-rotina traduzida no endereço da ROM indicado (o `CALL` original).
    void call(uint32_t a) const { s.call(a, c, b); }
    /// `CALL 0x8EC2` / `0x8ED1` com R0 = @p scale: aleatório 0..scale / −scale..+scale.
    double random(uint32_t pc, double scale) const {
        setR(0, scale);
        call(pc);
        return R(0);
    }
    /// Dorme @p frames quadros (`0xA874`) e retoma em @p pc.
    void sleep(uint32_t frames, uint32_t pc) const {
        c.ar[2] = frames;
        c.ret = pc;
        call(0xA874);
    }
    /// `0xA868`: processo `[ptr]` com ID 0x290; AR0 = PCB, carry = sem bloco livre.
    bool create(uint32_t ptr) const {
        c.ar[2] = rd(ptr);
        c.r[2] = kProcId;
        call(0xA868);
        return !c.carry();
    }
    /// Som AR2: `0x9188` para o carro focado (AR3), senão volume pela distância (`0x316E`, AR4).
    void sound(uint32_t id) const {
        c.ar[2] = id;
        call(c.ar[3] == rd(kFocusCar) ? 0x9188 : 0x316E);
    }
};

/// Matriz do objeto AR4 (`+4`) multiplicada pela rotação em x de R2 (`0x9377` em `[0x5A]`, `0x9494`).
void spinObject(const Ctx& x, double angle) {
    x.setR(2, angle);
    x.c.ar[2] = x.rd(kRotTemp);
    x.call(0x9377);
    x.c.r[2] = x.c.ar[4] + 4;
    x.c.r[3] = x.c.r[2];
    x.call(0x9494);
}

/// 0225F  objeto sólido: empurra o carro, freia e faz girar.
void solidHit(const Ctx& x, uint32_t car, uint32_t obj) {
    Tms32031State& c = x.c;
    const uint32_t d = c.ar[5];
    x.call(0x2633);
    const double push = x.R(0);
    x.wf(car + 1, push * x.f(c.ar[2]) + x.f(car + 1));
    x.wf(car + 3, push * x.f(c.ar[2] + 2) + x.f(car + 3));
    // 02266  velocidade · 0,6, mínimo 37
    double v = x.f(d + 0x26) * x.K(0x2267);
    const bool slow = v < x.K(0x2268);
    if (slow) v = x.K(0x2269);
    x.wf(d + 0x26, v);
    if (!slow) {
        c.ar[2] = 0x1F4;
        x.call(0x8EE9);
    }
    if (!slow && c.carry()) {
        // 0227B  rodada
        x.wr(d + 0x31, 1);
        x.wf(d + 0x32, x.K(0x227D));
        x.setR(0, x.K(0x227F));
    } else {
        // 0226F  giro curto para o lado do rumo da pista
        x.wr(d + 0x31, 0x3C);
        c.ar[4] = c.ar[0];
        x.call(0x3031);
        c.ar[0] = c.ar[4];
        x.setR(2, x.R(0) - x.f(d + 0x2C));
        x.call(0x9308);
        x.setR(0, x.R(2) > 0.0 ? x.K(0x2278) : x.K(0x2279));
    }
    x.wf(d + 0x2E, x.R(0));
    // 02281  batida de frente (|ângulo| ≥ π/2): inverte o rumo desejado
    c.ar[2] = x.rd(kDirTemp);
    x.setR(2, x.f(c.ar[2]));
    x.setR(3, x.f(c.ar[2] + 2));
    x.call(0x931A);
    x.setR(2, x.R(0) - x.K(0x2285) - x.f(d + 0x2D));
    x.call(0x9308);
    if (!(std::fabs(x.R(2)) < x.K(0x228A))) {
        c.r[3] = x.b.readWord(d + 0x2D);
        x.setR(2, x.R(3) + x.K(0x228E));
        x.call(0x9308);
        x.wf(d + 0x2D, x.R(2));
        if ((x.rd(obj + 0xF) & 0xF0u) != 0x40u) {
            // 02295  objeto 0x615: alinha ao rumo antigo e balança (processo 0x23B0)
            c.r[2] = c.r[3];
            c.ar[2] = obj + 4;
            x.call(0x938A);
            c.ar[4] = obj;
            x.wr(obj + 0xE, (x.rd(obj + 0xE) & ~0x8u) | kBit27);
            double r7 = x.f(d + 0x26) * x.K(0x22A1);
            if (r7 < x.K(0x22A2)) r7 = x.K(0x22A3);
            if (r7 > x.K(0x22A4)) r7 = x.K(0x22A5);
            x.setR(7, r7);
            x.create(0x23AF);
        }
    }
    // 022A9
    c.ar[2] = 0x207;
    x.call(0x9188);
}

/// 022B2  objeto arremessado (tipo 0x30).
void knockAway(const Ctx& x, uint32_t car, uint32_t obj) {
    Tms32031State& c = x.c;
    const uint32_t d = c.ar[5];
    if (std::fabs(x.f(car + 2) - x.f(obj + 2)) > 250.0) return;
    const double heading = x.random(0x8ED1, x.K(0x22B8)) + x.f(d + 0x2D);
    double r1 = (x.random(0x8EC2, x.K(0x22BC)) + x.K(0x22BF)) * x.K(0x22C0);
    x.setR(2, heading);
    x.call(0x92E2);
    double r3 = -x.R(0);
    x.call(0x92DF);
    r1 *= x.f(d + 0x26);
    r3 *= r1;
    const double r0 = x.R(0) * r1;
    x.wf(obj + 0x11, r3);
    x.wf(obj + 0x13, r0);
    double vy = (x.random(0x8EC2, x.K(0x22C9)) + x.K(0x22CB)) * x.K(0x22CC) * x.f(d + 0x26);
    if (vy < x.K(0x22CE)) vy = x.K(0x22CF);
    x.wf(obj + 0x12, vy);
    c.ar[4] = obj;
    c.ar[3] = car;
    // 022D3  um voo anterior ainda ativo é encerrado
    if (x.rd(obj + 0xE) & kBit23) {
        c.ar[2] = x.rd(obj + 0x17);
        x.call(0xA8AC);
    }
    if (!x.create(0x2333)) return;
    x.wr(obj + 0x17, c.ar[0]);
    x.wr(obj + 0xE, x.rd(obj + 0xE) | (kBit27 + kBit23));
    x.wr(obj + 0x1F, 0);
    const uint32_t type = x.rd(obj + 0xF);
    if ((type & 0xF0u) == 0x50u) {
        x.call(0x6997);
        return;
    }
    const bool one = (type & 0xFu) == 1;
    if (c.ar[3] == x.rd(kFocusCar)) x.sound(one ? 0x1FB : 0x20A);
    else x.sound(one ? 0x1FE : 0x204);
}

/// 022FA  objeto tombado (tipo 0x20 e os não listados).
void topple(const Ctx& x, uint32_t car, uint32_t obj) {
    Tms32031State& c = x.c;
    const uint32_t d = c.ar[5];
    c.r[2] = x.b.readWord(d + 0x2D);
    c.ar[2] = obj + 4;
    x.call(0x938A);
    c.ar[4] = obj;
    c.ar[3] = car;
    x.wr(obj + 0xE, x.rd(obj + 0xE) & ~0x8u);
    // 02303  o carro perde 20 % da velocidade (mínimo 10, no máximo tudo)
    double r7 = x.f(d + 0x26);
    double r6 = r7 * x.K(0x2305);
    if (r6 < x.K(0x2306)) r6 = x.K(0x2307);
    if (r7 < r6) r6 = r7;
    x.setR(5, r7 - r6);
    x.wf(d + 0x26, x.R(5));
    x.setR(6, r6);
    r7 *= x.K(0x230D);
    if (r7 < x.K(0x230E)) r7 = x.K(0x230F);
    if (r7 > x.K(0x2310)) r7 = x.K(0x2311);
    x.setR(7, r7);
    x.call(0x23D1);
    x.create(0x239B);
    // 02316  som pelo tipo
    const uint32_t kind = x.rd(obj + 0xF) & 0xFu;
    uint32_t id = 0x20A;
    if (kind == 1) {
        c.ar[2] = 5;
        x.call(0x8EDB);
        id = x.rd(c.r[0] + x.rd(0x232D));
    } else if (kind == 2) {
        id = 0x210;
    } else if (kind == 3) {
        id = 0x20D;
    }
    x.sound(id);
}

/// 0221F  carro AR0 × objeto AR1. Preserva AR0 e AR1.
void sceneryHit(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t car = c.ar[0], obj = c.ar[1];
    c.ar[2] = x.rd(kBoxBuffer);
    x.call(0x28A8);
    // 02223  centro do objeto à esquerda das 4 arestas do retângulo de cima
    c.ar[2] = x.rd(kEdgeTable);
    for (int i = 0; i < 4; ++i) {
        c.ar[3] = x.rd(c.ar[2]++);
        c.ar[4] = x.rd(c.ar[2]);
        const uint32_t a = c.ar[3], e = c.ar[4];
        // SUBF3 a,b,d = a − b: 02228 R0 = [AR4+1] − [AR3+1]; 02229 R1 = [AR3−1] − [AR4−1]
        const double r0 = x.f(e + 1) - x.f(a + 1);
        const double r1 = x.f(a - 1) - x.f(e - 1);
        const double r2 = -(x.f(a - 1) * r0 + x.f(a + 1) * r1);
        x.setR(0, r0 * x.f(obj + 1));
        x.setR(1, r1 * x.f(obj + 3) + x.R(0));
        x.setR(2, r2 + x.R(1));
        if (!(x.R(2) > 0.0)) return;  // 02232 BLE 0232A
    }

    c.ar[5] = x.rd(car + 0x1B);
    const uint32_t type = x.rd(obj + 0xF);
    if ((type & 0xF0u) == 0x60u) return;
    if ((type & 0xFF0u) == 0x750u) {
        // 022AC  0x6959 sempre volta com carry = 0: o arremesso de 0x22B2 não ocorre por aqui
        x.wr(obj + 0x1B, 1);
        x.call(0x6971);
        x.call(0x6959);
        if (c.carry()) knockAway(x, car, obj);
    } else if ((type & 0xF0u) == 0x30u) {
        knockAway(x, car, obj);
    } else if ((type & 0xF0u) == 0x20u) {
        topple(x, car, obj);
    } else if ((type & 0xFFFu) == 0x615u || (type & 0xF0u) == 0x40u) {
        if ((type & 0xFFFu) == 0x615u) {
            // 02247  1..3 processos [B124] (ID 0x615) com AR5 = carro
            const uint32_t r0 = c.r[0], r2 = c.r[2], ar2 = c.ar[2], ar3 = c.ar[3], ar5 = c.ar[5];
            c.ar[5] = car;
            c.ar[2] = 3;
            x.call(0x8EDB);
            int32_t n = static_cast<int32_t>(c.r[0]);
            do {
                c.ar[2] = x.rd(0xB124);
                c.r[2] = 0x615;
                x.call(0xA844);
            } while (--n >= 0);
            c.r[0] = r0; c.r[2] = r2; c.ar[0] = car; c.ar[2] = ar2; c.ar[3] = ar3; c.ar[5] = ar5;
        }
        solidHit(x, car, obj);
    } else {
        topple(x, car, obj);
    }
    c.ar[0] = car;
    c.ar[1] = obj;
}

/// 023D1  tira AR4 da lista `[0x58]` (próximo em +0x20); `+0xE`: −bit 31, +bit 27. Preserva AR1.
/// @note Se AR4 não estiver na lista, o original sai com `RETSEQ` sem desempilhar AR1 (não ocorre).
void detachObject(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t ar1 = c.ar[1];
    c.ar[1] = x.rd(kObjCellPtr) - kNext;
    for (;;) {
        c.r[0] = x.rd(c.ar[1] + kNext);
        if (c.r[0] == 0) break;
        if (c.r[0] == c.ar[4]) {
            x.wr(c.ar[1] + kNext, x.rd(c.ar[4] + kNext));
            x.wr(c.ar[4] + 0xE, (x.rd(c.ar[4] + 0xE) & ~kBit31) | kBit27);
            break;
        }
        c.ar[1] = c.r[0];
    }
    c.ar[1] = ar1;
}

/// 06997  `[AR1+0x21]` ≠ −1: `[AR1+0xD]` = modelo do tipo (`[C62B]` + 5·(tipo − 1)); `[AR4+0x21]` = −1.
void kindModel(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    if (x.rd(c.ar[1] + 0x21) == 0xFFFFFFFFu) return;
    const uint32_t entry = x.rd(kKindTable) + ((x.rd(c.ar[1] + 0xF) & 0xFu) - 1u) * 5u;
    x.wr(c.ar[1] + 0xD, x.rd(entry));
    x.wr(c.ar[4] + 0x21, 0xFFFFFFFFu);
}

/// 06959  objeto 0x750 a ≤ 30000: tipo 3 → `0x6AD3`; senão `0x6997`, posição de `[E8ED]`/`[E8EE]`
///        (se não forem 0) e `0x6A66`. Carry = 0. Preserva R0 e AR2.
void kindHit(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t r0 = c.r[0], ar2 = c.ar[2];
    if (static_cast<int32_t>(x.rd(c.ar[1] + 0x1C)) <= 30000) {
        if ((x.rd(c.ar[1] + 0xF) & 0xFu) == 3) {
            x.call(0x6AD3);
        } else {
            x.call(0x6997);
            if (x.f(0xE8ED) + x.f(0xE8EE) != 0.0) {
                x.wr(c.ar[1] + 1, x.rd(0xE8ED));
                x.wr(c.ar[1] + 3, x.rd(0xE8EE));
            }
            x.call(0x6A66);
        }
    }
    c.st &= ~Tms32031State::kStCarry;
    c.r[0] = r0;
    c.ar[2] = ar2;
}

/// 06971  som do objeto 0x750, no máximo a cada `[C62C]` minutos (`[E8EF]` = último).
///        Carro com tipo & 0xF00 = 0x100: `0x9188`, `0x91C1`(0) e `0x9188`(0x1B0); senão `0x316E`.
void kindSound(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t r0 = c.r[0], ar1 = c.ar[1], ar2 = c.ar[2], ar4 = c.ar[4];
    if (!(x.f(kMinutes) - x.f(0xE8EF) < x.f(0xC62C))) {
        x.wr(0xE8EF, x.rd(kMinutes));
        const uint32_t kind = x.rd(c.ar[1] + 0xF) & 0xFu;
        if (kind != 0 && kind != 3) {
            c.ar[2] = x.rd(x.rd(kKindTable) + (kind - 1) * 5 + 1);
            if ((x.rd(c.ar[0] + 0xF) & 0xF00u) == 0x100u) {
                x.call(0x9188);
                c.ar[2] = 0;
                x.call(0x91C1);
                c.ar[2] = 0x1B0;
                x.call(0x9188);
            } else {
                c.ar[4] = c.ar[0];
                x.call(0x316E);
            }
        }
    }
    c.r[0] = r0; c.ar[1] = ar1; c.ar[2] = ar2; c.ar[4] = ar4;
}

// --- Processos (AR4 = objeto, AR7 = PCB) ---------------------------------------------------

/// 02381  fim do voo.
void flightEnd(const Ctx& x) {
    x.wr(x.c.ar[4] + 0xE, x.rd(x.c.ar[4] + 0xE) & ~kBit23);
    if (x.rd(x.c.ar[4] + 0xF) == 0x750) {
        x.c.ar[2] = x.c.ar[4];
        x.call(0x7100);
    }
    x.call(0xA896);
}

/// 02343  um passo do voo.
void flightStep(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t o = c.ar[4];
    // gira o objeto [C92F] vezes pela matriz do processo (+0xE)
    c.ar[6] = x.rd(kTicks) - 1;
    do {
        c.r[2] = c.ar[7] + 0xE;
        c.ar[2] = o + 4;
        c.r[3] = c.ar[2];
        x.call(0x9494);
    } while (static_cast<int32_t>(--c.ar[6]) >= 0);
    x.call(0x908B);
    x.wr(o + 0x1E, 0);
    // 0234F  gravidade
    const double ticks = static_cast<double>(static_cast<int32_t>(x.rd(kTicks)));
    x.wf(o + 0x12, ticks * x.K(0x2350) + x.f(o + 0x12));
    double vy = x.f(o + 0x12);
    if (vy > x.K(0x2353)) return flightEnd(x);
    x.call(0x1FB0);
    if (c.carry()) {
        double floor = static_cast<double>(0x9B);
        if (x.rd(o + 0xF) == 0x750) floor = x.K(0x235A);
        if (!(x.R(0) > floor) && !(vy < 0.0)) {
            x.setR(0, floor - x.R(0) + x.f(o + 2));  // 0235F (não usado)
            if (vy < x.K(0x2361)) return flightEnd(x);
            // 02363  quica
            vy *= x.K(0x2363);
            x.wf(o + 0x12, vy);
            x.wf(o + 0x11, x.f(o + 0x11) * x.K(0x2366));
            x.wf(o + 0x13, x.f(o + 0x13) * x.K(0x2369));
            const double dist = static_cast<double>(static_cast<int32_t>(x.rd(o + 0x1C)));
            if (!(dist < 0.0)) {
                x.setR(0, dist);
                x.call(0xA585);
                double r2 = std::fabs(x.f(o + 0x12));
                if (r2 > x.K(0x2371)) r2 = x.K(0x2372);
                x.setR(0, x.R(0) * static_cast<double>(0x1388) * (r2 * x.K(0x2373)));
                int32_t vol = static_cast<int32_t>(tmsFix(x.R(0)));
                if (vol > 0x8C) vol = 0x8C;
                c.r[0] = static_cast<uint32_t>(vol);
                c.ar[2] = (x.rd(o + 0xF) & 0xFu) == 1 ? 0x1FE : 0x201;
                x.call(0x9186);
            }
        }
    }
    x.sleep(1, 0x2380);
}

/// 02334  voo do objeto arremessado: matriz de giro aleatório em +0xE.
void flightStart(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    x.setR(2, x.random(0x8ED1, x.K(0x2334)));
    c.ar[2] = c.ar[7] + 0xE;
    x.call(0x9377);
    c.ar[2] = x.rd(kRotTemp);
    x.setR(2, x.random(0x8ED1, x.K(0x233B)));
    x.call(0x938A);
    c.r[2] = c.ar[7] + 0xE;
    c.r[3] = c.r[2];
    x.call(0x9494);
    flightStep(c, b, s);
}

/// 0239D  tomba R7 por quadro; ao completar 1,5 rad acerta o último passo.
void toppleStep(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    double r6 = x.R(6) + x.R(7);
    if (!(r6 < x.K(0x239E))) {
        r6 -= x.K(0x23A0);
        x.setR(7, x.R(7) - r6);
        r6 = x.K(0x23A2);
    }
    x.setR(6, r6);
    spinObject(x, x.R(7));
    x.sleep(1, 0x23AC);
}

/// 023B0 / 023BB  balanço: um passo de R7 e um quadro de sono.
void wobble(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t resume) {
    Ctx x{c, b, s};
    spinObject(x, x.R(7));
    x.sleep(1, resume);
}

/// 023BA  R7 · −0,4 e 4 quadros com AR6 = 3.
void wobbleSeries(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t factorPc) {
    Ctx x{c, b, s};
    c.ar[6] = 3;
    x.setR(7, x.R(7) * x.K(factorPc));
    if (factorPc == 0x23C6 && !(std::fabs(x.R(7)) > x.K(0x23C8))) {
        // 023CA  parado: volta a colidir (+8) e limpa o bit 27
        x.wr(c.ar[4] + 0xE, (x.rd(c.ar[4] + 0xE) | 0x8u) & ~kBit27);
        x.call(0xA896);
        return;
    }
    wobble(c, b, s, 0x23C4);
}

} // namespace

void registerSceneryCollision(SubroutineDispatcher& d) {
    using S = ISubroutines;
    d.add(0x221F, sceneryHit);
    d.add(0x23D1, detachObject);
    d.add(0x6959, kindHit);
    d.add(0x6971, kindSound);
    d.add(0x6997, kindModel);
    d.add(0x2334, flightStart);
    d.add(0x2380, flightStep);
    d.add(0x239C, [](Tms32031State& c, MemoryBus& b, S& s) {
        c.r[6] = doubleToTms(0.0);
        toppleStep(c, b, s);
    });
    d.add(0x23AC, [](Tms32031State& c, MemoryBus& b, S& s) {
        if (tmsToDouble(c.r[6]) < shortFloat(static_cast<uint16_t>(b.readWord(0x23AC) & 0xFFFFu)))
            toppleStep(c, b, s);
        else
            s.call(0xA896, c, b);
    });
    d.add(0x23B0, [](Tms32031State& c, MemoryBus& b, S& s) { wobble(c, b, s, 0x23B9); });
    d.add(0x23B9, [](Tms32031State& c, MemoryBus& b, S& s) { wobbleSeries(c, b, s, 0x23BA); });
    d.add(0x23C4, [](Tms32031State& c, MemoryBus& b, S& s) {
        // DBU AR6: mais um quadro com o mesmo R7; esgotado → R7 · −0,6
        if (static_cast<int32_t>(--c.ar[6]) >= 0) wobble(c, b, s, 0x23C4);
        else wobbleSeries(c, b, s, 0x23C6);
    });
}

} // namespace rea::core::game
