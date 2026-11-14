/**
 * @file link_messages.cpp
 * @brief Tradução das mensagens do link (`0x793A`–`0x7C29`, parcial).
 */
#include "core/link/link_messages.hpp"

#include <cmath>

#include "core/link/link_car_sync.hpp"
#include "core/link/link_packet.hpp"
#include "core/link/link_routines.hpp"
#include "core/tms_float.hpp"

namespace rea::core::link {

using namespace msg_vars;

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// `LSH 8 / LSH −24`: byte (bits 16–23) sem sinal.
inline uint32_t ubyte(uint32_t w) { return (w << 8) >> 24; }
/// `LSH 8 / ASH −24`: byte (bits 16–23) com sinal.
inline uint32_t sbyte(uint32_t w) { return static_cast<uint32_t>(static_cast<int32_t>(w << 8) >> 24); }

/// Constante curta do TMS (16 bits: expoente 4, sinal 1, fração 11) → double.
double tmsShortToDouble(uint16_t raw) {
    const auto exponent = static_cast<int8_t>(static_cast<int8_t>(raw >> 8) >> 4);  // 4 bits com sinal
    if (exponent == -8) return 0.0;
    const bool negative = (raw >> 11) & 1u;
    const double frac = static_cast<double>(raw & 0x7FFu) / 2048.0;
    return std::ldexp(negative ? (-2.0 + frac) : (1.0 + frac), exponent);
}

/// Contexto dos tratadores: CPU, barramento e leitura do pacote recebido (AR2).
struct Ctx {
    Tms32031State& cpu;  ///< Registradores da CPU TMS320C31.
    MemoryBus& bus;  ///< Barramento de memória simulado.
    /// Lê com endereçamento direto (página DP, deslocamento @p off).
    uint32_t rd(uint32_t off) { return bus.readWord(cpu.direct(off)); }
    /// Grava @p v com endereçamento direto (página DP, deslocamento @p off).
    void wr(uint32_t off, uint32_t v) { bus.writeWord(cpu.direct(off), v); }
    /// `*AR2++`
    uint32_t next() { return bus.readWord(cpu.ar[2]++); }
};

/// Padrão `LDI $tipo,AR2 / BR $79AF` (mensagem de 1 palavra).
void sendType(Tms32031State& cpu, MemoryBus& bus, uint32_t type) {
    cpu.ar[2] = type;
    appendWord(cpu, bus);
}

/// Padrão `PUSH AR2 / LDI $tipo,AR2 / CALL $79AF / POP AR2`.
void sendTypeKeepAr2(Tms32031State& cpu, MemoryBus& bus, uint32_t type) {
    const uint32_t ar2 = cpu.ar[2];
    sendType(cpu, bus, type);
    cpu.ar[2] = ar2;
}

/// `[C8D7]` = `[C8D8]` = −1 (nenhum percurso escolhido).
void setMinusOne(Ctx& c) {
    c.cpu.r[0] = 0xFFFFFFFFu;
    c.wr(kVarC8D7, c.cpu.r[0]);
    c.wr(kVarC8D8, c.cpu.r[0]);
}

} // namespace

void parseReceived(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, const HandlerQuery& translated) {
    Ctx c{cpu, bus};
    // 0794C  Linking desligada, desconectado ou nada recebido: RETS
    cpu.r[0] = c.rd(link_vars::kDipShadow);
    if (cpu.r[0] & 0x0004u) return;
    cpu.r[0] = c.rd(link_vars::kConnected);
    if (cpu.r[0] == 0) return;
    cpu.r[0] = c.rd(link_vars::kRxPairs);
    if (cpu.r[0] == 0) return;
    // 07953  AR2 = início; AR6 = início + 2·([C8D4] + 1)
    cpu.ar[2] = c.rd(link_vars::kRxBufferPtr);
    cpu.ar[6] = c.rd(link_vars::kRxPairs);
    cpu.ar[6] += 1;
    cpu.ar[6] <<= 1;
    cpu.ar[6] += cpu.ar[2];
    // 07961  enquanto AR2 < AR6
    while (s(cpu.ar[2]) < s(cpu.ar[6])) {
        // 07959  AR0 = tipo; ≥ 0x21 encerra
        cpu.ar[0] = ubyte(c.next());
        if (s(cpu.ar[0]) >= static_cast<int32_t>(kHandlerCount)) break;
        // 0795E  R0 = tabela[tipo]; CALLU R0
        cpu.ar[0] += c.rd(kHandlerTablePtr);
        cpu.r[0] = bus.readWord(cpu.ar[0]);
        if (translated && !translated(cpu.r[0])) break;  // port: não ler payload como tipos
        subs.call(cpu.r[0], cpu, bus);
    }
    // 07963  [C8D4] = 0
    cpu.r[0] = 0;
    c.wr(link_vars::kRxPairs, cpu.r[0]);
}

