/**
 * @file continue_screen.cpp
 * @brief Tradução de `0x1E2C`–`0x1EF1`.
 */
#include "core/game/continue_screen.hpp"

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kMode = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.
constexpr uint32_t kSavedMode = 0xE850;  ///< Modo guardado ao entrar na tela.
constexpr uint32_t kClock = 0xE604;  ///< Contagem regressiva por segundo, só com `[C8C5] & 0x200`.
constexpr uint32_t kStart = 0xE631;  ///< ≠ 0: o jogador apertou para pular

/// Grava @p v no item @p item da CMOS (`0x99F4`).
void setItem(ProcCtx& c, uint32_t item, uint32_t v) {
    c.cpu.r[2] = v;
    c.cpu.ar[2] = item;
    c.call(0x99F4);
}

/// `CALL $C10075` → R1 = créditos para continuar.
uint32_t continuePrice(ProcCtx& c) {
    c.call(0xC10075);
    return c.cpu.r[1];
}

/// Texto @p ptr em (256, y), 9999 quadros, estilo `0x76BF`, flags |= 0x100.
void bigText(ProcCtx& c, uint32_t ptr, double y) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(ptr);
    cpu.r[2] = doubleToTms(256.0);
    cpu.r[3] = doubleToTms(y);
    cpu.rc = 0x270F;
    c.call(0x7660);
    c.call(0x76BF);
    c.orField(cpu.ar[0], 8, 0x100);
}

/// 01ED3  restaura o modo e volta ao chamador.
void leave(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(kMode, c.rd(kSavedMode));
    cpu.r[0] = 0x34;
    cpu.r[1] = 0xFFFFFFFFu;
    c.call(0xA8CF);
    c.call(0x7648);
    c.call(0x9108);
    c.wf(0xC93A, 0.0);
    c.lret();
}

/// 01E89  START sem créditos recarrega o relógio; o relógio desce e o quadro passa.
void countDown(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(kStart) != 0) {
        c.wr(kStart, 0);
        c.wr(kClock, 0x14);
    }
    cpu.r[1] = c.rd(0xB22E);
    cpu.r[0] = c.rd(0xC937) & cpu.r[1];
    if (cpu.r[0] != 0 && cpu.r[0] != cpu.r[1]) c.wr(kClock, c.rd(kClock) - 1);
    cpu.ar[6] -= 1;
    if (s(cpu.ar[6]) <= 0 && (c.rd(0xC937) & 4u) != 0) {
        c.wr(kClock, c.rd(kClock) - 1);
        cpu.ar[6] = 0xF;
    }
}

/// 01EA4  `[E851]` = 0..24 e 1 quadro.
void nextFrame(ProcCtx& c) {
    uint32_t n = c.rd(0xE851) + 1;
    if (s(n) >= 0x19) n = 0;
    c.cpu.r[0] = n;
    c.wr(0xE851, n);
    c.sleep(1, 0x1EAB);
}

/// 01EAF  START com créditos: continua.
void accept(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[1] = 0x720000;
    cpu.r[0] = c.rd(0xC937) & cpu.r[1];
    if (cpu.r[0] == cpu.r[1]) c.wr(0xE636, 1);
    setItem(c, 0xF7, 0);
    cpu.ar[2] = 0x71;
    c.call(0x99E9);
    const uint32_t credits = cpu.r[0];
    int32_t left = s(credits) - s(continuePrice(c));
    if (left < 0) left = 0;
    setItem(c, 0x71, static_cast<uint32_t>(left));
    c.wr(kMode, (c.rd(kMode) & ~0xFu) | 4u);
    c.wr(0xE860, 1);
    c.wr(kClock, 0x3C);
    cpu.ar[6] = 1;
    c.wr(0xC8CD, c.rd(0xC8CD) & ~1u);
    cpu.ar[2] = 0x72;
    c.call(0x99DA);
    leave(c);
}

