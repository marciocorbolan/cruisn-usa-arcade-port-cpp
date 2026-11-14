/**
 * @file coin_start.cpp
 * @brief Tradução de `0x1D7F`–`0x1DC2` (START) e `0x7314`–`0x739F` (moedas e créditos).
 */
#include "core/game/coin_start.hpp"

namespace rea::core::game {

namespace {

using namespace coin_vars;

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// Contexto das moedas e do start: CPU, barramento, sub-rotinas e atalhos.
struct Ctx {
    Tms32031State& cpu;  ///< Registradores da CPU TMS320C31.
    MemoryBus& bus;  ///< Barramento de memória simulado.
    ISubroutines& subs;  ///< Despachante das sub-rotinas traduzidas.
    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) { return bus.readWord(a); }
    /// Grava a palavra @p v no endereço @p a.
    void wr(uint32_t a, uint32_t v) { bus.writeWord(a, v); }
    /// Chama a sub-rotina traduzida no endereço da ROM indicado (o `CALL` original).
    void call(uint32_t a) { subs.call(a, cpu, bus); }
    /// `LDI item,AR2 / CALL $99E9` → R0.
    uint32_t item(uint32_t i) {
        cpu.ar[2] = i;
        call(0x99E9);
        return cpu.r[0];
    }
    /// `LDI v,R2 / LDI item,AR2 / CALL $99F4`.
    void setItem(uint32_t i, uint32_t v) {
        cpu.r[2] = v;
        cpu.ar[2] = i;
        call(0x99F4);
    }
    /// Incrementa o item de auditoria @p i (`0x99DA`).
    void audit(uint32_t i) {
        cpu.ar[2] = i;
        call(0x99DA);
    }
    /// Toca o som @p id (`0x91E0`).
    void sound(uint32_t id) {
        cpu.ar[2] = id;
        call(0x91E0);
    }
    /// `LDI @(addr),R1 / ADDI R0,R1 / STI R1,@(addr)` (com DP = 0).
    void addMeter(uint32_t addr) {
        cpu.dp = 0;
        cpu.r[1] = rd(addr) + cpu.r[0];
        wr(addr, cpu.r[1]);
        cpu.dp = 0;  // SUBI DP,DP
    }
    /// Item 0x71 + 1, limitado ao item 0x2D (`07391`–`0739B`, também em `07354` e `07375`).
    void giveCredit() {
        cpu.r[1] = item(kItemCredits) + 1;
        if (s(cpu.r[1]) >= s(item(kItemMaxCred))) cpu.r[1] = cpu.r[0];  // CMPI R0,R1 / LDIGE R0,R1
        setItem(kItemCredits, cpu.r[1]);
    }
    /// Encerra o processo atual (`0xA896`).
    void die() { call(0xA896); }
};

/// `0x7360`: converte R2 unidades de moeda em créditos e morre.
void convertCoins(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    if (cpu.r[2] == 0) return c.die();  // 07360
    cpu.r[1] = cpu.r[2];
    cpu.ar[2] = kItemCoins;
    c.call(0x99E2);                     // 07364  item 0x70 += unidades
    cpu.r[2] = cpu.r[1];
    cpu.ar[2] = kItemBonus;
    c.call(0x99E2);                     // 07367  item 0xF7 += unidades
    cpu.r[2] = c.item(kItemCoins);
    for (;;) {                          // 0736B
        c.call(0xC1005B);
        if (s(cpu.r[2]) < s(cpu.r[1])) break;
        c.call(0xC10042);               // unidades por crédito
        if (s(cpu.r[2]) < s(cpu.r[1])) break;
        cpu.r[2] -= cpu.r[1];
        cpu.ar[2] = kItemCoins;
        c.call(0x99F4);
        const uint32_t units = cpu.r[2];  // PUSH R2
        c.giveCredit();
        c.audit(0x69);
        cpu.r[2] = units;                 // POP R2
    }
    // 07386  crédito bônus
    cpu.r[2] = c.item(kItemBonus);
    c.call(0xC1004E);
    if (cpu.r[1] == 0 || s(cpu.r[2]) < s(cpu.r[1])) return c.die();
    c.setItem(kItemBonus, 0);
    c.giveCredit();
    c.setItem(kItemCoins, 0);
    c.die();
}

/**
 * Moeda: som, auditoria, aviso ao link (opcional), pulsos do contador e conversão.
 * @param meter    Leitor do campo do contador (`0xC1008E`…).
 * @param units    Leitor das unidades da moeda (`0xC1000F`…).
 * @param c Contexto do processo (CPU, barramento e sub-rotinas).
 * @param audit Item de auditoria da CMOS incrementado para esta moeda.
 * @param link Se true, avisa a outra cabine pelo link (`0x7C12`).
 * @param selectMeter Se true, o bit 0x100 de `[kMeterFlags]` escolhe o contador (zerado → contador 2); se false, sempre o contador 1.
 */
void coin(Ctx& c, uint32_t audit, bool link, uint32_t meter, uint32_t units, bool selectMeter) {
    c.sound(kSndCoin);
    c.audit(audit);
    if (link) c.call(0x7C12);
    const bool meter1 = !selectMeter || (c.rd(kMeterFlags) & 0x100u) != 0;  // 07326  TSTB / BEQ → contador 2
    c.call(meter);
    c.addMeter(meter1 ? kMeter1 : kMeter2);
    c.call(units);  // R2 = unidades
    convertCoins(c);
}

/// `0x1D92`: processo do botão START.
void startButton(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.r[0] = c.rd(kGameMode) & 0xFu;
    switch (cpu.r[0]) {
    case 3: case 5: case 6: case 7: case 8:
        cpu.r[0] = 1;
        c.wr(kContinueReq, 1);
        return c.die();
    case 4:
        return c.die();
    default:
        break;
    }
    c.call(0x1D7F);
    if (!cpu.carry()) {  // 01DA5  sem créditos: bipe uma vez
        cpu.r[0] = c.rd(kNoCreditBeep);
        if (s(cpu.r[0]) > 0) return c.die();
        c.wr(kNoCreditBeep, ++cpu.r[0]);
        c.sound(kSndNoCredit);
        return c.die();
    }
    // 01DAD  desconta o preço e começa o jogo
    c.call(0xC10068);
    cpu.r[0] -= cpu.r[1];
    cpu.r[2] = s(cpu.r[0]) < 0 ? 0u : cpu.r[0];
    cpu.ar[2] = kItemCredits;
    c.call(0x99F4);
    c.setItem(kItemBonus, 0);
    cpu.r[0] = (c.rd(kGameMode) & ~0xFu) | 3u;
    c.wr(kGameMode, cpu.r[0]);
    cpu.r[0] = 0;
    c.wr(kE876, 0);
    cpu.dp = 0;
    cpu.sp = c.rd(kStackTop);
    cpu.ar[2] = 1;
    cpu.ret = 0x1DC1;
    c.call(0x9503);
    cpu.jump = 0x4C1B;  // BR $4C1B
}

} // namespace