void resetLinkState(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    // 0793A  [E596] = [E592]
    cpu.r[0] = c.rd(kVarE592);
    c.wr(kVarE596, cpu.r[0]);
    // 0793C  zera as variáveis
    cpu.r[0] = 0;
    for (uint32_t v : {kLocalC8DA, kPeerC8DA, kPeerMode, kRaceFlag, kVarE594, kVarC8DD, kVarE592, kVarE404})
        c.wr(v, cpu.r[0]);
    // 07945  RPTS 9: zera 10 palavras de [0x508E]
    cpu.ar[0] = c.rd(kSlotTablePtr);
    for (int i = 0; i < 10; ++i) bus.writeWord(cpu.ar[0]++, cpu.r[0]);
    cpu.rc = 0xFFFFFFFFu;
    // 07948  [C8D7] = [C8D8] = −1
    setMinusOne(c);
}

void handleStatus(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus};
    // 079E7  PUSH R0, R1, R2; R2 = 0xFF0000
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2];
    cpu.r[2] = 0x00FFu << 16;
    // 079EC  [C8D6] = 4 bytes, LSB primeiro
    cpu.r[0] = (c.next() & cpu.r[2]) >> 16;
    cpu.r[1] = (c.next() & cpu.r[2]) >> 8;
    cpu.r[0] |= cpu.r[1];
    cpu.r[1] = c.next() & cpu.r[2];
    cpu.r[0] |= cpu.r[1];
    cpu.r[1] = (c.next() & cpu.r[2]) << 8;
    cpu.r[0] |= cpu.r[1];
    c.wr(kPeerMode, cpu.r[0]);
    // 079F7  [C8D9] = 2 bytes
    cpu.r[0] = (c.next() & cpu.r[2]) >> 16;
    cpu.r[1] = ((c.next() & cpu.r[2]) << 8) >> 16;
    cpu.r[0] |= cpu.r[1];
    c.wr(kPeerC8DA, cpu.r[0]);
    // 079FE  [C8E5], [C8E2]
    cpu.r[0] = (c.next() & cpu.r[2]) >> 16;
    c.wr(kPeerC8E4, cpu.r[0]);
    cpu.r[0] = (c.next() & cpu.r[2]) >> 16;
    c.wr(kPeerE607, cpu.r[0]);
    // 07A04  [C8DC] = [C8DB]; [C8DB] = byte com sinal
    cpu.r[0] = static_cast<uint32_t>(static_cast<int32_t>((c.next() & cpu.r[2]) << 8) >> 24);
    cpu.r[1] = c.rd(kPeerC8E8);
    c.wr(kPeerC8E8Prev, cpu.r[1]);
    c.wr(kPeerC8E8, cpu.r[0]);
    // 07A0A  [C8D5] = FLOAT(byte) × 0,009998 (constante curta 0x923D; produto exato)
    cpu.r[0] = (c.next() & cpu.r[2]) >> 16;
    cpu.r[0] = doubleToTms(static_cast<double>(s(cpu.r[0])) * tmsShortToDouble(0x923D));
    c.wr(kPeerE639, cpu.r[0]);

    // 07A0F  outro gabinete no modo 2
    cpu.r[0] = c.rd(kPeerMode) & 0x000Fu;
    if (cpu.r[0] == 2) {
        cpu.r[0] = 0;
        c.wr(kRaceFlag, cpu.r[0]);
        setMinusOne(c);
    } else {
        // 07A19  bit 0x800: dele ligado e o nosso desligado → [E604] = 0
        cpu.r[0] = c.rd(kPeerC8DA);
        bool done = false;
        if (cpu.r[0] & 0x0800u) {
            cpu.r[1] = c.rd(kLocalC8DA);
            if (!(cpu.r[1] & 0x0800u)) {
                cpu.r[1] = 0;
                c.wr(kVarE604, cpu.r[1]);
                done = true;
            }
        }
        // 07A22  ambos no modo 4 e nosso & 0xF00 < dele & 0xF00 → CALL $AD00
        if (!done) {
            cpu.r[0] = c.rd(kPeerMode) & 0x000Fu;
            if (cpu.r[0] == 4) {
                cpu.r[0] = c.rd(kGameMode) & 0x000Fu;
                if (cpu.r[0] == 4) {
                    cpu.r[0] = c.rd(kPeerC8DA) & 0x0F00u;
                    cpu.r[1] = c.rd(kLocalC8DA) & 0x0F00u;
                    if (cpu.r[1] != cpu.r[0] && s(cpu.r[1]) < s(cpu.r[0])) subs.call(0xAD00, cpu, bus);
                }
            }
        }
    }
    // 07A32  POP R2, R1, R0
    cpu.r[2] = r2;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}

