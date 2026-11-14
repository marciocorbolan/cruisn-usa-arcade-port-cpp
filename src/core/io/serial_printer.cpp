/**
 * @file serial_printer.cpp
 * @brief Tradução de `0x4689`–`0x4692`, `0x4834`–`0x4866`, `0x48D2`–`0x490A`, `0x4936`–`0x4953`, `0x49B8`–`0x4A64`
 *        e `0x77D1`–`0x77E6`.
 */
#include "core/io/serial_printer.hpp"

#include "core/tms_float.hpp"

#include <utility>

namespace rea::core::io {

namespace {

constexpr uint32_t kPort      = 0x995000;  ///< Latch serial das saídas, também usado pela impressora.
constexpr uint32_t kItemPrint = 0x34; ///< ajuste da CMOS: impressora ligada
constexpr uint32_t kLow       = 0xFFFFFF00u; ///< `LDI -$0100`
constexpr uint32_t kStrobe    = 0xFFFFFB00u; ///< `LDI -$0500`

/// Liga ou desliga o bit C (carry) de ST, que a rotina original usa como retorno de sucesso ou falha.
void setCarry(Tms32031State& c, bool on) {
    if (on) c.st |= Tms32031State::kStCarry;
    else c.st &= ~Tms32031State::kStCarry;
}

/// 04689  espera (RPTS 100) e, no modo 1, alimenta o watchdog. Preserva R0.
void delay(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    const uint32_t r0 = c.r[0];
    if ((b.readWord(0xC8C5) & 0xFu) == 1) s.call(0x5029, c, b);
    c.r[0] = r0;
    c.rc = 0xFFFFFFFFu;
}

/// Escreve @p v na porta serial e espera (`delay`).
void put(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t v) {
    b.writeWord(kPort, v);
    delay(c, b, s);
}

/// Fase de endereço: `−(0xFF − n)`, `−(0x8FF − n)`, `−(0xFF − n)`.
void selectAddress(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t n) {
    c.ar[3] = kPort;
    c.r[0] = 0xFFFFFF00u | n;
    c.r[1] = 0xFFFFF700u | n;
    put(c, b, s, c.r[0]);
    put(c, b, s, c.r[1]);
    put(c, b, s, c.r[0]);
}

/// Fase de dado com leitura no meio (endereços 8 e 10).
uint32_t readData(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.r[0] = kLow;
    c.r[1] = kStrobe;
    put(c, b, s, c.r[0]);
    put(c, b, s, c.r[1]);
    const uint32_t v = b.readWord(kPort);
    put(c, b, s, c.r[0]);
    return v;
}

/// 049B8  estado (endereço 10): carry = bit 15 da leitura (ocupado/sem dado).
void status(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    selectAddress(c, b, s, 0x0A);
    const uint32_t v = readData(c, b, s);
    c.r[1] = v << 17;
    setCarry(c, (v >> 15) & 1u);
}

/// 049CE  recebe um byte em R1 (carry = 1) se o estado indicou dado duas vezes seguidas.
void receive(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    status(c, b, s);
    if (!c.carry()) return;
    status(c, b, s);
    if (!c.carry()) return;
    selectAddress(c, b, s, 0x08);
    c.r[1] = (readData(c, b, s) >> 8) & 0xFFu;
    setCarry(c, true);
}

/// 04A41  dígito hexadecimal ASCII em R1 → valor (carry = 1) ou carry = 0.
void hexDigit(Tms32031State& c, MemoryBus&, ISubroutines&) {
    int32_t v = static_cast<int32_t>(c.r[1]);
    bool ok = false;
    if (v < 0x47) {
        v -= 0x30;
        if (v >= 0) {
            if (v <= 9) ok = true;
            else if (v >= 0x11) { v -= 7; ok = true; }
        }
    }
    c.r[1] = static_cast<uint32_t>(v);
    setCarry(c, ok);
}

/// 04A2E  lê "$hh" → R1 (carry = 1).
void readHexByte(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    receive(c, b, s);
    if (!c.carry() || c.r[1] != 0x24) return setCarry(c, false);
    receive(c, b, s);
    if (!c.carry()) return setCarry(c, false);
    hexDigit(c, b, s);
    if (!c.carry()) return setCarry(c, false);
    c.r[2] = c.r[1] << 4;
    receive(c, b, s);
    if (!c.carry()) return setCarry(c, false);
    hexDigit(c, b, s);
    if (!c.carry()) return setCarry(c, false);
    c.r[1] += c.r[2];
    setCarry(c, true);
}

/// Grava @p value no item @p item da CMOS (`0x99F4`).
void setCmos(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t item, uint32_t value) {
    c.r[2] = value;
    c.ar[2] = item;
    s.call(0x99F4, c, b);
}

/// 049EA  trata os comandos recebidos até faltar dado (carry = 0 na saída).
void commands(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    for (;;) {
        receive(c, b, s);
        if (!c.carry()) return;
        const uint32_t cmd = c.r[1];
        if (cmd == 'G') {
            setCmos(c, b, s, 0x80, 1);
            setCmos(c, b, s, kItemPrint, 0);
        } else if (cmd == 'V') {
            readHexByte(c, b, s); // o original usa R1 mesmo sem carry
            if ((c.r[1] & 6u) != 6u) {
                c.r[1] = b.readWord(0xEB52);
                if (static_cast<int32_t>(c.r[1]) < 2) c.r[1] = 2;
                b.writeWord(0xEB52, c.r[1]);
            }
        } else if (cmd == 'W' || cmd == 'S' || cmd == 'Q') {
            // 04A0C  mensagem na tela por 60 quadros e desliga a impressora
            c.ar[2] = b.readWord(cmd == 'W' ? 0xC7D4 : cmd == 'S' ? 0xC7D5 : 0xC7D6);
            c.r[2] = doubleToTms(256.0);
            c.r[3] = doubleToTms(110.0);
            c.rc = 0x3C;
            s.call(0x7651, c, b);
            b.writeWord(c.ar[0] + 8, b.readWord(c.ar[0] + 8) | 0x100u);
            b.writeWord(c.ar[1] + 8, b.readWord(c.ar[1] + 8) | 0x100u);
            s.call(0x76C9, c, b);
            setCmos(c, b, s, kItemPrint, 0);
            return;
        } else if (cmd == 'X' || cmd == 'Y' || cmd == 'Z') {
            readHexByte(c, b, s);
            if (!c.carry()) return;
            b.writeWord(cmd == 'X' ? 0xEBB8 : cmd == 'Y' ? 0xEBB9 : 0xEBBA, c.r[1]);
        }
    }
}

/// 04A4E  espera a resposta: até 1000 tentativas; sem resposta desliga a impressora.
void awaitReply(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.st &= ~Tms32031State::kStGie;
    for (c.r[6] = 1000;;) {
        status(c, b, s);
        if (!c.carry()) break;
        b.writeWord(0xEB50, 300);
        commands(c, b, s);
        if (!c.carry()) break;
        if (--c.r[6] == 0) {
            s.call(0x4C33, c, b);
            setCmos(c, b, s, kItemPrint, 0);
            return;
        }
    }
    s.call(0x4C33, c, b);
}

/// 048D2  envia o texto empacotado AR2 (4 caracteres por palavra, 1º no byte baixo). Preserva R0–R7.
void sendText(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    uint32_t saved[8];
    for (int i = 0; i < 8; ++i) saved[i] = c.r[i];
    c.st &= ~Tms32031State::kStGie;
    selectAddress(c, b, s, 0x09);
    int32_t shift = 0;
    for (;;) {
        if (shift == -0x20) {
            shift = 0;
            ++c.ar[2];
        }
        const uint32_t ch = (b.readWord(c.ar[2]) >> static_cast<uint32_t>(-shift)) & 0xFFu;
        shift -= 8;
        if (ch == 0) break;
        put(c, b, s, kLow | ch);
        put(c, b, s, kStrobe | ch);
        put(c, b, s, kLow | ch);
    }
    s.call(0x4C33, c, b);
    for (int i = 0; i < 8; ++i) c.r[i] = saved[i];
}

/// 077D1  copia o texto empacotado AR0 → AR1 (palavras inteiras, até a que contém o 0). Preserva R0, AR0–AR2.
void copyText(Tms32031State& c, MemoryBus& b, ISubroutines&) {
    uint32_t src = c.ar[0], dst = c.ar[1];
    int32_t shift = 0;
    for (;;) {
        if (shift == -0x20) {
            shift = 0;
            ++src;
            ++dst;
        }
        const uint32_t w = b.readWord(src);
        b.writeWord(dst, w);
        const uint32_t ch = (w >> static_cast<uint32_t>(-shift)) & 0xFFu;
        shift -= 8;
        if (ch == 0) break;
    }
}

/// Uma linha: 0 em decimal em `[p]` (`0xA762`); copia `[p+1]` → `[p+2]` (`0x77D1`); anexa `[p+4]` a `[p+3]`
/// e `[p+6]` a `[p+5]` (`0x77E7`, destino AR0); envia o texto AR0 e trata a resposta.
void printLine(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t p) {
    c.r[2] = 0;
    c.ar[2] = b.readWord(p);
    s.call(0xA762, c, b);
    c.ar[0] = b.readWord(p + 1); c.ar[1] = b.readWord(p + 2);
    copyText(c, b, s);
    c.ar[0] = b.readWord(p + 3); c.ar[1] = b.readWord(p + 4);
    s.call(0x77E7, c, b);
    c.ar[0] = b.readWord(p + 5); c.ar[1] = b.readWord(p + 6);
    s.call(0x77E7, c, b);
    c.ar[2] = c.ar[0];
    sendText(c, b, s);
    awaitReply(c, b, s);
}

/// 04834  imprime as 3 linhas de estatística se a impressora estiver ligada.
void printStats(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    s.call(0x4693, c, b);
    if (c.r[0] & 0x40u) return;
    s.call(0x469D, c, b);
    if (c.r[0] != 1) return;
    b.writeWord(0xEBB4, 0);
    printLine(c, b, s, 0xC7B7);
    printLine(c, b, s, 0xC7BE);
    printLine(c, b, s, 0xC7C5);
}

/// 04936  reinicia o modem: desliga o GIE, pulsa a porta (−0xF2/−0x8F2/−0xF2, depois
///        −0x100/−0x500/−0x100, cada um com `0x4689`), religa (`0x4C33`), envia `[C7CE]` (`0x48D2`) e
///        espera a resposta (`0x4A4E`).
void modemReset(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.st &= ~Tms32031State::kStGie;
    c.ar[3] = kPort;
    for (const auto& [lo, hi] : {std::pair<uint32_t, uint32_t>{0xFFFFFF0Eu, 0xFFFFF70Eu}, {kLow, kStrobe}}) {
        c.r[0] = lo;
        c.r[1] = hi;
        for (uint32_t v : {lo, hi, lo}) {
            b.writeWord(c.ar[3], v);
            s.call(0x4689, c, b);
        }
    }
    s.call(0x4C33, c, b);
    c.ar[2] = b.readWord(0xC7CE);
    s.call(0x48D2, c, b);
    s.call(0x4A4E, c, b);
}

/// 049A7  desliga o GIE, `[EBB7]` = 0 (modem não conectado) e religa (`0x4C33`).
void modemOff(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.st &= ~Tms32031State::kStGie;
    c.r[0] = 0;
    b.writeWord(0xEBB7, 0);
    s.call(0x4C33, c, b);
}

/// 04693  R0 = `[0x992000]` >> 16; Z = bit 0x40 desligado (DIP do modem ligado). DP = 0.
void modemDip(Tms32031State& c, MemoryBus& b, ISubroutines&) {
    c.r[0] = b.readWord(0x992000) >> 16;
    c.dp = 0;
    c.setFlag(Tms32031State::kStZero, (c.r[0] & 0x40u) == 0);
}

/// 0469D  R0 = item 0x34 da CMOS; Z = (R0 == 1), C = (R0 < 1 sem sinal).
void modemEnabled(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.ar[2] = 0x34;
    s.call(0x99E9, c, b);
    c.setFlag(Tms32031State::kStZero, c.r[0] == 1);
    c.setFlag(Tms32031State::kStCarry, c.r[0] < 1);
}

/// 04530  inicialização do modem no RESET: `0x49A7`, item 0x34 = 1, item 0x80 = 0; sem o DIP do modem
///        (`0x4693` com Z = 0) volta. Com o DIP, segue o diálogo com o modem em `0x4539` (não traduzido).
void modemInit(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    s.call(0x49A7, c, b);
    c.r[2] = 1;
    c.ar[2] = 0x34;
    s.call(0x99F4, c, b);
    c.r[2] = 0;
    c.ar[2] = 0x80;
    s.call(0x99F4, c, b);
    s.call(0x4693, c, b);
    if (!c.zero()) return;  // RETSNE
    s.call(0x4539, c, b);
}

} // namespace

void registerSerialPrinter(SubroutineDispatcher& d) {
    d.add(0x4689, delay);
    d.add(0x4834, printStats);
    d.add(0x48D2, sendText);
    d.add(0x49B8, status);
    d.add(0x49CE, receive);
    d.add(0x49EA, commands);
    d.add(0x4A2E, readHexByte);
    d.add(0x4A41, hexDigit);
    d.add(0x4A4E, awaitReply);
    d.add(0x77D1, copyText);
    d.add(0x4530, modemInit);
    d.add(0x4693, modemDip);
    d.add(0x469D, modemEnabled);
    d.add(0x49A7, modemOff);
    d.add(0x4936, modemReset);
}

} // namespace rea::core::io