void hasStartCredits(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus, subs};
    const uint32_t r2 = cpu.r[2], ar2 = cpu.ar[2];
    bool ok = c.item(kItemFreePlay) == 1;
    if (!ok) {
        c.call(0xC10068);  // R1 = preço
        ok = s(c.item(kItemCredits)) >= s(cpu.r[1]);
    }
    cpu.setFlag(Tms32031State::kStCarry, ok);
    cpu.ar[2] = ar2;
    cpu.r[2] = r2;
}

void registerCoinStart(SubroutineDispatcher& d) {
    auto add = [&d](uint32_t a, void (*f)(Ctx&)) {
        d.add(a, [f](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
            Ctx x{c, b, s};
            f(x);
        });
    };
    d.add(0x1D7F, hasStartCredits);
    add(0x1D92, startButton);
    add(0x7314, [](Ctx& c) { coin(c, 0x64, true, 0xC1008E, 0xC1000F, false); });
    add(0x7321, [](Ctx& c) { coin(c, 0x65, true, 0xC10096, 0xC1001B, true); });
    add(0x7338, [](Ctx& c) { coin(c, 0x65, false, 0xC1009F, 0xC10028, false); });
    add(0x7344, [](Ctx& c) { coin(c, 0x67, false, 0xC100A8, 0xC10035, false); });
    add(0x7350, [](Ctx& c) {
        c.sound(kSndCoin);
        c.audit(0x68);
        c.giveCredit();
        c.die();
    });
    add(0x7360, convertCoins);
}

} // namespace rea::core::game
