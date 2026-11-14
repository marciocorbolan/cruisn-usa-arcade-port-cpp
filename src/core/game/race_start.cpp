/**
 * @file race_start.cpp
 * @brief Tradução de `0x1C9B`–`0x1D37`.
 */
#include "core/game/race_start.hpp"

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

constexpr uint32_t kGridLock = 0xE5EC; ///< 0x33BB = carros presos no grid; 0x5577 = largada.

/// Modo do jogo (`[C8C5]` & 0xF).
uint32_t mode(MemoryBus& b) { return b.readWord(0xC8C5) & 0xFu; }
/// true se a corrida tem link.
bool linked(MemoryBus& b) { return b.readWord(0xC8E3) != 0; }

/// Dorme @p frames quadros (`0xA874`) e retoma em @p pc.
void sleep(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t frames, uint32_t pc) {
    c.ar[2] = frames;
    c.ret = pc;
    s.call(0xA874, c, b);
}

/// Mostra o texto `[AR6++]` em (256, 160) por 0x14 quadros e toca o som `[AR6++]`.
void showStep(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.ar[2] = b.readWord(c.ar[6]++);
    c.r[2] = doubleToTms(256.0);
    c.r[3] = doubleToTms(160.0);
    c.rc = 0x14;
    s.call(0x7660, c, b);
    s.call(0x76BF, c, b);
    b.writeWord(c.ar[0] + 8, b.readWord(c.ar[0] + 8) | 0x100u);
    c.ar[2] = b.readWord(c.ar[6]++);
    s.call(0x9188, c, b);
}

void stepLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s);

/// 01CDE  um passo da contagem.
void countStep(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    showStep(c, b, s);
    if (linked(b) && (b.readWord(0xC8CC) & 1u) == 0) {
        c.r[0] = b.readWord(c.ar[6]);
        s.call(c.r[0], c, b);
    }
    c.ar[6] += 1;
    c.ar[5] -= 1;
    if (static_cast<int32_t>(c.ar[5]) > 0) return stepLoop(c, b, s);
    // 01CF4
    c.ar[6] = b.readWord(0xB222);
    sleep(c, b, s, 0xF, 0x1CF7);
}

/// 01CD1  escravo do link: espera o passo `[E63B]` do mestre.
void linkWait(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    const int32_t step = static_cast<int32_t>(b.readWord(0xE84D));
    if (step == 3) {
        c.ar[6] = b.readWord(0xB222);
        return sleep(c, b, s, 0xF, 0x1CF7);
    }
    if (step < static_cast<int32_t>(b.readWord(0xE63B))) {
        b.writeWord(0xE84D, b.readWord(0xE63B));
        return countStep(c, b, s);
    }
    sleep(c, b, s, 1, 0x1CD1);
}

/// 01CCC  espera antes de cada passo.
void stepLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    if (linked(b) && (b.readWord(0xC8CC) & 1u)) return linkWait(c, b, s);
    sleep(c, b, s, 0x14, 0x1CDE);
}

/// 01CC8  som 0x90 e início da contagem.
void beginCount(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.ar[2] = 0x90;
    s.call(0x9188, c, b);
    c.ar[5] = 2;
    c.ar[6] = b.readWord(0xB221);
    stepLoop(c, b, s);
}

/// 01CC4  espera de sincronia do link (`[C8E1]` = 3).
void linkSync(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    s.call(0x7BF6, c, b);
    if (b.readWord(0xC8E1) != 3) return sleep(c, b, s, 1, 0x1CC4);
    beginCount(c, b, s);
}

/// 01C9B  início.
void start(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.dp = 0;
    c.r[0] = 0;
    b.writeWord(0xE860, 0);
    b.writeWord(0xE84C, 0);
    b.writeWord(0xE85F, 0);
    c.r[5] = b.readWord(0xC8C5);
    b.writeWord(0xC8C5, c.r[5] | 0x110u);
    c.ar[2] = 0x1A;
    s.call(0x99E9, c, b);
    if (c.r[0] != 0) {
        c.ar[2] = b.readWord(0xB21E);
        c.r[2] = 0x500;
        s.call(0xA844, c, b);
        if (mode(b) != 2) {
            c.ar[2] = b.readWord(0xB21F);
            c.r[2] = 0x500;
            s.call(0xA844, c, b);
        }
    }
    sleep(c, b, s, 5, 0x1CB5);
}