void handleType5(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus};
    // 07A6B  [C8DD] = 1
    cpu.r[0] = 1;
    c.wr(kVarC8DD, cpu.r[0]);
    cpu.r[0] = c.rd(kVarE594);
    if (cpu.r[0] == 0) {
        // 07A83  envia 0xA; [C8D7] = [C8D8] = −1
        sendTypeKeepAr2(cpu, bus, 0x0A);
        setMinusOne(c);
        return;
    }
    // 07A6F  [C8E3] = 1; auditoria 0xF8 + 1 (AR2 não é salvo)
    cpu.r[0] = 1;
    c.wr(kRaceFlag, cpu.r[0]);
    cpu.ar[2] = 0x00F8;
    subs.call(0x99DA, cpu, bus);
    // 07A73  [C8DD] = [E594] = 0; envia 9; [C8D7] = [C8D8] = −1
    cpu.r[0] = 0;
    c.wr(kVarC8DD, cpu.r[0]);
    c.wr(kVarE594, cpu.r[0]);
    sendTypeKeepAr2(cpu, bus, 0x09);
    setMinusOne(c);
    // 07A7A  CALL $91E0 com AR2 = 0xF3 (AR2 preservado)
    const uint32_t ar2 = cpu.ar[2];
    cpu.ar[2] = 0x00F3;
    subs.call(0x91E0, cpu, bus);
    cpu.ar[2] = ar2;
    // 07A7E  [E636] == 1 → mensagem 3
    cpu.r[0] = c.rd(0xE636);
    if (cpu.r[0] != 1) return;
    subs.call(0x7A98, cpu, bus);
}

namespace {

// --- Tratadores simples ------------------------------------------------------

/// Trecho traduzido da ROM em `0x7ABE`.
void handleType2(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2];
    cpu.r[2] = 0x00FFu << 16;
    cpu.r[0] = (c.next() & cpu.r[2]) >> 16;
    cpu.r[1] = (c.next() & cpu.r[2]) >> 8;
    cpu.r[0] |= cpu.r[1];
    c.wr(kVarE604, cpu.r[0]);
    cpu.r[0] = c.next() & cpu.r[2];  // sem deslocar: [C93E] = byte << 16
    c.wr(kVarC93E, cpu.r[0]);
    cpu.r[2] = r2;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}

/// Trecho traduzido da ROM em `0x7AA3`.
void handleType3(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    const uint32_t r1 = cpu.r[1];
    cpu.r[1] = sbyte(c.next());
    c.wr(kVarC8DF, cpu.r[1]);
    cpu.r[0] = ubyte(c.next());
    c.wr(kVarC8E0, cpu.r[0]);
    cpu.r[0] = ubyte(c.next());
    c.wr(kVarC8D7, cpu.r[0]);
    cpu.r[1] = r1;
}

/// Trecho traduzido da ROM em `0x7AB2`.
void handleType4(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    cpu.r[0] = ubyte(c.next());
    c.wr(kVarC8D8, cpu.r[0]);
}

/// Grava @p value em @p var com endereçamento direto.
void setVar(Tms32031State& cpu, MemoryBus& bus, uint32_t var, uint32_t value) {
    cpu.r[0] = value;
    bus.writeWord(cpu.direct(var), cpu.r[0]);
}

