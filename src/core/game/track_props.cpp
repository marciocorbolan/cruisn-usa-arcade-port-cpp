/**
 * @file track_props.cpp
 * @brief Tradução de `0x4225`–`0x4340` e `0x4366`–`0x440B` (objetos animados do cenário).
 */
#include "core/game/track_props.hpp"

#include <cmath>
#include <cstdint>

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

constexpr uint32_t kAnimId   = 0x850;  ///< ID dos processos de animação dos objetos da pista.
constexpr uint32_t kBit23    = 1u << 23;  ///< Bit 23 da palavra de flags do objeto (`+0xE`).
[[maybe_unused]] constexpr uint32_t kVecTemp  = 0x005D;  ///< `[0x5D]`: vetor de rascunho.
constexpr uint32_t kTicks    = 0xC92F;  ///< Quadros (VBLANKs) gastos na última iteração do loop principal.
constexpr uint32_t kHumSound = 0xB7;  ///< Som do zumbido dos objetos animados.

/// Contexto das animações dos objetos da pista: CPU, barramento, sub-rotinas e atalhos.
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
    /// Aleatório em [0, @p n) (`0x8EDB`).
    uint32_t randomBelow(uint32_t n) const {
        c.ar[2] = n;
        call(0x8EDB);
        return c.r[0];
    }
    /// Dorme @p frames quadros (`0xA874`) e retoma em @p pc.
    void sleep(uint32_t frames, uint32_t pc) const {
        c.ar[2] = frames;
        c.ret = pc;
        call(0xA874);
    }
    /// `0xA844` com PC `[ptr]` e ID 0x850: AR0 = PCB, carry = falhou.
    bool create(uint32_t ptr, uint32_t id = kAnimId) const {
        c.ar[2] = rd(ptr);
        c.r[2] = id;
        call(0xA844);
        return !c.carry();
    }
    /// 042FC / 043F5  `[AR4+0x17]` = AR0; `[AR4+0xE]` |= bit 23.
    void markAnimated() const {
        wr(c.ar[4] + 0x17, c.ar[0]);
        wr(c.ar[4] + 0xE, rd(c.ar[4] + 0xE) | kBit23);
    }
    /// Desliga o bit 0x400 das flags do objeto AR4 (volta à paleta da tabela).
    void clearBit400() const { wr(c.ar[4] + 0xE, rd(c.ar[4] + 0xE) & ~0x400u); }
};

/// PUSH R0, AR0, AR2, AR5, AR6, R2 … POP (prólogo/epílogo dos tratadores de animação).
struct HandlerRegs {
    uint32_t r0;   ///< R0.
    uint32_t r2;   ///< R2.
    uint32_t ar0;  ///< AR0.
    uint32_t ar2;  ///< AR2.
    uint32_t ar5;  ///< AR5.
    uint32_t ar6;  ///< AR6.
    /// Guarda os registradores do tratador.
    explicit HandlerRegs(const Tms32031State& c)
        : r0(c.r[0]), r2(c.r[2]), ar0(c.ar[0]), ar2(c.ar[2]), ar5(c.ar[5]), ar6(c.ar[6]) {}
    /// Restaura os registradores em @p c.
    void restore(Tms32031State& c) const {
        c.r[0] = r0; c.r[2] = r2; c.ar[0] = ar0; c.ar[2] = ar2; c.ar[5] = ar5; c.ar[6] = ar6;
    }
};

/// Tratador de animação: AR6 = `[listPtr]`; com @p phase, AR5 = AR6 + aleatório(0..6) e limpa o bit
/// 0x400; cria o processo `[procPtr]`.
void animHandler(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t listPtr, uint32_t procPtr,
                 bool phase) {
    Ctx x{c, b, s};
    const HandlerRegs saved(c);
    c.ar[6] = x.rd(listPtr);
    if (phase) {
        x.clearBit400();
        c.ar[5] = c.ar[6] + x.randomBelow(7);
    }
    if (x.create(procPtr)) x.markAnimated();
    saved.restore(c);
}