/// 01CB5
void afterStart(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    if (mode(b) != 2) {
        c.ar[2] = b.readWord(0xB220);
        c.r[2] = 0x500;
        s.call(0xA868, c, b);
    }
    c.r[0] = 0;
    b.writeWord(0xE84D, 0);
    if (linked(b)) {
        b.writeWord(0xC8E1, 0);
        return sleep(c, b, s, 1, 0x1CC4);
    }
    beginCount(c, b, s);
}

/// 01CFB  "GO": libera o grid e morre.
void go(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    showStep(c, b, s);
    if (linked(b) && (b.readWord(0xC8CC) & 1u) == 0) s.call(0x79C4, c, b);
    c.r[5] = (c.r[5] | 0x200u) & ~0x10u;
    b.writeWord(0xC8C5, c.r[5]);
    b.writeWord(0xE860, c.r[5]);
    s.call(0x4693, c, b);
    bool skip = (c.r[0] & 0x40u) != 0;
    if (!skip) {
        s.call(0x469D, c, b);
        skip = c.r[0] != 1;
    }
    if (!skip && (b.readWord(0x991030) & b.readWord(0xB223)) != 0) b.writeWord(0xEB52, 0x11D);
    // 01D23
    b.writeWord(kGridLock, 0x5577);
    c.ar[2] = 0x93;
    s.call(0x9188, c, b);
    c.ar[2] = 0x1C;
    s.call(0x99E9, c, b);
    b.writeWord(0xE604, tmsMpyi(c.r[0], 5) + 0x3C);
    if (linked(b) && b.readWord(0xC8E0) == 1) b.writeWord(0xE636, 1);
    if (mode(b) != 2) s.call(0x9108, c, b);
    s.call(0xA896, c, b);
}


/// 03C1C  um quadro do aviso do ajuste 0x1A: dois textos com sombra ([B107], [B109]) acima da
///        prioridade normal (+8 |= 0x100), cor sorteada de [B108]/[B10A]; posição pelo modo (atrato = 2).
void bannerFrame(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.r[4] = b.readWord(0xC8C5) & 0xFu;
    const bool attract = c.r[4] == 2;
    auto text = [&](uint32_t textPtr, uint32_t colorPtr, int32_t y) {
        c.r[2] = doubleToTms(attract ? 0x16D : 0x100);
        c.r[3] = doubleToTms(static_cast<double>(y));
        c.rc = 1;
        c.ar[2] = b.readWord(textPtr);
        s.call(0x7651, c, b);
        b.writeWord(c.ar[0] + 8, b.readWord(c.ar[0] + 8) | 0x100u);
        b.writeWord(c.ar[1] + 8, b.readWord(c.ar[1] + 8) | 0x100u);
        c.ar[2] = b.readWord(colorPtr);
        s.call(0x9EDB, c, b);
        b.writeWord(c.ar[0] + 0xC, c.r[0]);
        b.writeWord(c.ar[1] + 0xC, c.r[0]);
    };
    text(0xB107, 0xB108, attract ? 0x7D : 0x46);
    text(0xB109, 0xB10A, attract ? 0x96 : 0x5A);
    sleep(c, b, s, 1, 0x3C48);
}

} // namespace

void registerRaceStart(SubroutineDispatcher& d) {
    d.add(0x1C9B, start);
    d.add(0x1CB5, afterStart);
    d.add(0x1CC4, linkSync);
    d.add(0x1CCC, stepLoop);
    d.add(0x1CD1, linkWait);
    d.add(0x1CDE, countStep);
    d.add(0x1CF7, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        b.writeWord(0xE84C, 1);
        sleep(c, b, s, 5, 0x1CFB);
    });
    d.add(0x1CFB, go);
    // 03C19  processo do aviso (criado em 0x1CA9 quando o ajuste 0x1A ≠ 0): espera 5 quadros e mostra
    //        os dois textos por 0x5B quadros
    d.add(0x3C19, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { sleep(c, b, s, 5, 0x3C1B); });
    d.add(0x3C1B, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[5] = 0x5A;
        bannerFrame(c, b, s);
    });
    d.add(0x3C48, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        if (static_cast<int32_t>(--c.ar[5]) >= 0) bannerFrame(c, b, s);
        else s.call(0xA896, c, b);
    });
}

} // namespace rea::core::game
