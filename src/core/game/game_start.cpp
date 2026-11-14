/**
 * @file game_start.cpp
 * @brief Tradução de `0x9503`–`0x95C9`.
 */
#include "core/game/game_start.hpp"

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// Contexto do início do jogo: CPU, barramento, sub-rotinas e atalhos.
struct Ctx {
    Tms32031State& cpu;  ///< Registradores da CPU TMS320C31.
    MemoryBus& bus;  ///< Barramento de memória simulado.
    ISubroutines& subs;  ///< Despachante das sub-rotinas traduzidas.
    /// Lê com endereçamento direto (página DP, deslocamento @p off).
    uint32_t rd(uint32_t off) { return bus.readWord(cpu.direct(off)); }
    /// Grava @p v com endereçamento direto (página DP, deslocamento @p off).
    void wr(uint32_t off, uint32_t v) { bus.writeWord(cpu.direct(off), v); }
    /// Chama a sub-rotina traduzida no endereço da ROM indicado (o `CALL` original).
    void call(uint32_t a) { subs.call(a, cpu, bus); }
    /// `LDI @(ptr),AR2 / LDI id,R2 / CALL $A844`
    void spawn(uint32_t ptr, uint32_t id) {
        cpu.ar[2] = rd(ptr);
        cpu.r[2] = id;
        call(0xA844);
    }
    /// `LDI @($C8CD),R0 / ANDN $E,R0 / STI R0,@($C8CD)`
    void clearC8CD() {
        cpu.r[0] = rd(0xC8CD) & ~0x000Eu;
        wr(0xC8CD, cpu.r[0]);
    }
};

/// Trecho traduzido da ROM em `0x9579`.
void caseAttract(Ctx& c) {
    c.clearC8CD();
    c.cpu.r[0] = 0xFFFFFFFFu;
    c.wr(0xC943, c.cpu.r[0]);
    c.cpu.r[0] = 0x22;
    c.wr(0xC8C5, c.cpu.r[0]);
    c.spawn(0xC703, 0x500);
    c.spawn(0xC704, 0x500);
}

/// Trecho traduzido da ROM em `0x9587`.
void caseRace(Ctx& c) {
    c.clearC8CD();
    c.cpu.r[0] = 2;
    c.wr(0xC8C5, c.cpu.r[0]);
    c.call(0x59E3);
    c.spawn(0xC705, 0x500);
    c.cpu.ar[6] = c.rd(0xC8CF);
    c.spawn(0xC706, 0x500);
}

/// Trecho traduzido da ROM em `0x9596`.
void caseLoadAndRace(Ctx& c) {
    c.clearC8CD();
    c.cpu.ar[2] = c.rd(0xC707);
    c.call(0xA3C2);
    c.cpu.r[0] = 2;
    c.wr(0xC8C5, c.cpu.r[0]);
    c.spawn(0xC708, 0x500);
    c.spawn(0xC709, 0x500);
}

/// Trecho traduzido da ROM em `0x95A4`.
void case95A4(Ctx& c) {
    c.cpu.r[0] = 0;
    c.wr(0xC8E6, c.cpu.r[0]);
    c.cpu.r[0] = 2;
    c.wr(0xC8C5, c.cpu.r[0]);
    c.spawn(0xC70A, 0x202);
    c.cpu.r[0] = 0x384;
    c.wr(0xE84F, c.cpu.r[0]);
    c.spawn(0xC70B, 0x500);
    c.spawn(0xC70C, 0x500);
}

/// Trecho traduzido da ROM em `0x95BC`.
void load95BC(Ctx& c) {
    c.call(0x4EE3);
    c.cpu.r[0] = 1;
    c.wr(0xE5CB, c.cpu.r[0]);
    c.cpu.ar[2] = c.rd(0xC70E);
    c.call(0xA3C2);
    c.cpu.r[0] = 0xFFFFFFFFu;
    c.wr(0xC8E8, c.cpu.r[0]);
}

