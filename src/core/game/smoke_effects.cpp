/**
 * @file smoke_effects.cpp
 * @brief Tradução de `0x87D7`–`0x88EA`, `0x88EB`–`0x8973` (fumaça dos carros).
 */
#include "core/game/smoke_effects.hpp"

#include <array>
#include <cmath>
#include <cstdint>

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

constexpr uint32_t kFocusCar   = 0xE876;  ///< objeto do carro focado (jogador/câmera)
constexpr uint32_t kFocusProc  = 0xE877; ///< AR7 usado ao criar o processo do efeito.
constexpr uint32_t kGameMode   = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.
constexpr uint32_t kFrameList  = 0xC6C0; ///< `[C6C0]`: lista de quadros (modelos) da nuvem.
constexpr uint32_t kRotTemp    = 0x005A;  ///< `[0x5A]`: matriz de rascunho.
constexpr uint32_t kVecTemp    = 0x005D;  ///< `[0x5D]`: vetor de rascunho.
constexpr uint32_t kTicks      = 0xC92F;  ///< Quadros (VBLANKs) gastos na última iteração do loop principal.
constexpr uint32_t kEffectId   = 0x540;  ///< ID do processo do efeito.
constexpr uint32_t kSmokeType  = 0x766;  ///< Tipo dos objetos de fumaça.
constexpr uint32_t kSlots      = 0x13;  ///< `[AR7+0x13+i]`: objetos das nuvens.

/// Contexto dos efeitos de fumaça: CPU, barramento, sub-rotinas e atalhos.
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
    /// Aleatório float pela rotina @p pc com o limite @p scale.
    double random(uint32_t pc, double scale) const {
        setR(0, scale);
        call(pc);
        return R(0);
    }
};

/// `0x9042` / `0x905C`: empilha e desempilha R0–R7 e AR0–AR7.
struct SavedRegs {
    std::array<uint32_t, 8> r{};   ///< R0–R7.
    std::array<uint32_t, 8> ar{};  ///< AR0–AR7.
    /// Guarda R0–R7 e AR0–AR7 de @p c.
    explicit SavedRegs(const Tms32031State& c) {
        for (int i = 0; i < 8; ++i) { r[i] = c.r[i]; ar[i] = c.ar[i]; }
    }
    /// Restaura os registradores em @p c.
    void restore(Tms32031State& c) const {
        for (int i = 0; i < 8; ++i) { c.r[i] = r[i]; c.ar[i] = ar[i]; }
    }
};

/// 08960  objeto AR2 += R1 · (matriz `+4`) · `[AR2+0x11..0x13]` (via `[0x5D]`).
void pushAlong(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    c.ar[1] = c.ar[2];
    c.ar[2] += 0x11;
    c.r[2] = c.ar[1] + 4;
    c.r[3] = x.rd(kVecTemp);
    x.call(0x941E);
    c.ar[0] = x.rd(kVecTemp);
    for (uint32_t i = 0; i < 3; ++i) x.wf(c.ar[1] + 1 + i, x.f(c.ar[0] + i) * x.R(1) + x.f(c.ar[1] + 1 + i));
}

/// Liga um objeto de fumaça novo (`0x700A` com o 1º modelo): carry = falhou; AR0 = objeto.
bool newPuff(const Ctx& x) {
    x.c.ar[2] = x.rd(x.rd(kFrameList));
    x.call(0x700A);
    if (x.c.carry()) return false;
    x.wr(x.c.ar[0] + 0xE, x.rd(x.c.ar[0] + 0xE) | 0x18u);
    return true;
}

/// AR4 = objeto: `0xAF58`, tipo 0x766.
void tagPuff(const Ctx& x) {
    x.call(0xAF58);
    x.wr(x.c.ar[4] + 0xF, kSmokeType);
}

/// 088E7  nenhuma nuvem: mata o processo AR7 recém-criado.
void killEffect(const Ctx& x) {
    x.c.ar[2] = x.c.ar[7];
    x.c.ar[7] = 0;
    x.call(0xA8AC);
}