/// 0439E  `0xAF58` (lista `[E8EC]`, tipo 0x730) e assenta no chão: se a altura relativa (`0x1F8B`
/// em AR4+1) for ≤ 1000, y += altura − 45.
void settle(const Ctx& x) {
    Tms32031State& c = x.c;
    x.call(0xAF58);
    c.ar[4] += 1;
    x.call(0x1F8B);
    c.ar[4] -= 1;
    c.r[1] = tmsFix(x.R(0));
    if (static_cast<int32_t>(c.r[1]) <= 0x3E8) {
        c.r[1] = x.rd(c.ar[4] + 2);
        x.setR(0, x.R(0) + x.R(1) - 45.0);
        x.wr(c.ar[4] + 2, c.r[0]);
    }
}

/// 043B1  objeto 0x767: animação `[4357]`, bit 0x400 limpo, flag 8, assentado; processo `[B0CD]`.
void prop767(const Ctx& x) {
    Tms32031State& c = x.c;
    c.ar[6] = x.rd(0x4357);
    x.wr(c.ar[4] + 0xE, (x.rd(c.ar[4] + 0xE) & ~0x400u) | 8u);
    settle(x);
    x.wr(c.ar[4] + 0xF, 0x767);
    c.ar[5] = c.ar[6];
    if (x.create(0xB0CD, 0x857)) x.markAnimated();
}

/// 04366  objeto 0x768 (com o item 0x25): animação `[435C]`, paleta sorteada (4 em 5) da tabela
/// `[4365]` (pares paleta/slot), assentado; processo `[B0CB]`. Sem o item, vira 0x767.
void prop768(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const HandlerRegs saved(c);
    const uint32_t ar2 = c.ar[2];
    c.ar[2] = 0x25;
    x.call(0x99E9);
    c.ar[2] = ar2;
    if (c.r[0] == 0) {
        prop767(x);
        return saved.restore(c);
    }
    c.ar[6] = x.rd(0x435C);
    x.wr(c.ar[4] + 0xE, x.rd(c.ar[4] + 0xE) | 8u);
    const uint32_t pick = x.randomBelow(5);
    if (pick != 4) {
        c.ar[5] = pick * 2 + x.rd(0x4365);
        c.ar[2] = x.rd(c.ar[5] + 1);
        x.call(0x9ECD);
        if (!c.carry()) {
            c.r[2] = c.r[0];
            c.ar[2] = x.rd(c.ar[5]);
            c.r[3] = x.rd(c.ar[2]++);
            x.call(0x9F5C);
            x.wr(c.ar[4] + 0x10, c.r[0]);
        }
    }
    settle(x);
    x.wr(c.ar[4] + 0xF, 0x768);
    c.ar[5] = c.ar[6];
    if (x.create(0xB0CB, 0x857)) x.markAnimated();
    saved.restore(c);
}

/// 042EE  som contínuo (`[B0C9]` = 0x4313) e animação `[42E2]` (`[B0CA]` = 0x4308).
void humHandler(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const HandlerRegs saved(c);
    x.create(0xB0C9);
    c.ar[6] = x.rd(0x42E2);
    if (x.create(0xB0CA)) x.markAnimated();
    saved.restore(c);
}

/// 04225  andarilho: processo `[B0C6]` (ID 0x16, trocado para 0x800), modelo sorteado de `[423D]`.
void wanderHandler(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t r0 = c.r[0], r2 = c.r[2], ar2 = c.ar[2];
    x.create(0xB0C6, 0x16);
    x.wr(c.ar[4] + 0x17, c.ar[0]);
    x.wr(c.ar[0] + 3, 0x800);
    x.wr(c.ar[4] + 0xE, (x.rd(c.ar[4] + 0xE) | kBit23) & ~0x400u);
    const uint32_t pick = x.randomBelow(4) + x.rd(0x423D);
    x.wr(c.ar[4] + 0xD, x.rd(pick));
    c.r[0] = r0; c.r[2] = r2; c.ar[2] = ar2;
}