/// Trecho traduzido da ROM em `0x95B6`.
void case95B6(Ctx& c) {
    c.spawn(0xC70D, 0x500);
    c.cpu.r[0] = 0xFFFFFFFFu;
    c.wr(0xC8E8, c.cpu.r[0]);
}

/// Trecho traduzido da ROM em `0x9572`.
void case9572(Ctx& c) {
    c.clearC8CD();
    c.spawn(0xC702, 0x500);
}

/// Trecho traduzido da ROM em `0x95C4`.
void case95C4(Ctx& c) {
    c.call(0x92AD);
    c.cpu.r[0] = 1;
    c.wr(0xC8E9, c.cpu.r[0]);
    c.spawn(0xC70F, 0x110);
}

/// `0x9563`: AR2 = 1; CALL $A874 (retoma em 0x9565).
void waitPeerSleep(Ctx& c) {
    c.cpu.ar[2] = 1;
    c.cpu.ret = 0x9565;
    c.call(0xA874);
}

/// Trecho traduzido da ROM em `0x9565`.
void waitPeer(Ctx& c) {
    c.cpu.r[0] = c.rd(0xC8D6) & 0x000Fu;
    if (c.cpu.r[0] != 5 && c.cpu.r[0] != 6 && c.cpu.r[0] != 7) {
        c.cpu.r[0] = c.rd(0xC8DD);
        if (c.cpu.r[0] == 0) {
            waitPeerSleep(c);  // BEQ $9563
            return;
        }
    }
    // 0956F  [C8E8] = −7; BR $1E17
    c.cpu.r[0] = static_cast<uint32_t>(-7);
    c.wr(0xC8E8, c.cpu.r[0]);
    c.call(0x1E17);
}

/// `0x1E17`: reinicia o jogo no caso `[C8E8]` e volta ao loop principal (`0x4C1B`).
void restartGame(Ctx& c) {  // 01E17
    c.call(0x9166);
    c.cpu.sp = c.rd(0x0063);
    c.cpu.ar[2] = c.rd(0xC8E8);
    c.cpu.ret = 0x1E1B;
    c.call(0x9503);
    c.cpu.jump = 0x4C1B;  // BU $4C1B
}

/// `0x1E1C`: avança o ciclo de atração (`[C8E8]` = −1, −2, −3, −4, −1…) e reinicia.
void nextAttract(Ctx& c) {  // 01E1C
    c.call(0x9166);
    c.call(0x95F5);
    c.cpu.sp = c.rd(0x0063);
    c.cpu.ar[2] = c.rd(0xC8E8) - 1;
    if (s(c.cpu.ar[2]) < -4) c.cpu.ar[2] = 0xFFFFFFFFu;
    c.wr(0xC8E8, c.cpu.ar[2]);
    c.cpu.ret = 0x1E25;
    c.call(0x9503);
    c.cpu.jump = 0x4C1B;  // BU $4C1B
}

/// `0x1E26`: processo que espera `[E84F]` quadros e chama `0x1E1C`.
void attractTimer(Ctx& c) {  // 01E26
    c.cpu.ar[2] = c.rd(0xE84F);
    c.cpu.ret = 0x1E28;
    c.call(0xA874);
}

/// `0x903B`: processo de sistema. Alterna a promoção (`0x726F`) e o
/// rebaixamento (`0x7254`) das listas de visibilidade, um por quadro.
void cullProcessPromote(Ctx& c) {  // 0903B (e 09041: BU $903B)
    c.call(0x726F);
    c.cpu.ar[2] = 1;
    c.cpu.ret = 0x903E;
    c.call(0xA874);
}

/// Trecho traduzido da ROM em `0x903E`.
void cullProcessDemote(Ctx& c) {
    c.call(0x7254);
    c.cpu.ar[2] = 1;
    c.cpu.ret = 0x9041;
    c.call(0xA874);
}

} // namespace