/// 088B2  2 nuvens em volta do ponto `[AR7+0xC..0xE]` relativo ao carro `[AR7+0x10]`.
void twoPuffs(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    uint32_t n = 0;
    for (; n < 2; ++n) {
        if (!newPuff(x)) break;
        c.ar[4] = c.ar[0];
        tagPuff(x);
        x.wr(c.ar[7] + kSlots + n, c.ar[4]);
        const double r1 = x.random(0x8ED1, 60.0);
        const double r2 = x.random(0x8EC2, -60.0) + x.K(0x88C6);
        const double r3 = x.random(0x8ED1, 60.0);
        const uint32_t car = x.rd(c.ar[7] + 0x10);
        c.ar[0] = car;
        x.wf(c.ar[4] + 1, x.f(c.ar[7] + 0xC) + r1 + x.f(car + 1));
        const double y = x.f(c.ar[7] + 0xD) + r2 + x.K(0x88D2);
        x.wf(c.ar[4] + 0x12, -y + x.K(0x88D4));
        x.wf(c.ar[4] + 2, y + x.f(car + 2));
        x.wf(c.ar[4] + 3, x.f(c.ar[7] + 0xE) + r3 + x.f(car + 3));
        x.wr(c.ar[4] + 0x13, x.rd(kFrameList));
        c.ar[2] = c.ar[4];
        x.call(0x7093);
    }
    c.r[5] = n;
    if (n != 0) x.wr(c.ar[7] + 0x12, n);
    else killEffect(x);
}

/// Cria o processo do efeito (`0xA868`, ID 0x540, PC `[ptr]`) com AR7 = `[E877]`; AR7 = novo PCB.
bool startEffect(const Ctx& x, uint32_t ptr) {
    x.c.ar[7] = x.rd(kFocusProc);
    x.c.ar[2] = x.rd(ptr);
    x.c.r[2] = kEffectId;
    x.call(0xA868);
    if (x.c.carry()) return false;
    x.c.ar[7] = x.c.ar[0];
    return true;
}

/// true no modo 2 (atração).
bool attractMode(const Ctx& x) { return (x.rd(kGameMode) & 0xFu) == 2; }

/// `0x894D`: true se já existem 2 efeitos ou mais.
bool tooMany(const Ctx& x) {
    x.call(0x894D);
    return x.c.carry();
}

/// 087D7  arrancada do carro AR4: 6 nuvens na largura do carro, na traseira.
void launchSmoke(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const SavedRegs saved(c);
    if (attractMode(x) || tooMany(x) || !startEffect(x, 0xC6C1)) return saved.restore(c);
    x.wr(c.ar[7] + 0x10, c.ar[4]);
    c.ar[5] = x.rd(c.ar[4] + 0x1B);
    x.wr(c.ar[7] + 0x11, c.ar[5]);
    uint32_t n = 0;
    for (; n < 6; ++n) {
        if (!newPuff(x)) break;
        c.ar[4] = c.ar[0];
        tagPuff(x);
        x.wr(c.ar[7] + kSlots + n, c.ar[4]);
        // 087F6  orientação = carroceria × matriz do carro
        const uint32_t car = x.rd(c.ar[7] + 0x10);
        c.ar[2] = car;
        x.call(0xAE1F);
        c.ar[2] = c.ar[0];
        c.r[2] = car + 4;
        c.r[3] = c.ar[4] + 4;
        x.call(0x947F);
        const uint32_t d = c.ar[5];
        const double r1 = x.random(0x8EC2, x.f(d + 0x3A) - x.f(d + 0x3B)) + x.f(d + 0x3B);
        c.ar[0] = car;
        x.wf(c.ar[4] + 1, x.f(car + 1) + r1);
        x.wr(c.ar[4] + 2, x.rd(car + 2));
        x.wr(c.ar[4] + 3, x.rd(car + 3));
        x.wf(c.ar[4] + 0x12, 180.0);
        x.wr(c.ar[4] + 0x13, x.rd(d + 0x3F));
        c.ar[2] = c.ar[4];
        x.setR(1, x.K(0x8810));
        x.call(0x8960);
        x.wr(c.ar[4] + 0x13, x.rd(kFrameList));
        x.wf(c.ar[4] + 0x12, 0.0);
        c.ar[2] = c.ar[4];
        x.call(0x7093);
    }
    if (n == 0) {
        // 0881C → 088E7: o original sai sem desempilhar o 0x9042 (não ocorre: 0x700A falharia na 1ª)
        killEffect(x);
        return;
    }
    x.wr(c.ar[7] + 0x12, n);
    saved.restore(c);
}

