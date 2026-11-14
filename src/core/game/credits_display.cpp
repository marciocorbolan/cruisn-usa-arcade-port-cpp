/**
 * @file credits_display.cpp
 * @brief Tradução de `0x745E`–`0x7591`, `0x762E`, `0x73A0`–`0x73B5`, `0x77E7`,
 *        `0x73B7`, `0xC1000F`–`0xC10035` e `0xC10042`–`0xC100AF`.
 */
#include "core/game/credits_display.hpp"

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

using namespace credit_vars;

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// Lê o item @p item da CMOS (`0x99E9`).
uint32_t cmosItem(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t item) {
    cpu.ar[2] = item;
    subs.call(0x99E9, cpu, bus);
    return cpu.r[0];
}

/// Leitor de campo do registro de preços (`0xC10042`…): R1 = (`[AR0 + word]` >> shift) & 0xFF.
uint32_t priceField(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t word, uint32_t shift) {
    const uint32_t ar0 = cpu.ar[0], ar2 = cpu.ar[2], r0 = cpu.r[0];
    priceRecord(cpu, bus, subs);
    cpu.r[1] = (bus.readWord(cpu.ar[0] + word) >> shift) & 0xFFu;
    cpu.r[0] = r0;
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
    return cpu.r[1];
}

/// C10068
uint32_t startPrice(Tms32031State& c, MemoryBus& b, ISubroutines& s) { return priceField(c, b, s, 1, 24); }
/// C10075
uint32_t continuePrice(Tms32031State& c, MemoryBus& b, ISubroutines& s) { return priceField(c, b, s, 2, 0); }

/// Soma @p v ao float de R3.
void addR3(Tms32031State& cpu, double v) { cpu.r[3] = doubleToTms(tmsToDouble(cpu.r[3]) + v); }

/// Texto com sombra centralizado (flag 0x100) em (R2, R3), vida 1.
void centered(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t text) {
    cpu.ar[2] = text;
    cpu.rc = 1;
    subs.call(0x7651, cpu, bus);
    cpu.r[0] = bus.readWord(cpu.ar[0] + 8) | 0x100u;
    bus.writeWord(cpu.ar[0] + 8, cpu.r[0]);
    cpu.r[0] = bus.readWord(cpu.ar[1] + 8) | 0x100u;
    bus.writeWord(cpu.ar[1] + 8, cpu.r[0]);
}

/**
 * Pisca: com `[E56C]` ≤ 0 conta para baixo (aceso; abaixo de −limit vira 1) e devolve true;
 * com `[E56C]` > 0 conta para cima (apagado; acima de limit vira −1).
 */
bool blink(Tms32031State& cpu, MemoryBus& bus, int32_t limit) {
    cpu.r[0] = bus.readWord(kBlink);
    const int32_t frames = s(bus.readWord(kFrames));
    if (s(cpu.r[0]) > 0) {
        cpu.r[0] += static_cast<uint32_t>(frames);
        if (s(cpu.r[0]) > limit) cpu.r[0] = 0xFFFFFFFFu;
        bus.writeWord(kBlink, cpu.r[0]);
        return false;
    }
    cpu.r[0] -= static_cast<uint32_t>(frames);
    if (s(cpu.r[0]) < -limit) cpu.r[0] = 1;
    bus.writeWord(kBlink, cpu.r[0]);
    return true;
}

/// 074A5  "INSERT COINS" em (256, R3)
void insertCoinsText(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[2] = doubleToTms(256.0);
    centered(cpu, bus, subs, bus.readWord(kInsertCoins));
    subs.call(0x7450, cpu, bus);
}

/// 0749F  "INSERT COINS" piscando (±30)
void insertCoinsBlink(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    if (blink(cpu, bus, 30)) insertCoinsText(cpu, bus, subs);
}

/// 074B7  [C8CD] = 1; "PRESS START" piscando (±20)
void pressStart(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = 1;
    bus.writeWord(kStartFlag, cpu.r[0]);
    if (!blink(cpu, bus, 20)) return;
    cpu.r[2] = doubleToTms(256.0);
    centered(cpu, bus, subs, bus.readWord(kPressStart));
    subs.call(0x7450, cpu, bus);
}