void startGame(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus, subs};
    // 09503  POP AR7 (o retorno sai da pilha antes de zerar a RAM interna)
    cpu.ar[7] = cpu.ret;
    c.call(0x9166);
    // 09505  zera 0x809800–0x809FFF (RPTS $7FF)
    cpu.r[0] = 0;
    cpu.ar[0] = 0x809800;
    for (uint32_t i = 0; i < 0x800; ++i) bus.writeWord(cpu.ar[0]++, cpu.r[0]);
    cpu.rc = 0xFFFFFFFFu;
    c.call(0x7F18);
    // 0950C  limpa a BSS (0x4E35) preservando E573–E576
    const uint32_t e575 = c.rd(0xE575), e576 = c.rd(0xE576), e573 = c.rd(0xE573), e574 = c.rd(0xE574);
    c.call(0x4E35);
    c.wr(0xE574, e574);
    c.wr(0xE573, e573);
    c.wr(0xE576, e576);
    c.wr(0xE575, e575);
    cpu.r[0] = e575;
    // 0951D  RAM principal 0x0000–0x10000 → RAM rápida 0x400000 (RPTB, RC = 0x10000)
    cpu.ar[1] = 0;
    cpu.ar[3] = 0x400000;
    cpu.rc = 0x10000;
    for (;;) {
        cpu.r[0] = bus.readWord(cpu.ar[1]++);
        bus.writeWord(cpu.ar[3]++, cpu.r[0]);
        if (s(--cpu.rc) < 0) break;
    }
    // 09525  PUSH AR7; PUSH AR2
    const uint32_t arg = cpu.ar[2];
    c.call(0x7F4D);
    c.call(0x95CB);
    // 09529  câmera: zera 0x809800–02, 06–08, 14; 0,70703 em 12/13
    const uint32_t zero_f = doubleToTms(0.0);
    for (uint32_t off : {0x9806u, 0x9807u, 0x9808u, 0x9800u, 0x9801u, 0x9802u, 0x9814u}) bus.writeWord(0x800000u | off, zero_f);
    const uint32_t k = doubleToTms(0.70703125);  // LDF 0.707031 (curto 0xF350)
    bus.writeWord(0x809812, k);
    bus.writeWord(0x809813, k);
    cpu.r[0] = k;
    cpu.dp = 0;
    c.call(0x7409);
    cpu.ar[0] = c.rd(0x0047);
    c.call(0x93D7);
    // 09539  [E3FD] = 35,0; [C93F] = 1; [EA48] = FLOAT(CMOS item 5)
    c.wr(0xE3FD, doubleToTms(35.0));
    cpu.r[0] = 1;
    c.wr(0xC93F, cpu.r[0]);
    cpu.ar[2] = 5;
    c.call(0x99E9);
    c.wr(0xEA48, doubleToTms(static_cast<double>(s(cpu.r[0]))));
    c.wr(0xC8EB, zero_f);
    c.wr(0xC8EC, zero_f);
    cpu.r[0] = 0;
    c.wr(0xE541, cpu.r[0]);
    c.wr(0xE8E3, cpu.r[0]);
    c.call(0x95F5);
    // 09548  processo de sistema [C701], ID 0x500
    c.spawn(0xC701, 0x500);
    // 0954B  POP AR2
    cpu.ar[2] = arg;
    if (cpu.ar[2] == 1) {
        case95C4(c);
        return;
    }
    // 0954E  ~[0x991030] & 0x70 == 0x60 → 0x95B6 (leitura descartada de [0])
    cpu.ar[0] = 0;
    cpu.r[0] = ~bus.readWord(0x991030);
    cpu.r[2] = bus.readWord(cpu.ar[0]);
    cpu.r[0] &= 0x70u;
    if (cpu.r[0] == 0x60) {
        case95B6(c);
        return;
    }
    // 09556  CALLU [AR2 + [0x955B]]
    cpu.ar[0] = cpu.ar[2] + c.rd(0x955B);
    cpu.r[0] = bus.readWord(cpu.ar[0]);
    c.call(cpu.r[0]);
}