/// 088EB  raspão do carro focado AR4 (dados AR5): nuvens no canto do lado do muro.
void scrapeSmoke(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const SavedRegs saved(c);
    if (attractMode(x) || c.ar[4] != x.rd(kFocusCar)) return saved.restore(c);
    const uint32_t car = c.ar[4], d = c.ar[5];
    if (x.rd(d + 0x37) == 0 || tooMany(x) || !startEffect(x, 0xC6C2)) return saved.restore(c);
    x.wr(c.ar[7] + 0x10, car);
    x.wr(c.ar[7] + 0x11, d);
    c.ar[2] = x.rd(kRotTemp);
    c.r[2] = x.rd(d + 0x2C);
    x.call(0x938A);
    // 08901  ângulo entre o rumo do carro e o rumo para o nó [D+0x37]
    c.ar[2] = x.rd(d + 0x37);
    x.call(0x303A);
    c.r[2] = c.r[0];
    x.call(0x9308);
    const uint32_t toNode = c.r[2];
    c.r[2] = x.rd(d + 0x2C);
    x.call(0x9308);
    x.setR(2, x.R(2) - tmsToDouble(toNode));
    x.call(0x9308);
    double side = x.f(d + 0x46);
    double ang = x.R(2);
    if (!(std::fabs(ang) < x.K(0x890D))) {
        side = -side;
        ang = -ang;
    }
    uint32_t r1, r0;
    if (side > 0.0) {
        r1 = x.rd(d + 0x3B);
        r0 = ang > 0.0 ? x.rd(d + 0x3E) : x.rd(d + 0x3F);
    } else {
        r1 = x.rd(d + 0x3A);
        r0 = ang > 0.0 ? x.rd(d + 0x3F) : x.rd(d + 0x3E);
    }
    x.wr(c.ar[7] + 0xC, r1);
    x.wr(c.ar[7] + 0xE, r0);
    x.wf(c.ar[7] + 0xD, x.K(0x891E));
    // 08920  deslocamento no rumo do carro
    c.ar[2] = c.ar[7] + 0xC;
    c.r[3] = c.ar[2];
    c.r[2] = x.rd(kRotTemp);
    x.call(0x941E);
    x.call(0x88B2);
    saved.restore(c);
}

/// 08928  batida: se AR0 ou AR1 é o focado, nuvens no canto em contato (AR3 = y do canto).
void crashSmoke(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const SavedRegs saved(c);
    if (attractMode(x)) return saved.restore(c);
    const uint32_t focus = x.rd(kFocusCar);
    if (c.ar[0] == focus) c.ar[4] = c.ar[0];
    else if (c.ar[1] == focus) c.ar[4] = c.ar[1];
    else return saved.restore(c);
    const uint32_t car = c.ar[4];
    c.ar[5] = x.rd(car + 0x1B);
    const uint32_t d = c.ar[5];
    if (x.rd(d + 0x37) == 0 || tooMany(x) || !startEffect(x, 0xC6C3)) return saved.restore(c);
    x.wr(c.ar[7] + 0x10, car);
    x.wr(c.ar[7] + 0x11, d);
    x.wf(c.ar[7] + 0xC, x.f(c.ar[3] - 1) - x.f(car + 1));
    x.wf(c.ar[7] + 0xD, x.K(0x8943));
    x.wf(c.ar[0] + 0xE, x.f(c.ar[3] + 1) - x.f(car + 3));
    x.call(0x88B2);
    saved.restore(c);
}

/// 08820  com < 4 nuvens, mais uma atrás da última (AR4 = última não nula).
void addPuff(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t count = x.rd(c.ar[7] + 0x12);
    if (static_cast<int32_t>(count) >= 4) return;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t o = x.rd(c.ar[7] + kSlots + i);
        if (o != 0) c.ar[4] = o;
    }
    if (!newPuff(x)) return;
    const uint32_t p = c.ar[0];
    c.ar[2] = x.rd(kRotTemp);
    c.r[2] = x.rd(c.ar[5] + 0x2D);
    x.call(0x938A);
    x.wf(p + 1, x.random(0x8ED1, x.K(0x8835)));
    x.wf(p + 2, x.random(0x8ED1, x.K(0x8838)));
    const double ticks = static_cast<double>(static_cast<int32_t>(x.rd(kTicks)));
    x.wf(p + 3, -(ticks * x.K(0x883C) * x.R(7)));
    c.ar[2] = p + 1;
    c.r[3] = c.ar[2];
    c.r[2] = x.rd(kRotTemp);
    x.call(0x941E);
    for (uint32_t i = 1; i <= 3; ++i) x.wf(p + i, x.f(c.ar[4] + i) + x.f(p + i));
    x.wr(c.ar[7] + kSlots + x.rd(c.ar[7] + 0x12), p);
    x.wr(p + 0x12, x.rd(c.ar[4] + 0x12));
    x.wr(c.ar[7] + 0x12, x.rd(c.ar[7] + 0x12) + 1);
    c.ar[4] = p;
    tagPuff(x);
    x.wr(c.ar[4] + 0x13, x.rd(kFrameList));
    c.ar[2] = c.ar[4];
    x.call(0x7093);
}