/// Trecho traduzido da ROM em `0x7BC3`.
void handleType0E(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    cpu.r[2] = static_cast<uint32_t>(-16);
    cpu.r[0] = c.next() >> 16;
    cpu.r[1] = c.rd(kVarE593);
    if (cpu.r[1] != 0) {
        cpu.ar[2] += 1;  // 07BCF
        return;
    }
    // 07BC8  byte ≤ 9: consulta [[0x508E] + byte] (o resultado não é usado)
    if (s(cpu.r[0]) > 9) return;
    cpu.r[0] += c.rd(kSlotTablePtr);
    cpu.ar[0] = cpu.r[0];
    cpu.r[0] = bus.readWord(cpu.ar[0]);
}

/// Trecho traduzido da ROM em `0x7BB0`.
void handleType0F(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus};
    cpu.r[0] = c.rd(kRaceFlag);
    if (cpu.r[0] == 0) { cpu.ar[2] += 3; return; }
    cpu.r[0] = c.rd(kVarE593);
    if (cpu.r[0] != 0) { cpu.ar[2] += 3; return; }
    // 07BB4  R4, R5 = bytes; AR6 = byte << 8; cria o processo [B129] (R2 = 0x213)
    const uint32_t ar6 = cpu.ar[6];
    cpu.r[2] = static_cast<uint32_t>(-16);
    cpu.r[3] = static_cast<uint32_t>(-8);
    cpu.r[4] = c.next() >> 16;
    cpu.r[5] = c.next() >> 16;
    cpu.ar[6] = c.next() >> 8;
    const uint32_t ar2 = cpu.ar[2];
    cpu.ar[2] = c.rd(kProcessPtr);
    cpu.r[2] = 0x0213;
    subs.call(0xA844, cpu, bus);
    cpu.ar[2] = ar2;
    cpu.ar[6] = ar6;
}

/// Trecho traduzido da ROM em `0x7BD1`.
void handleType19(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    for (uint32_t var : {kVarE8AD, kVarE8AE}) {
        cpu.r[2] = static_cast<uint32_t>(-16);
        cpu.r[3] = static_cast<uint32_t>(-8);
        cpu.r[0] = c.next() >> 16;
        cpu.r[1] = c.next() >> 8;
        cpu.r[0] += cpu.r[1];
        cpu.r[0] += c.next();  // 3º byte já está em << 16
        c.wr(var, cpu.r[0]);
    }
}

/// Trecho traduzido da ROM em `0x7BFD`.
void handleType1E(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    Ctx c{cpu, bus};
    cpu.r[0] = c.next();
    cpu.r[1] = c.rd(kRaceFlag);
    if (cpu.r[1] == 0) return;
    const uint32_t ar2 = cpu.ar[2];
    cpu.r[0] = ubyte(cpu.r[0]);
    cpu.ar[2] = cpu.r[0];
    subs.call(0x90E6, cpu, bus);
    cpu.ar[2] = ar2;
}

/// Trecho traduzido da ROM em `0x7C1C`.
void sendType20(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    cpu.ar[2] = c.rd(packet::kScratchPtr);
    cpu.r[0] = 0x20;
    bus.writeWord(cpu.ar[2]++, cpu.r[0]);
    cpu.r[0] = c.rd(kVar9A6C) + 1;
    c.wr(kVar9A6C, cpu.r[0]);
    // 07C22  Slave → 0xA0, Master → 0xB0 no nibble alto
    cpu.r[1] = (c.rd(link_vars::kDipShadow) & 0x0001u) ? 0xA0u : 0xB0u;
    // 07C26  OR R1,R0 com R0 = [C8CC] (lido para o TSTB), não o contador:
    //        o byte enviado é (DIPs | 0xA0/0xB0). Bug do original, preservado.
    cpu.r[0] = c.rd(link_vars::kDipShadow) | cpu.r[1];
    bus.writeWord(cpu.ar[2], cpu.r[0]);
    cpu.ar[2] -= 1;
    cpu.rc = 1;
    appendBlock(cpu, bus);
}

/// Trecho traduzido da ROM em `0x7C14`.
void handleType20(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    cpu.r[0] = sbyte(c.next());
    c.wr(kVar9A6B, cpu.r[0]);
    cpu.r[0] &= 0x000Fu;
    c.wr(kVar9A6C, cpu.r[0]);
    sendType20(cpu, bus);
}