/// 074FC  R3 = 17 × linhas da tabela de preços (1 a 3). Preserva R0 e AR0.
void priceLinesHeight(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0];
    subs.call(0x73B3, cpu, bus);
    double h = 17.0;
    if (bus.readWord(cpu.ar[0] + 1) != 0) {
        h += 17.0;
        if (bus.readWord(cpu.ar[0] + 2) != 0) h += 17.0;
    }
    cpu.r[3] = doubleToTms(h);
    cpu.ar[0] = ar0;
    cpu.r[0] = r0;
}

/// 074D1  até 3 linhas da tabela de preços em (R2, R3), 17 em 17
void priceLines(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    for (uint32_t i = 0; i < 3; ++i) {
        subs.call(0x73B3, cpu, bus);
        const uint32_t text = bus.readWord(cpu.ar[0] + i);
        if (i > 0) {
            if (text == 0) return;
            addR3(cpu, 17.0);
        }
        centered(cpu, bus, subs, text);
        subs.call(0x76E2, cpu, bus);
        // 0750B  paleta [B121]
        cpu.ar[2] = bus.readWord(kPricePalette);
        subs.call(0x9EDB, cpu, bus);
        bus.writeWord(cpu.ar[0] + 0xC, cpu.r[0]);
        bus.writeWord(cpu.ar[1] + 0xC, cpu.r[0]);
    }
}

/// Converte @p value em texto no buffer `[@p bufPtr]` (`0xA762`).
void number(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t value, uint32_t bufPtr) {
    cpu.r[2] = value;
    cpu.ar[2] = bus.readWord(bufPtr);
    subs.call(0xA762, cpu, bus);
}

/// Acrescenta o texto `[@p srcPtr]` ao fim de `[@p dstPtr]`.
void append(Tms32031State& cpu, MemoryBus& bus, uint32_t dstPtr, uint32_t srcPtr) {
    cpu.ar[0] = bus.readWord(dstPtr);
    cpu.ar[1] = bus.readWord(srcPtr);
    appendString(cpu, bus);
}

/// 07510  saldo em [743B]: "c  m@n CREDITS" (créditos, moedas parciais / denominador)
void balanceString(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[7] = 0;
    bus.writeWord(bus.readWord(kBalanceStr), cpu.r[7]);  // STI R7,($E55B)
    cpu.r[4] = priceField(cpu, bus, subs, 2, 8);       // C10081
    cpu.r[2] = cmosItem(cpu, bus, subs, kItemCredits);
    if (cpu.r[2] != 0 || cpu.r[4] != 1) {
        cpu.r[7] = 1;
        number(cpu, bus, subs, cpu.r[2], kNumBuf1);
        append(cpu, bus, kBalanceStr, kNumBuf1);
        append(cpu, bus, kBalanceStr, kTwoSpaces);
    }
    cpu.r[2] = cmosItem(cpu, bus, subs, kItemCoins);
    if (cpu.r[2] != 0 && cpu.r[4] != 0) {
        cpu.r[7] = 1;
        number(cpu, bus, subs, cpu.r[2], kNumBuf2);
        append(cpu, bus, kBalanceStr, kNumBuf2);
        append(cpu, bus, kBalanceStr, kSlash);
        cpu.r[2] = priceField(cpu, bus, subs, 1, 0);   // C10042
        number(cpu, bus, subs, cpu.r[2], kDenomBuf);
        append(cpu, bus, kBalanceStr, kDenomBuf);
        append(cpu, bus, kBalanceStr, kSpace);
    }
    cpu.ar[0] = bus.readWord(kBalanceStr);
    cpu.ar[1] = bus.readWord(kCredits);
    if (s(cmosItem(cpu, bus, subs, kItemCredits)) <= 1) cpu.ar[1] = bus.readWord(kCredit);
    appendString(cpu, bus);
}