/// 08862  um quadro do efeito.
void effectStep(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    uint32_t alive = 0;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(x.rd(c.ar[7] + 0x12)); ++i) {
        const uint32_t slot = c.ar[7] + kSlots + i;
        c.ar[4] = x.rd(slot);
        if (c.ar[4] == 0) continue;
        const uint32_t o = c.ar[4];
        bool keep = (x.rd(o + 0xE) & 0x3000u) != 0 && x.rd(o + 0xF) == kSmokeType;
        if (keep) {
            // 0886F  próximo quadro da animação
            c.ar[6] = x.rd(o + 0x13) + 1;
            const uint32_t frame = x.rd(c.ar[6]);
            keep = static_cast<int32_t>(frame) >= 0;
            if (keep) {
                x.wr(o + 0xD, frame);
                x.wr(o + 0x13, c.ar[6]);
                keep = static_cast<int32_t>(x.rd(o + 0x1C)) > 0x3E8;
            }
        }
        if (keep) {
            // 08877  anda (1,5 · v · [C92F] − 100) no rumo do carro
            c.ar[2] = x.rd(kRotTemp);
            c.r[2] = x.rd(c.ar[5] + 0x2D);
            x.call(0x938A);
            x.call(0x9414);
            const double ticks = static_cast<double>(static_cast<int32_t>(x.rd(kTicks)));
            x.wf(c.ar[2] + 2, ticks * x.f(c.ar[5] + 0x26) * x.K(0x887D) - x.K(0x887E));
            c.r[2] = x.rd(kRotTemp);
            c.r[3] = c.ar[2];
            x.call(0x941E);
            c.ar[1] = x.rd(kVecTemp);
            x.wf(o + 1, x.f(c.ar[1]) + x.f(o + 1));
            x.wf(o + 3, x.f(c.ar[1] + 2) + x.f(o + 3));
            // 0888A  cola no chão a +0x12 de altura
            c.ar[4] = o + 1;
            x.call(0x1F8B);
            c.ar[4] = o;
            keep = c.carry();
            if (keep) {
                x.wf(o + 2, x.R(0) - x.f(o + 0x12) + x.f(o + 2));
                ++alive;
            }
        }
        if (!keep) {
            // 088A5  descarta a nuvem
            c.ar[2] = x.rd(slot);
            x.wr(slot, 0);
            if ((x.rd(c.ar[2] + 0xE) & 0x3000u) && x.rd(c.ar[2] + 0xF) == kSmokeType) x.call(0x7100);
        }
    }
    c.r[6] = alive;
    if (alive == 0) {
        x.call(0xA896);
        return;
    }
    c.ar[2] = 1;
    c.ret = 0x889E;
    x.call(0xA874);
}

} // namespace

void registerSmokeEffects(SubroutineDispatcher& d) {
    using S = ISubroutines;
    d.add(0x87D7, launchSmoke);
    d.add(0x8820, addPuff);
    d.add(0x88B2, twoPuffs);
    d.add(0x88EB, scrapeSmoke);
    d.add(0x8928, crashSmoke);
    d.add(0x8960, pushAlong);
    d.add(0x8860, [](Tms32031State& c, MemoryBus& b, S& s) {
        c.r[7] = doubleToTms(0.0);
        c.ar[5] = b.readWord(c.ar[7] + 0x11);
        effectStep(c, b, s);
    });
    d.add(0x889E, [](Tms32031State& c, MemoryBus& b, S& s) {
        // 0889E  a cada 2 quadros, mais uma nuvem
        const double r7 = tmsToDouble(c.r[7]) + 1.0;
        c.r[7] = doubleToTms(r7);
        if (r7 == 2.0) {
            s.call(0x8820, c, b);
            c.r[7] = doubleToTms(0.0);
        }
        effectStep(c, b, s);
    });
}

} // namespace rea::core::game