/// `0x7421`: R6 = itens AR2, AR2 − 1, ... da CMOS (byte baixo de cada) em R5, R5 − 8, ... ≥ 0; `*AR3++` = R6.
void packCmosItems(Ctx& c) {
    c.cpu.r[6] = 0;
    for (;;) {
        const uint32_t ar2 = c.cpu.ar[2];
        c.call(0x99E9);
        c.cpu.ar[2] = ar2 - 1;
        c.cpu.r[0] = tmsLsh(c.cpu.r[0] & 0xFFu, c.cpu.r[5]);
        c.cpu.r[6] |= c.cpu.r[0];
        c.cpu.r[5] -= 8;
        if (static_cast<int32_t>(c.cpu.r[5]) < 0) break;
    }
    c.bus.writeWord(c.cpu.ar[3]++, c.cpu.r[6]);
}

/// `0x7409`: empacota os ajustes da CMOS em `[[0x742D]]` (= 0xE542), 11 palavras.
void packSettings(Ctx& c) {
    auto pack = [&](uint32_t item, uint32_t shift) {
        c.cpu.ar[2] = item;
        c.cpu.r[5] = shift;
        packCmosItems(c);
    };
    c.cpu.r[5] = 0x18;
    c.cpu.ar[3] = c.rd(0x742D);
    pack(0x0C, 0x18);  // itens 0xC..0x9
    pack(0x10, 0x18);  // itens 0x10..0xD
    pack(0x12, 0x08);  // itens 0x12, 0x11
    // 07413  [744A] (= 0x744B), 0, 0, e 4 × [744A]
    const uint32_t v = c.rd(0x744A);
    const uint32_t seq[7] = {v, 0, 0, v, v, v, v};
    for (uint32_t w : seq) {
        c.cpu.r[0] = w;
        c.bus.writeWord(c.cpu.ar[3]++, c.cpu.r[0]);
    }
    pack(0x16, 0x18);  // itens 0x16..0x13
}

/// `0x7F4D`: `[C8D1]` = 1, `[C8D0]` = 0; `[[0x7F17]]` (= 0x997000) = (`[C8CC]` bit 0 ? 0x3000 : −0x4000) << 16.
void configLinkBoard(Ctx& c) {
    const uint32_t r0 = c.cpu.r[0], ar0 = c.cpu.ar[0];
    c.wr(0xC8D1, 1);
    c.wr(0xC8D0, 0);
    c.cpu.r[0] = (c.rd(0xC8CC) & 1u) ? 0x3000u : static_cast<uint32_t>(-0x4000);
    c.cpu.ar[0] = c.rd(0x7F17);
    c.bus.writeWord(c.cpu.ar[0], c.cpu.r[0] << 16);
    c.cpu.ar[0] = ar0;
    c.cpu.r[0] = r0;
}

void registerRoutines(SubroutineDispatcher& d) {
    auto wrap = [](void (*fn)(Ctx&)) {
        return [fn](Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
            Ctx c{cpu, bus, subs};
            fn(c);
        };
    };
    d.add(0x9503, startGame);
    d.add(0x7409, wrap(packSettings));
    d.add(0x7421, wrap(packCmosItems));
    d.add(0x7F4D, wrap(configLinkBoard));
    d.add(0x9579, wrap(caseAttract));
    d.add(0x9587, wrap(caseRace));
    d.add(0x9596, wrap(caseLoadAndRace));
    d.add(0x95A4, wrap(case95A4));
    d.add(0x95B4, wrap([](Ctx& c) { load95BC(c); caseAttract(c); }));
    d.add(0x95BC, wrap(load95BC));
    d.add(0x95B6, wrap(case95B6));
    d.add(0x9572, wrap(case9572));
    d.add(0x95C4, wrap(case95C4));
    d.add(0x9563, wrap(waitPeerSleep));
    d.add(0x9565, wrap(waitPeer));
    d.add(0x1E17, wrap(restartGame));
    d.add(0x1E1C, wrap(nextAttract));
    d.add(0x1E26, wrap(attractTimer));
    d.add(0x1E28, wrap(nextAttract));  // BU $1E1C
    d.add(0x903B, wrap(cullProcessPromote));
    d.add(0x9041, wrap(cullProcessPromote));
    d.add(0x903E, wrap(cullProcessDemote));
}

} // namespace rea::core::game
