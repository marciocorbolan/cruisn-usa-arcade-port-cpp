/**
 * @file object_query.cpp
 * @brief Tradução de `0x36B3`–`0x3737` e `0x9EDB`.
 */
#include "core/scene/object_query.hpp"

#include "core/scene/object_list.hpp"
#include "core/tms_float.hpp"

namespace rea::core::scene {

namespace {
/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
/// Liga ou desliga o bit C (carry) de ST, que a rotina original usa como retorno de sucesso ou falha.
inline void setCarry(Tms32031State& cpu, bool c) {
    cpu.st = c ? (cpu.st | Tms32031State::kStCarry) : (cpu.st & ~Tms32031State::kStCarry);
}
/// Lê o bit C (carry) de ST.
inline bool carry(const Tms32031State& cpu) { return (cpu.st & Tms32031State::kStCarry) != 0; }

/// Percorre a lista de objetos a partir de AR0 (ou da cabeça) até @p match aceitar um objeto; resultado em AR0/AR5 e no carry.
template <typename Match>
void findImpl(Tms32031State& cpu, MemoryBus& bus, bool from_head, bool head_keeps_last, Match match) {
    const uint32_t r0 = cpu.r[0], ar5 = cpu.ar[5];
    if (from_head) cpu.ar[0] = bus.readWord(cpu.direct(objlist::kHeadCellPtr));
    for (;;) {
        cpu.ar[5] = bus.readWord(cpu.ar[0]);
        cpu.r[0] = cpu.ar[5];
        if (cpu.r[0] == 0) {
            // fim: 0x36B3 deixa AR0 no último nó; as outras zeram AR0
            if (!head_keeps_last) cpu.ar[0] = 0;
            setCarry(cpu, true);
            break;
        }
        cpu.ar[0] = cpu.r[0];
        cpu.r[0] = bus.readWord(cpu.ar[0] + kTypeField);
        if (match(cpu.r[0])) {
            setCarry(cpu, false);
            break;
        }
    }
    cpu.ar[5] = ar5;
    cpu.r[0] = r0;
}

/// `0x3712`: todos com `tipo & 0xFF == R1` recebem `tipo |= R2`.
void orTypeEq(Tms32031State& cpu, MemoryBus& bus) {
    findTypeEq(cpu, bus, true);
    while (!carry(cpu)) {
        cpu.r[3] = bus.readWord(cpu.ar[0] + kTypeField) | cpu.r[2];
        bus.writeWord(cpu.ar[0] + kTypeField, cpu.r[3]);
        findTypeEq(cpu, bus, false);
    }
}

/// Trecho traduzido da ROM em `0x3702`.
void orTypeList(Tms32031State& cpu, MemoryBus& bus) {
    for (;;) {
        cpu.r[1] = bus.readWord(cpu.ar[3]++);
        if (s(cpu.r[1]) <= 0xFF) {
            if (cpu.r[1] == 0) return;
            orTypeEq(cpu, bus);
            continue;
        }
        cpu.r[0] = cpu.r[1] & 0xFFu;
        cpu.r[1] >>= 8;  // LSH −8 (lógico)
        do {
            orTypeEq(cpu, bus);
            cpu.r[1] += 1;
        } while (s(cpu.r[0] - cpu.r[1]) >= 0);
    }
}

/// Trecho traduzido da ROM em `0x371E`.
void addZByMask(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r3 = cpu.r[3];
    findTypeMask(cpu, bus, true);
    while (!carry(cpu)) {
        cpu.r[3] = doubleToTms(tmsToDouble(bus.readWord(cpu.ar[0] + 2)) + tmsToDouble(cpu.r[2]));
        bus.writeWord(cpu.ar[0] + 2, cpu.r[3]);
        findTypeMask(cpu, bus, false);
    }
    cpu.r[3] = r3;
}

/// Trecho traduzido da ROM em `0x372E`.
void call715FByMask(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    findTypeMask(cpu, bus, true);
    while (!carry(cpu)) {
        cpu.ar[2] = cpu.ar[0];
        subs.call(0x715F, cpu, bus);
        findTypeMask(cpu, bus, false);
    }
}
} // namespace

void findTypeEq(Tms32031State& cpu, MemoryBus& bus, bool from_head) {
    findImpl(cpu, bus, from_head, from_head, [&](uint32_t t) { return (t & 0xFFu) == cpu.r[1]; });
}

void findTypeMask(Tms32031State& cpu, MemoryBus& bus, bool from_head) {
    findImpl(cpu, bus, from_head, false, [&](uint32_t t) { return (t & cpu.r[1]) != 0; });
}

void findInTable9E7F(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t ar0 = cpu.ar[0];
    cpu.r[0] = 0xFFFFFFFFu;
    cpu.dp = 0;
    cpu.ar[0] = bus.readWord(0x9E7F);
    for (;;) {
        cpu.r[0] += 1;
        if (bus.readWord(cpu.ar[0]++) == cpu.ar[2]) {
            cpu.r[0] <<= 8;  // 09EE8
            setCarry(cpu, true);
            break;
        }
        if (s(cpu.r[0]) > 0x80) {
            cpu.r[0] = 0xFFFFFFFFu;
            setCarry(cpu, false);
            break;
        }
    }
    cpu.ar[0] = ar0;
}

void findNextType(Tms32031State& cpu, MemoryBus& bus) {
    // 0AE27  a partir do sucessor de AR0, procura +0xF == R1; preserva R0 e AR5
    const uint32_t r0 = cpu.r[0], ar5 = cpu.ar[5];
    cpu.ar[5] = bus.readWord(cpu.ar[0]);
    for (;;) {
        cpu.ar[0] = cpu.ar[5];
        if (cpu.ar[0] == 0) break;
        cpu.ar[5] = bus.readWord(cpu.ar[0]);
        if (bus.readWord(cpu.ar[0] + 0xF) == cpu.r[1]) break;
    }
    cpu.st &= ~Tms32031State::kStCarry;
    if (cpu.ar[0] == 0) cpu.st |= Tms32031State::kStCarry;
    cpu.ar[5] = ar5;
    cpu.r[0] = r0;
}

void registerObjectQuery(SubroutineDispatcher& d) {
    d.add(0xAE27, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { findNextType(c, b); });
    d.add(0x36B3, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { findTypeEq(c, b, true); });
    d.add(0x36C7, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { findTypeEq(c, b, false); });
    d.add(0x36DB, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { findTypeMask(c, b, true); });
    d.add(0x36EF, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { findTypeMask(c, b, false); });
    d.add(0x3712, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { orTypeEq(c, b); });
    d.add(0x3702, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { orTypeList(c, b); });
    d.add(0x371E, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { addZByMask(c, b); });
    d.add(0x372E, call715FByMask);
    d.add(0x9EDB, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { findInTable9E7F(c, b); });
}

} // namespace rea::core::scene