// --- Emissores ------------------------------------------------------------------

/// Trecho traduzido da ROM em `0x7A8D`.
void sendType2(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    cpu.ar[2] = c.rd(packet::kScratchPtr);
    bus.writeWord(cpu.ar[2]++, 2);
    cpu.r[0] = c.rd(kVarE604);
    bus.writeWord(cpu.ar[2]++, cpu.r[0]);
    cpu.r[0] >>= 8;
    bus.writeWord(cpu.ar[2]++, cpu.r[0]);
    cpu.r[0] = c.rd(kVarC93E);
    bus.writeWord(cpu.ar[2], cpu.r[0]);
    cpu.ar[2] -= 3;
    cpu.rc = 3;
    appendBlock(cpu, bus);
}

/// Trecho traduzido da ROM em `0x7A98`.
void sendType3(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    cpu.ar[2] = c.rd(kScratch3Ptr);
    bus.writeWord(cpu.ar[2]++, 3);
    for (uint32_t v : {0xEA30u, 0xE636u}) {
        cpu.r[0] = c.rd(v);
        bus.writeWord(cpu.ar[2]++, cpu.r[0]);
    }
    cpu.r[0] = c.rd(0xE634);
    bus.writeWord(cpu.ar[2], cpu.r[0]);
    cpu.ar[2] -= 3;
    cpu.rc = 3;
    appendBlock(cpu, bus);
}

/// Trecho traduzido da ROM em `0x7AB7`.
void sendType4(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    cpu.ar[2] = c.rd(packet::kScratchPtr);
    bus.writeWord(cpu.ar[2]++, 4);
    cpu.r[0] = c.rd(0xE640);
    bus.writeWord(cpu.ar[2], cpu.r[0]);
    cpu.ar[2] -= 1;
    cpu.rc = 1;
    appendBlock(cpu, bus);
}

/// Trecho traduzido da ROM em `0x7A61`.
void sendType5(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    cpu.r[0] = 0;
    c.wr(kVarE593, cpu.r[0]);
    sendType(cpu, bus, 0x05);
    cpu.r[0] = 0;
    c.wr(kLocalC8DA, cpu.r[0]);
    c.wr(kRaceFlag, cpu.r[0]);
    cpu.r[0] = 1;
    c.wr(kVarE594, cpu.r[0]);
}

/// Trecho traduzido da ROM em `0x7BE0`.
void sendType19(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    cpu.ar[2] = c.rd(packet::kScratchPtr);
    cpu.r[2] = 0x19;
    bus.writeWord(cpu.ar[2]++, cpu.r[2]);
    auto put3 = [&]() {
        cpu.r[2] = bus.readWord(cpu.ar[0] + 0x1E);
        for (int i = 0; i < 3; ++i) {
            bus.writeWord(cpu.ar[2]++, cpu.r[2]);
            if (i < 2) cpu.r[2] >>= 8;
        }
    };
    cpu.ar[0] = c.rd(0xE46E);
    cpu.ar[0] = bus.readWord(cpu.ar[0] + 0x21);
    cpu.ar[0] = bus.readWord(cpu.ar[0] + 0x21);
    put3();
    cpu.ar[0] = c.rd(0xE470);
    put3();
    cpu.rc = 6;
    cpu.ar[2] = c.rd(packet::kScratchPtr);
    appendBlock(cpu, bus);
}

/// Trecho traduzido da ROM em `0x7C07`.
void sendType1E(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    cpu.ar[2] = c.rd(packet::kScratchPtr);
    cpu.r[0] = 0x1E;
    bus.writeWord(cpu.ar[2]++, cpu.r[0]);
    cpu.r[0] = c.rd(0xE916);
    bus.writeWord(cpu.ar[2], cpu.r[0]);
    cpu.ar[2] -= 1;
    cpu.rc = 1;
    appendBlock(cpu, bus);
}

/// `0x7A36`: `[C8DA] |= (1 << [E63A]) << 8`.
void markBit(Tms32031State& cpu, MemoryBus& bus) {
    Ctx c{cpu, bus};
    cpu.r[0] = c.rd(kVarE63A);
    cpu.r[1] = tmsLsh(1, cpu.r[0]);
    cpu.r[1] <<= 8;
    cpu.r[1] |= c.rd(kLocalC8DA);
    c.wr(kLocalC8DA, cpu.r[1]);
}

} // namespace