/// 01E6E  um quadro da espera.
void waitFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.call(0x75AF);
    cpu.ar[2] = 0xF7;
    c.call(0x99E9);
    if (cpu.r[0] != cpu.r[4]) {
        cpu.r[4] = cpu.r[0];
        c.wr(kClock, 0x14);
    }
    if (cpu.r[5] != 0) {
        if (c.rd(kStart) != 0) return accept(c);
        countDown(c);
        return nextFrame(c);
    }
    cpu.ar[2] = 0x19;
    c.call(0x99E9);
    bool ready = cpu.r[0] == 1;
    if (!ready) {
        cpu.ar[2] = 0x71;
        c.call(0x99E9);
        ready = s(cpu.r[0]) >= s(continuePrice(c));
    }
    if (ready) {
        // 01E81  "PRESS START" no lugar do 1º texto
        c.wr(cpu.ar[4] + 1, c.rd(0x1E2B));
        cpu.r[5] = 1;
        c.wr(0xC8CD, c.rd(0xC8CD) | 1u);
    } else {
        countDown(c);
    }
    nextFrame(c);
}

/// `0x1E2C`: entra na tela de continuar.
void enter(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    c.wr(kSavedMode, c.rd(kMode));
    c.call(0x4693);
    if (cpu.zero()) {
        c.call(0x4936);
        c.wr(0xEB4F, 0);
        c.wr(0xEB53, 0);
        c.wr(0xEB52, 0);
    }
    // 01E35  tempo jogado (centésimos de minuto) e média por partida
    cpu.r[2] = tmsFix(c.rf(0xC93A) * c.k(0x1E36));
    cpu.ar[2] = 0x73;
    c.call(0x99E9);
    setItem(c, 0x73, cpu.r[2] + cpu.r[0]);
    const uint32_t total = cpu.r[2];
    cpu.ar[2] = 0x72;
    c.call(0x99E9);
    cpu.r[1] = cpu.r[0];
    cpu.r[0] = total;
    c.call(0xA531);
    setItem(c, 0x74, cpu.r[0]);
    c.wr(kClock, 0x14);
    c.call(0x9166);
    cpu.ar[2] = 0xEA;
    c.call(0x91E0);
    c.wr(0xE860, 0);
    c.wr(kStart, 0);
    c.wr(kMode, (c.rd(kMode) & ~0xFu) | 0x207u);
    bigText(c, 0x1E29, 100.0);
    cpu.ar[4] = cpu.ar[0];
    bigText(c, 0x1E2A, 150.0);
    cpu.ar[2] = c.rd(0xB22D);
    cpu.r[2] = 0x34;
    c.call(0xA868);
    cpu.ar[2] = 0xF7;
    c.call(0x99E9);
    cpu.r[4] = cpu.r[0];
    cpu.r[5] = 0;
    cpu.ar[6] = 0xF;
    c.wr(0xE851, 0);
    waitFrame(c);
}

/// 01EE1  relógio `[E604]` em (256, 270) com a paleta `[B22F]`.
void clockFrame(ProcCtx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[2] = c.rd(kClock);
    cpu.ar[2] = c.rd(0x9CE0);
    c.call(0xA762);
    cpu.r[2] = doubleToTms(256.0);
    cpu.r[3] = doubleToTms(270.0);
    cpu.rc = 1;
    c.call(0x7660);
    c.orField(cpu.ar[0], 8, 0x100);
    c.call(0x76A7);
    const uint32_t obj = cpu.ar[0];
    cpu.ar[2] = c.rd(0xB22F);
    c.call(0x9EDB);
    c.wr(obj + 0xC, cpu.r[0]);
    c.sleep(1, 0x1EF1);
}

} // namespace

void registerContinueScreen(SubroutineDispatcher& d) {
    addProc(d, 0x1E2C, enter);
    addProc(d, 0x1EAB, [](ProcCtx& c) {
        if (c.rd(kClock) != 0) return waitFrame(c);
        c.cpu.ar[6] = 0;
        leave(c);
    });
    addProc(d, 0x1EE0, [](ProcCtx& c) {
        c.cpu.r[4] = c.rd(kClock);
        clockFrame(c);
    });
    addProc(d, 0x1EF1, clockFrame);
}

} // namespace rea::core::game