/// 07564 (início) / 0757B (continuação): "n CREDITS TO START|CONTINUE" em [743C]
void priceString(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, bool cont) {
    cpu.r[7] = 0;
    bus.writeWord(bus.readWord(kPriceStr), cpu.r[7]);  // STI R7,($E563)
    cpu.r[2] = cont ? continuePrice(cpu, bus, subs) : startPrice(cpu, bus, subs);
    cpu.r[7] = 1;
    number(cpu, bus, subs, cpu.r[2], kNumBuf1);
    append(cpu, bus, kPriceStr, kNumBuf1);
    append(cpu, bus, kPriceStr, kSpace);
    cpu.ar[0] = bus.readWord(kPriceStr);
    cpu.ar[1] = bus.readWord(kCredits);
    const uint32_t n = cont ? continuePrice(cpu, bus, subs) : startPrice(cpu, bus, subs);
    if (n == 1) cpu.ar[1] = bus.readWord(kCredit);
    appendString(cpu, bus);
    append(cpu, bus, kPriceStr, cont ? kToContinue : kToStart);
}

/// 07544  preço piscando (±45): aceso → "TO START"; apagado → "TO CONTINUE" se o preço for outro
void priceBlink(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    if (blink(cpu, bus, 45)) {
        priceString(cpu, bus, subs, false);
    } else {
        cpu.r[0] = startPrice(cpu, bus, subs);
        cpu.r[2] = continuePrice(cpu, bus, subs);
        if (cpu.r[2] == cpu.r[0]) return;
        priceString(cpu, bus, subs, true);
    }
    // 07558
    cpu.r[2] = doubleToTms(256.0);
    centered(cpu, bus, subs, bus.readWord(kPriceStr));
    subs.call(0x7450, cpu, bus);
}

/// 0762E  aviso "MOTION OFF"
void motionNotice(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    if (serviceBit(bus)) return;
    if (item34IsOne(cpu, bus, subs)) return;
    cpu.r[2] = doubleToTms(256.0);
    cpu.r[3] = doubleToTms(305.0);
    centered(cpu, bus, subs, bus.readWord(kNoticePtr));
}

} // namespace

bool serviceBit(MemoryBus& bus) {
    return ((bus.readWord(kInputPort) >> 16) & 0x40u) != 0;
}

bool item34IsOne(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    return cmosItem(cpu, bus, subs, kItemNotice) == 1;
}

void priceRecord(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 073A0  item 0x17 != 1 → registro personalizado [742D]
    if (cmosItem(cpu, bus, subs, kItemCustom) != 1) {
        cpu.ar[0] = bus.readWord(kCustomPrices);
        return;
    }
    // 073A6  [B080] + (item 0 & 0x7F) · 11, limitado a [B081]
    cpu.ar[0] = cmosItem(cpu, bus, subs, kItemCoinage);
    cpu.ar[0] = (cpu.ar[0] & 0x7Fu) * 0xB + bus.readWord(kPriceTable);
    if (!(s(cpu.ar[0]) < s(bus.readWord(kPriceTableEnd)))) cpu.ar[0] = bus.readWord(kPriceTable);
}

void coinDipPrice(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 073B7  DIPs = ~([0x992000] >> 24) com os 8 bits invertidos (LSH −1 / ROLC); DP = 0
    const uint32_t dips = ~(bus.readWord(kInputPort) >> 24);
    cpu.dp = 0;
    uint32_t rev = 0;
    for (int i = 0; i < 8; ++i) rev = (rev << 1) | ((dips >> i) & 1u);
    // 073C5  registro [B083] + (moeda & 0x1F)·4 + (país & 0x60) >> 5, limitado a [B085]
    cpu.ar[2] = bus.readWord(0xB083) + (rev & 0x1Fu) * 4 + ((rev & 0x60u) >> 5);
    if (s(cpu.ar[2]) > s(bus.readWord(0xB085))) cpu.ar[2] = bus.readWord(0xB083);
    // 073CF  R0 = R2 = índice do registro em [B080] (11 palavras cada); AR2 = [AR2]
    cpu.r[0] = bus.readWord(cpu.ar[2]) - bus.readWord(kPriceTable);
    cpu.r[1] = 0xB;
    subs.call(0xA531, cpu, bus);
    cpu.r[2] = cpu.r[0];
    cpu.ar[2] = bus.readWord(cpu.ar[2]);
}