void registerMessageRoutines(SubroutineDispatcher& d) {
    using R = SubroutineDispatcher::Routine;
    auto simple = [](void (*fn)(Tms32031State&, MemoryBus&)) -> R {
        return [fn](Tms32031State& c, MemoryBus& b, ISubroutines&) { fn(c, b); };
    };
    auto set = [](uint32_t var, uint32_t value) -> R {
        return [var, value](Tms32031State& c, MemoryBus& b, ISubroutines&) { setVar(c, b, var, value); };
    };
    auto send = [](uint32_t type) -> R {
        return [type](Tms32031State& c, MemoryBus& b, ISubroutines&) { sendType(c, b, type); };
    };
    auto sendKeep = [](uint32_t type) -> R {
        return [type](Tms32031State& c, MemoryBus& b, ISubroutines&) { sendTypeKeepAr2(c, b, type); };
    };

    d.add(0x794C, [&d](Tms32031State& c, MemoryBus& b, ISubroutines& subs) {
        parseReceived(c, b, subs, [&d](uint32_t a) { return d.translated(a); });
    });
    d.add(0x793A, simple(resetLinkState));
    registerCarSync(d);  // tipos 7, 0xC, 0x12, 0x15 e emissores 0x7ACF/0x7AD7/0x7ADB
    // Tratadores
    d.add(0x79BF, [](Tms32031State&, MemoryBus&, ISubroutines&) {});  // tipo 0: RETS
    d.add(0x79E7, handleStatus);
    d.add(0x7ABE, simple(handleType2));
    d.add(0x7AA3, simple(handleType3));
    d.add(0x7AB2, simple(handleType4));
    d.add(0x7A6B, handleType5);
    d.add(0x7A8A, set(kRaceFlag, 0));
    d.add(0x79DD, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {  // preserva R0
        const uint32_t r0 = c.r[0];
        setVar(c, b, kVarC8DE, 1);
        c.r[0] = r0;
    });
    d.add(0x79E2, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        const uint32_t r0 = c.r[0];
        setVar(c, b, kVarC8DE, 2);
        c.r[0] = r0;
    });
    d.add(0x7A5C, set(kVarC8DD, 0));
    d.add(0x7BC3, simple(handleType0E));
    d.add(0x7BB0, handleType0F);
    d.add(0x79C8, set(kVarE63B, 1));
    d.add(0x79CB, set(kVarE63B, 2));
    d.add(0x79CE, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        setVar(c, b, kVarE63B, 3);
        setVar(c, b, kVarC93E, 0);
    });
    d.add(0x7BD1, simple(handleType19));
    d.add(0x7BFA, set(kVarC8E1, 3));
    d.add(0x7BFD, handleType1E);
    d.add(0x7C0F, set(kVarE597, 1));
    d.add(0x7C14, simple(handleType20));
    // Emissores
    d.add(0x79C0, send(0x16));
    d.add(0x79C2, send(0x17));
    d.add(0x79C4, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        setVar(c, b, kVarC93E, 0);
        sendType(c, b, 0x18);
    });
    d.add(0x79D3, sendKeep(0x09));
    d.add(0x79D8, sendKeep(0x0A));
    d.add(0x7A5F, send(0x0B));
    d.add(0x7A88, send(0x08));
    d.add(0x7A61, simple(sendType5));
    d.add(0x7A8D, simple(sendType2));
    d.add(0x7A98, simple(sendType3));
    d.add(0x7AB7, simple(sendType4));
    d.add(0x7BE0, simple(sendType19));
    d.add(0x7BF6, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        setVar(c, b, kVarE595, 3);
        sendType(c, b, 0x1D);
    });
    d.add(0x7C07, simple(sendType1E));
    d.add(0x7C12, send(0x1F));
    d.add(0x7C1C, simple(sendType20));
    d.add(0x7A36, simple(markBit));
    d.add(0x7A3D, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.r[0] = 0x0800u | b.readWord(c.direct(kLocalC8DA));
        b.writeWord(c.direct(kLocalC8DA), c.r[0]);
    });
}

} // namespace rea::core::link