/// Anda o vetor (0, 0, @p z) girado pela matriz do objeto AR4 (`0x9414`, `0x941E` em `[0x5D]`).
void stepForward(const Ctx& x, double z) {
    Tms32031State& c = x.c;
    const uint32_t r0 = c.r[0];
    x.call(0x9414);
    c.r[0] = r0;
    x.wf(c.ar[2] + 2, z);
    c.r[3] = c.ar[2];
    c.r[2] = c.ar[4] + 4;
    x.call(0x941E);
    for (uint32_t i = 0; i < 3; ++i) x.wf(c.ar[4] + 1 + i, x.f(c.ar[4] + 1 + i) + x.f(c.ar[2] + i));
}

/// 04278  um passo do andarilho (R6 por quadro) e um quadro de sono.
void wanderStep(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    // 0427A  MPYF ($C92F): o inteiro de [C92F] é lido como float (1 → 1,0000001)
    stepForward(x, x.R(6) * x.f(kTicks));
    x.sleep(1, 0x428B);
}

/// 04242  andarilho: às vezes dá meia-volta; avança 0..499 e sorteia a passada (50..79) · (1..3).
void wanderStart(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    if (static_cast<int32_t>(x.randomBelow(50)) >= 25) {
        stepForward(x, static_cast<double>(0x3E80));
        x.wf(c.ar[4] + 0x15, x.f(c.ar[4] + 0x15) + x.K(0x4257));
        c.ar[2] = c.ar[4] + 4;
        c.r[2] = x.rd(c.ar[4] + 0x15);
        x.call(0x938A);
    }
    stepForward(x, static_cast<double>(static_cast<int32_t>(x.randomBelow(500))));
    double r6 = static_cast<double>(static_cast<int32_t>(x.randomBelow(30))) + x.K(0x4272);
    r6 *= static_cast<double>(static_cast<int32_t>(x.randomBelow(3) + 1));
    x.setR(6, r6);
    wanderStep(c, b, s);
}

/// Próximo quadro da lista AR5 (negativo → recomeça em AR6) em `[AR4+0xD]`; dorme 1–2 (+@p extra)
/// quadros.
void animStep(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t extra, uint32_t resume) {
    Ctx x{c, b, s};
    for (;;) {
        c.r[0] = x.rd(c.ar[5]++);
        if (static_cast<int32_t>(c.r[0]) >= 0) break;
        c.ar[5] = c.ar[6];
    }
    x.wr(c.ar[4] + 0xD, c.r[0]);
    const uint32_t frames = (x.randomBelow(4) == 0 ? 1u : 2u) + extra;
    x.sleep(frames, resume);
}

/// 04330  som AR2 com volume R2 (canais contínuos `[E8FF]` = 1, `[E908]` = 2 via `0x9114`).
void soundWithVolume(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    auto setVolume = [&](uint32_t channel) {
        c.r[1] = c.r[2];
        c.r[0] = channel;
        x.call(0x9114);
    };
    if (c.ar[2] == x.rd(0xE8FF)) return setVolume(1);
    if (c.ar[2] == x.rd(0xE908)) return setVolume(2);
    x.call(0x9188);
    if (c.ar[2] == x.rd(0xE8FF)) setVolume(1);
    else if (c.ar[2] == x.rd(0xE908)) setVolume(2);
}

/// 04315  som contínuo 0xB7 com volume pela distância `[AR4+0x1C]`.
void humStep(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const int32_t dist = static_cast<int32_t>(x.rd(c.ar[4] + 0x1C));
    const double behind = dist < 0 ? x.K(0x4316) : x.K(0x4317);
    bool play = dist > -0x4E20;
    if (!play) {
        // 0431A  longe atrás: para e morre, salvo [E871] = 0 com o focado rodando ([D+0x31] ≠ 0)
        if (x.rd(0xE871) == 0 && x.rd(x.rd(0xE878) + 0x31) != 0) play = true;
    }
    if (!play) {
        c.ar[2] = kHumSound;
        x.call(0x91C1);
        x.call(0xA896);
        return;
    }
    // 04322  volume = 7 · (75 − |dist · fator| · 0,01 · 0,1), até 255
    double r2 = std::fabs(static_cast<double>(dist) * behind) * x.K(0x4325) * x.K(0x4326);
    r2 = x.K(0x4327) - r2;
    if (!(r2 < 0.0)) {
        int32_t vol = static_cast<int32_t>(tmsFix(r2 * x.K(0x4329)));
        if (vol > 0xFF) vol = 0xFF;
        c.r[2] = static_cast<uint32_t>(vol);
        c.ar[2] = kHumSound;
        x.call(0x4330);
    }
    x.sleep(1, 0x4315);
}

} // namespace