void appendString(Tms32031State& cpu, MemoryBus& bus) {
    uint32_t dst = cpu.ar[0], src = cpu.ar[1];
    // 077F1  acha o terminador do destino
    int32_t dshift = 0;
    for (;;) {
        if (dshift == -32) {
            dshift = 0;
            ++dst;
        }
        const uint32_t ch = (bus.readWord(dst) >> static_cast<uint32_t>(-dshift)) & 0xFFu;
        dshift -= 8;
        if (ch == 0) break;
    }
    dshift += 8;
    // 077FD  copia a origem byte a byte, inclusive o terminador
    int32_t sshift = 0;
    for (;;) {
        if (sshift == -32) {
            sshift = 0;
            ++src;
        }
        const uint32_t ch = (bus.readWord(src) >> static_cast<uint32_t>(-sshift)) & 0xFFu;
        sshift -= 8;
        bus.writeWord(dst, bus.readWord(dst) | (ch << static_cast<uint32_t>(-dshift)));
        dshift -= 8;
        if (dshift == -32) {
            dshift = 0;
            ++dst;
            bus.writeWord(dst, 0);
        }
        if (ch == 0) break;
    }
}

/// 075E4  "n CREDITS TO CONTINUE" em (256, 200)
void continuePriceLine(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    priceString(cpu, bus, subs, true);
    cpu.r[2] = doubleToTms(256.0);
    cpu.r[3] = doubleToTms(200.0);
    centered(cpu, bus, subs, bus.readWord(kPriceStr));
}

/// 075AF  linhas de crédito da tela de continue (nada no jogo grátis): preço e, em y = 237, o saldo
///        ou "INSERT COINS" piscando (±30, `0x75CC`). Preserva R4.
void continueCredits(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r4 = cpu.r[4];
    if (cmosItem(cpu, bus, subs, kItemFreePlay) != 1) {
        continuePriceLine(cpu, bus, subs);
        cpu.r[1] = cmosItem(cpu, bus, subs, kItemCredits);
        cpu.r[0] = cmosItem(cpu, bus, subs, kItemCoins);
        cpu.r[1] += cpu.r[0];
        cpu.r[2] = doubleToTms(256.0);
        cpu.r[3] = doubleToTms(237.0);
        if (cpu.r[1] == 0) {
            if (blink(cpu, bus, 30)) centered(cpu, bus, subs, bus.readWord(kInsertCoins));
        } else {
            balanceString(cpu, bus, subs);
            cpu.r[2] = doubleToTms(256.0);
            cpu.r[3] = doubleToTms(237.0);
            centered(cpu, bus, subs, bus.readWord(kBalanceStr));
        }
    }
    cpu.r[4] = r4;
}