void registerTrackProps(SubroutineDispatcher& d) {
    using S = ISubroutines;
    using St = Tms32031State;
    using B = MemoryBus;
    // Tratadores da tabela [4500]
    d.add(0x4225, wanderHandler);
    d.add(0x428C, [](St& c, B& b, S& s) {
        const uint32_t r0 = c.r[0];
        s.call(0xAF58, c, b);
        b.writeWord(c.ar[4] + 0xF, 0x731);
        c.r[0] = r0;
    });
    d.add(0x4292, [](St& c, B& b, S& s) { s.call(0xAF58, c, b); });
    d.add(0x4293, [](St& c, B& b, S& s) { animHandler(c, b, s, 0x42A5, 0xB0D0, false); });
    d.add(0x429C, [](St& c, B& b, S& s) { animHandler(c, b, s, 0x42AD, 0xB0D0, false); });
    d.add(0x42C3, [](St& c, B& b, S& s) { animHandler(c, b, s, 0x42BB, 0xB0C7, false); });
    d.add(0x42D6, [](St& c, B& b, S& s) { animHandler(c, b, s, 0x42CF, 0xB0C8, false); });
    d.add(0x42EE, humHandler);
    d.add(0x43CB, [](St& c, B& b, S& s) { animHandler(c, b, s, 0x434C, 0xB0CE, true); });
    d.add(0x43DE, [](St& c, B& b, S& s) { animHandler(c, b, s, 0x4341, 0xB0CF, true); });
    d.add(0x4330, soundWithVolume);
    d.add(0x4366, prop768);
    d.add(0x438E, [](St& c, B& b, S& s) {
        Ctx x{c, b, s};
        const HandlerRegs saved(c);
        c.ar[6] = x.rd(0x435C);
        x.wr(c.ar[4] + 0xE, x.rd(c.ar[4] + 0xE) | 8u);
        c.ar[5] = c.ar[6];
        if (x.create(0xB0CC, 0x857)) x.markAnimated();
        saved.restore(c);
    });
    d.add(0x439E, [](St& c, B& b, S& s) { settle(Ctx{c, b, s}); });
    d.add(0x43AB, [](St& c, B& b, S& s) {
        const HandlerRegs saved(c);
        prop767(Ctx{c, b, s});
        saved.restore(c);
    });
    // Processos
    d.add(0x4242, wanderStart);
    d.add(0x428B, wanderStep);
    d.add(0x4308, [](St& c, B& b, S& s) {
        c.ar[5] = c.ar[6];
        animStep(c, b, s, 0, 0x4312);
    });
    d.add(0x4312, [](St& c, B& b, S& s) { animStep(c, b, s, 0, 0x4312); });
    d.add(0x4313, [](St& c, B& b, S& s) {
        c.ar[2] = 1;
        c.ret = 0x4315;
        s.call(0xA874, c, b);
    });
    d.add(0x4315, humStep);
    d.add(0x43C0, [](St& c, B& b, S& s) { animStep(c, b, s, 1, 0x43CA); });
    d.add(0x43CA, [](St& c, B& b, S& s) { animStep(c, b, s, 1, 0x43CA); });
    d.add(0x4401, [](St& c, B& b, S& s) {
        c.ar[5] = c.ar[6];
        animStep(c, b, s, 0, 0x440B);
    });
    d.add(0x4402, [](St& c, B& b, S& s) { animStep(c, b, s, 0, 0x440B); });
    d.add(0x440B, [](St& c, B& b, S& s) { animStep(c, b, s, 0, 0x440B); });
}

} // namespace rea::core::game