void creditsDisplay(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    subs.call(0x7594, cpu, bus);
    subs.call(0x762E, cpu, bus);
    cpu.r[0] = bus.readWord(kHide);
    if (cpu.r[0] != 0) return;
    // 07462  jogo grátis
    cpu.r[0] = cmosItem(cpu, bus, subs, kItemFreePlay);
    cpu.r[3] = doubleToTms(360.0);
    if (cpu.r[0] == 1) {
        pressStart(cpu, bus, subs);
        return;
    }
    // 07467  atrato ímpar sem créditos/moedas, ou modo 7: só "INSERT COINS"
    if (bus.readWord(kAttractCase) & 1u) {
        cpu.r[2] = cmosItem(cpu, bus, subs, kItemCoins);
        cpu.r[2] += cmosItem(cpu, bus, subs, kItemCredits);
        if (cpu.r[2] == 0 || (bus.readWord(kGameMode) & 0xFu) == 7) {
            insertCoinsBlink(cpu, bus, subs);
            return;
        }
    }
    // 07476  y inicial = 345 − (17·linhas + 44)/2
    priceLinesHeight(cpu, bus, subs);
    cpu.r[3] = doubleToTms((tmsToDouble(cpu.r[3]) + 44.0) * -0.5 + 345.0);
    cpu.r[0] = cmosItem(cpu, bus, subs, kItemCredits);
    cpu.r[1] = startPrice(cpu, bus, subs);
    if (s(cpu.r[0]) >= s(cpu.r[1])) pressStart(cpu, bus, subs);
    else priceBlink(cpu, bus, subs);
    addR3(cpu, 22.0);
    // 07485  saldo ou "INSERT COINS"
    cpu.r[2] = cmosItem(cpu, bus, subs, kItemCoins);
    cpu.r[2] += cmosItem(cpu, bus, subs, kItemCredits);
    if (cpu.r[2] == 0) {
        insertCoinsText(cpu, bus, subs);
    } else {
        balanceString(cpu, bus, subs);
        cpu.r[2] = doubleToTms(256.0);
        centered(cpu, bus, subs, bus.readWord(kBalanceStr));
        subs.call(0x7450, cpu, bus);
    }
    // 0749B  linhas de preço
    addR3(cpu, 22.0);
    cpu.r[2] = doubleToTms(256.0);
    priceLines(cpu, bus, subs);
}

void registerCreditsDisplay(SubroutineDispatcher& d) {
    d.add(0x745E, creditsDisplay);
    d.add(0x762E, motionNotice);
    d.add(0x75AF, continueCredits);
    d.add(0x75E4, continuePriceLine);
    d.add(0x77E7, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { appendString(c, b); });
    d.add(0x73A0, priceRecord);
    d.add(0x73B3, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        priceRecord(c, b, s);
        c.ar[0] += 3;
    });
    // 0x4693 / 0x469D (com o flag Z) ficam em io/serial_printer.cpp
    d.add(0x73B7, coinDipPrice);
    // 0xC10042…0xC10081: R1 = campo; preservam R0, AR0 e AR2
    struct Field { uint32_t addr, word, shift; };
    static constexpr Field kFields[] = {{0xC10042, 1, 0}, {0xC1004E, 1, 8}, {0xC1005B, 1, 16},
                                        {0xC10068, 1, 24}, {0xC10075, 2, 0}, {0xC10081, 2, 8}};
    for (const Field& f : kFields) {
        d.add(f.addr, [&f](Tms32031State& c, MemoryBus& b, ISubroutines& s) { priceField(c, b, s, f.word, f.shift); });
    }
    // 0xC1000F…0xC10035: R2 = byte 0..3 de +0 (unidades por moeda das entradas 1..4); preservam R0, AR0, AR2
    struct Unit { uint32_t addr, shift; };
    static constexpr Unit kUnits[] = {{0xC1000F, 0}, {0xC1001B, 8}, {0xC10028, 16}, {0xC10035, 24}};
    for (const Unit& u : kUnits) {
        d.add(u.addr, [&u](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
            const uint32_t ar0 = c.ar[0], ar2 = c.ar[2], r0 = c.r[0];
            priceRecord(c, b, s);
            c.r[2] = (b.readWord(c.ar[0]) >> u.shift) & 0xFFu;
            c.r[0] = r0;
            c.ar[2] = ar2;
            c.ar[0] = ar0;
        });
    }
    // 0xC1008E / 0xC10096 / 0xC1009F / 0xC100A8: R0 = byte 0..3 de +0xA (contador mecânico); preservam AR0 e AR2
    for (uint32_t shift : {0u, 8u, 16u, 24u}) {
        static constexpr uint32_t kAddr[] = {0xC1008E, 0xC10096, 0xC1009F, 0xC100A8};
        d.add(kAddr[shift / 8], [shift](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
            const uint32_t ar0 = c.ar[0], ar2 = c.ar[2];
            priceRecord(c, b, s);
            c.r[0] = (b.readWord(c.ar[0] + 0xA) >> shift) & 0xFFu;
            c.ar[2] = ar2;
            c.ar[0] = ar0;
        });
    }
}

} // namespace rea::core::game
