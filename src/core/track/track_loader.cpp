/**
 * @file track_loader.cpp
 * @brief Tradução de `0x8C14`–`0x8E48`, `0xAA30`–`0xAB18`, `0x6754`–`0x677D`,
 *        `0xAF55`–`0xAF73` e `0x3966`–`0x396E`.
 */
#include "core/track/track_loader.hpp"

#include <initializer_list>
#include <vector>

#include "core/tms_float.hpp"

namespace rea::core::track {

namespace {

using namespace track_vars;

// ---------------------------------------------------------------------------------------------
// Carregadores por pista: sequências fixas de operações

/// Operação de um carregador de pista.
enum class Op : uint8_t {
    Lzw,      ///< AR2 = [a]; CALL 0xA3C2
    Pal,      ///< AR2 = [a]; CALL 0x9DF6
    PalAlt,   ///< AR2 = [a]; CALL 0x9DFE
    Tex,      ///< AR2 = [a]; CALL 0x9F2F
    Call,     ///< CALL a
    Set,      ///< [a] = v (inteiro)
    Sky,      ///< [E3FD] = FLOAT(v)
    Slot,     ///< R0 = a; R1 = v; CALL 0x9EC2
    Spawn,    ///< AR2 = [a]; R2 = 0x800; CALL 0xA844
    Segments, ///< CALL 0xAA30; [E482] = a << 8; AR0 = a; AR1 = v; CALL 0xAA66
};

/// Um passo do carregador: operação @p op com endereço @p a e valor @p v.
struct Step {
    Op op;  ///< Operação.
    uint32_t a;  ///< Endereço (ponteiro na ROM, rotina ou variável).
    int32_t v;  ///< Valor imediato.
};

/// Executa os passos de um carregador de pista em ordem, como a sequência fixa de `CALL`s da ROM.
void run(const std::vector<Step>& steps, Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    for (const Step& s : steps) {
        switch (s.op) {
        case Op::Lzw:
        case Op::Pal:
        case Op::PalAlt:
        case Op::Tex: {
            static constexpr uint32_t kTarget[] = {0xA3C2, 0x9DF6, 0x9DFE, 0x9F2F};
            cpu.ar[2] = bus.readWord(s.a);
            subs.call(kTarget[static_cast<int>(s.op)], cpu, bus);
            break;
        }
        case Op::Call: subs.call(s.a, cpu, bus); break;
        case Op::Set:
            cpu.r[0] = static_cast<uint32_t>(s.v);
            bus.writeWord(s.a, cpu.r[0]);
            break;
        case Op::Sky:
            cpu.r[0] = doubleToTms(static_cast<double>(s.v));
            bus.writeWord(0xE3FD, cpu.r[0]);
            break;
        case Op::Slot:
            cpu.r[0] = s.a;
            cpu.r[1] = static_cast<uint32_t>(s.v);
            subs.call(0x9EC2, cpu, bus);
            break;
        case Op::Spawn:
            cpu.ar[2] = bus.readWord(s.a);
            cpu.r[2] = 0x800;
            subs.call(0xA844, cpu, bus);
            break;
        case Op::Segments:
            subs.call(0xAA30, cpu, bus);
            cpu.ar[0] = s.a << 8;
            bus.writeWord(0xE482, cpu.ar[0]);
            cpu.ar[0] = s.a;
            cpu.ar[1] = static_cast<uint32_t>(s.v);
            subs.call(0xAA66, cpu, bus);
            break;
        }
    }
}

/// Apelido curto usado nas tabelas abaixo.
using O = Op;

/// Registra os carregadores de cada pista no despachante.
void registerLoaders(SubroutineDispatcher& d) {
    auto add = [&d](uint32_t addr, std::initializer_list<Step> steps) {
        d.add(addr, [v = std::vector<Step>(steps)](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
            run(v, c, b, s);
        });
    };
    // 08C14 / 08C19 / 08C67 / 08C6C / 08C70 / 08CEE: blocos comuns
    add(0x8C14, {{O::Lzw, 0xC650, 0}, {O::Lzw, 0xC651, 0}});
    add(0x8C19, {{O::Call, 0x8C67, 0}, {O::Lzw, 0xC652, 0}});
    add(0x8C67, {{O::Lzw, 0xC66E, 0}, {O::Pal, 0xC66F, 0}});
    // 08C1D–08C5E  cenários (LZW) das pistas, por `[[8C12] + [E481]]`
    add(0x8C1D, {{O::Call, 0x8C67, 0}, {O::Lzw, 0xC653, 0}, {O::Lzw, 0xC654, 0}});
    add(0x8C1E, {{O::Lzw, 0xC653, 0}, {O::Lzw, 0xC654, 0}});
    add(0x8C23, {{O::Call, 0x8C1E, 0}, {O::Call, 0x8CEE, 0}});
    add(0x8C26, {{O::Call, 0x8C1E, 0}, {O::Call, 0x8CEE, 0}, {O::Lzw, 0xC655, 0}});
    add(0x8C2B, {{O::Call, 0x8CEE, 0}, {O::Lzw, 0xC656, 0}});
    add(0x8C2F, {{O::Call, 0x8CEE, 0}, {O::Lzw, 0xC657, 0}, {O::Lzw, 0xC658, 0}});
    add(0x8C35, {{O::Call, 0x8CEE, 0}, {O::Lzw, 0xC659, 0}, {O::Lzw, 0xC65A, 0}});
    add(0x8C3B, {{O::Lzw, 0xC65B, 0}, {O::Lzw, 0xC65C, 0}});
    add(0x8C40, {{O::Lzw, 0xC65D, 0}, {O::Lzw, 0xC65E, 0}, {O::Lzw, 0xC65F, 0}});
    add(0x8C47, {{O::Lzw, 0xC660, 0}, {O::Lzw, 0xC661, 0}, {O::Lzw, 0xC662, 0}, {O::Lzw, 0xC663, 0}});
    add(0x8C50, {{O::Lzw, 0xC664, 0}, {O::Lzw, 0xC665, 0}, {O::Lzw, 0xC666, 0}});
    add(0x8C57, {{O::Lzw, 0xC667, 0}, {O::Lzw, 0xC668, 0}, {O::Lzw, 0xC669, 0}});
    add(0x8C5E, {{O::Lzw, 0xC66A, 0}, {O::Lzw, 0xC66B, 0}, {O::Lzw, 0xC66C, 0}, {O::Lzw, 0xC66D, 0}});
    d.add(0x8C6C, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.r[0] = b.readWord(0xC8C5) | 0x80u;
        b.writeWord(0xC8C5, c.r[0]);
    });
    d.add(0x8C70, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.r[0] = b.readWord(0xC8C5) & ~0x80u;
        b.writeWord(0xC8C5, c.r[0]);
    });
    add(0x8CEE, {{O::Pal, 0xC684, 0}, {O::Lzw, 0xC685, 0}});
    // 08C74  pista 0
    d.add(0x8C74, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        run({{O::Lzw, 0xC670, 0}, {O::Lzw, 0xC671, 0}, {O::Set, 0xE8E3, 1}, {O::Set, 0xE5F2, 0x3C},
             {O::Set, 0xE5F3, 0x64}, {O::Pal, 0xC672, 0}, {O::Tex, 0xC673, 0}, {O::Sky, 0, -15},
             {O::Call, 0x8C6C, 0}, {O::Call, 0xAA30, 0}},
            c, b, s);
        // 08C86  sem [E482]
        c.ar[0] = 1;
        c.ar[1] = 0x57;
        s.call(0xAA66, c, b);
    });
    add(0x8C8A, {{O::Lzw, 0xC674, 0}, {O::Call, 0x8C67, 0}, {O::Pal, 0xC675, 0}, {O::Pal, 0xC676, 0},
                 {O::Sky, 0, -15}, {O::Call, 0x8C6C, 0}, {O::Set, 0xE5F2, 0x78}, {O::Set, 0xE5F3, 0x78},
                 {O::Set, 0xE8E3, 1}, {O::Set, 0xE916, 2}, {O::PalAlt, 0xC677, 0}, {O::Pal, 0xC678, 0},
                 {O::Lzw, 0xC679, 0}, {O::Set, 0xE47F, 0}, {O::Segments, 0x5C, 0x9E}});
    add(0x8CAC, {{O::Call, 0x8C67, 0}, {O::Lzw, 0xC67A, 0}, {O::Lzw, 0xC67B, 0}, {O::Set, 0xE916, 3},
                 {O::Set, 0xE5F2, 0x3C}, {O::Set, 0xE5F3, 0x64}, {O::Set, 0xE47F, 0}, {O::Set, 0xE8E3, 0},
                 {O::Pal, 0xC67C, 0}, {O::Pal, 0xC67D, 0}, {O::Sky, 0, 55}, {O::Call, 0x8C70, 0},
                 {O::Segments, 0xA1, 0xCC}});
    add(0x8CCC, {{O::Lzw, 0xC67E, 0}, {O::Lzw, 0xC67F, 0}, {O::Pal, 0xC680, 0}, {O::Pal, 0xC681, 0},
                 {O::Lzw, 0xC682, 0}, {O::Pal, 0xC683, 0}, {O::Call, 0x8C70, 0}, {O::Set, 0xE916, 0},
                 {O::Set, 0xE47F, 1}, {O::Set, 0xE8E3, 1}, {O::Set, 0xE5F2, 0x78}, {O::Set, 0xE5F3, 0x78},
                 {O::Sky, 0, 50}, {O::Call, 0x8CEE, 0}, {O::Segments, 0xCE, 0x13C}});
    add(0x8CF3, {{O::Call, 0x8CEE, 0}, {O::Lzw, 0xC686, 0}, {O::Lzw, 0xC687, 0}, {O::Set, 0xE5F2, 0x3C},
                 {O::Set, 0xE5F3, 0x64}, {O::Pal, 0xC688, 0}, {O::Pal, 0xC689, 0}, {O::Call, 0x8C70, 0},
                 {O::Set, 0xE916, 5}, {O::Set, 0xE47F, 0}, {O::Set, 0xE5F7, 0}, {O::Set, 0xE8E3, 1},
                 {O::Sky, 0, 45}, {O::Segments, 0x13E, 0x19F}});
    add(0x8D12, {{O::Call, 0x8CEE, 0}, {O::Lzw, 0xC68A, 0}, {O::Call, 0x8C70, 0}, {O::Set, 0xE916, 2},
                 {O::Set, 0xE47F, 0}, {O::Set, 0xE8E3, 1}, {O::Sky, 0, 35}, {O::Pal, 0xC68B, 0},
                 {O::Segments, 0x1A2, 0x1FD}, {O::Set, 0xE5F2, 0x3C}, {O::Set, 0xE5F3, 0x64}});
    add(0x8D2C, {{O::Lzw, 0xC68C, 0}, {O::Lzw, 0xC68D, 0}, {O::Call, 0x8CEE, 0}, {O::Call, 0x8C70, 0},
                 {O::Set, 0xE916, 3}, {O::Set, 0xE47F, 1}, {O::Set, 0xE8E3, 1}, {O::Sky, 0, 55},
                 {O::Slot, 1, 1}, {O::Pal, 0xC68E, 0}, {O::Pal, 0xC68F, 0}, {O::Set, 0xE5F2, 0x3C},
                 {O::Set, 0xE5F3, 0x64}, {O::Segments, 0x203, 0x25B}});
    add(0x8D4C, {{O::Pal, 0xC690, 0}, {O::Call, 0x8C70, 0}, {O::Pal, 0xC691, 0}, {O::Call, 0x8CEE, 0},
                 {O::Pal, 0xC692, 0}, {O::Lzw, 0xC693, 0}, {O::Lzw, 0xC694, 0}, {O::Set, 0xE916, 1},
                 {O::Set, 0xE47F, 0}, {O::Set, 0xE8E3, 1}, {O::Sky, 0, 45}, {O::Set, 0xE5F2, 0x3C},
                 {O::Set, 0xE5F3, 0x64}, {O::Segments, 0x265, 0x2D0}});
    add(0x8D6C, {{O::Lzw, 0xC695, 0}, {O::Lzw, 0xC696, 0}, {O::Call, 0x8C70, 0}, {O::Pal, 0xC697, 0},
                 {O::Lzw, 0xC698, 0}, {O::Set, 0xE916, 3}, {O::Set, 0xE47F, 1}, {O::Set, 0xE8E3, 0},
                 {O::Sky, 0, 55}, {O::Pal, 0xC699, 0}, {O::Set, 0xE5F2, 0x4B}, {O::Set, 0xE5F3, 0x7D},
                 {O::Segments, 0x2D5, 0x338}});
    add(0x8D8B, {{O::Call, 0x8C70, 0}, {O::Set, 0xE916, 7}, {O::Set, 0xE47F, 0}, {O::Set, 0xE8E3, 1},
                 {O::Sky, 0, 45}, {O::Lzw, 0xC69A, 0}, {O::Lzw, 0xC69B, 0}, {O::Lzw, 0xC69C, 0},
                 {O::Set, 0xE5F2, 0x4B}, {O::Set, 0xE5F3, 0x7D}, {O::Pal, 0xC69D, 0}, {O::Slot, 0, 0xFB},
                 {O::Slot, 1, 0xFD}, {O::Segments, 0x33C, 0x39C}, {O::Spawn, 0xC69E, 0},
                 {O::Spawn, 0xC69F, 0}, {O::Spawn, 0xC6A0, 0}});
    add(0x8DB7, {{O::Lzw, 0xC6A1, 0}, {O::Lzw, 0xC6A2, 0}, {O::Call, 0x8C70, 0}, {O::Set, 0xE8E3, 1},
                 {O::Set, 0xE916, 2}, {O::Set, 0xE47F, 1}, {O::Sky, 0, 55}, {O::Pal, 0xC6A3, 0},
                 {O::Pal, 0xC6A4, 0}, {O::Set, 0xE5F2, 0x3C}, {O::Set, 0xE5F3, 0x64},
                 {O::Segments, 0x3A2, 0x406}});
    add(0x8DD4, {{O::Call, 0x8C70, 0}, {O::Lzw, 0xC6A5, 0}, {O::Slot, 1, 0xFD}, {O::Set, 0xE916, 4},
                 {O::Set, 0xE47F, 0}, {O::Set, 0xE8E3, 1}, {O::Sky, 0, 45}, {O::Pal, 0xC6A6, 0},
                 {O::Lzw, 0xC6A7, 0}, {O::Slot, 0, 0xFA}, {O::Lzw, 0xC6A8, 0}, {O::Set, 0xE5F2, 0x4B},
                 {O::Set, 0xE5F3, 0x7D}, {O::Segments, 0x409, 0x45D}, {O::Spawn, 0xC6A9, 0},
                 {O::Spawn, 0xC6AA, 0}, {O::Spawn, 0xC6AB, 0}});
    add(0x8E00, {{O::Call, 0x8C70, 0}, {O::Lzw, 0xC6AC, 0}, {O::Slot, 1, 0xFD}, {O::Lzw, 0xC6AD, 0},
                 {O::Slot, 0, 0xFA}, {O::Set, 0xE916, 5}, {O::Set, 0xE47F, 1}, {O::Set, 0xE8E3, 0},
                 {O::Set, 0xE5F2, 0x3C}, {O::Set, 0xE5F3, 0x64}, {O::Sky, 0, 85}, {O::Pal, 0xC6AE, 0},
                 {O::Lzw, 0xC6AF, 0}, {O::Segments, 0x464, 0x4CE}});
    add(0x8E23, {{O::Call, 0x8C70, 0}, {O::Lzw, 0xC6B0, 0}, {O::Slot, 0, 0xFA}, {O::Lzw, 0xC6B1, 0},
                 {O::Slot, 1, 0xFD}, {O::Set, 0xE916, 2}, {O::Set, 0xE47F, 0}, {O::Set, 0xE8E3, 0},
                 {O::Sky, 0, 45}, {O::Set, 0xE5F2, 0x3C}, {O::Set, 0xE5F3, 0x64}, {O::Pal, 0xC6B2, 0},
                 {O::Pal, 0xC6B3, 0}, {O::Lzw, 0xC6B4, 0}, {O::Lzw, 0xC6B5, 0},
                 {O::Segments, 0x4D5, 0x517}});
}

// ---------------------------------------------------------------------------------------------
// Nós da pista

/// 0AA48  insere a entrada AR3 (chave `[AR3+4]`) na lista `[E859]`, em ordem crescente.
void sortedInsert(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[0] = bus.readWord(cpu.ar[3] + 4);
    cpu.ar[0] = bus.readWord(0xAA47);
    for (;;) {
        cpu.ar[1] = bus.readWord(cpu.ar[0]);
        if (cpu.ar[1] == 0) break;
        if (static_cast<int32_t>(cpu.r[0]) < static_cast<int32_t>(bus.readWord(cpu.ar[1] + 4))) break;
        cpu.ar[0] = cpu.ar[1];
    }
    bus.writeWord(cpu.ar[3], cpu.ar[1]);
    bus.writeWord(cpu.ar[0], cpu.ar[3]);
}

/// 0AA54  copia os nós ordenados (4 palavras cada) para *AR3++. Preserva R0, AR0 e AR1.
void flushSorted(Tms32031State& cpu, MemoryBus& bus) {
    for (uint32_t e = bus.readWord(kSortHead); e != 0; e = bus.readWord(e))
        for (uint32_t i = 0; i < 4; ++i) bus.writeWord(cpu.ar[3]++, bus.readWord(e + 1 + i));
}

/// 0AA9D  converte o trecho AR4 (registro AR2) em nós. Preserva R4–R6, AR0–AR5 e AR7.
void buildSegment(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const auto saved = cpu;
    bus.writeWord(kCurSeg, cpu.ar[4]);
    subs.call(0xAA39, cpu, bus);
    subs.call(0xAA40, cpu, bus);
    cpu.ar[3] += 1;
    const uint32_t rec = cpu.ar[2];
    cpu.ar[7] = rec;
    cpu.r[5] = bus.readWord(rec);
    cpu.r[6] = (cpu.r[5] >> 7) & 1u;
    cpu.r[2] = bus.readWord(rec + 4);
    bus.writeWord(0xE85B, cpu.r[2]);
    cpu.ar[2] = bus.readWord(0x5A);
    subs.call(0x939D, cpu, bus);
    cpu.ar[5] = bus.readWord(rec + 5) + 1;
    cpu.r[4] = bus.readWord(cpu.ar[5]++) - 1;
    const bool offset = (cpu.r[5] & 8u) != 0;
    do {
        // 0AAB8  ponto: (palavra ignorada), x, y, z inteiros
        cpu.ar[2] = bus.readWord(cpu.ar[5]++);
        for (uint32_t i = 0; i < 3; ++i)
            bus.writeWord(cpu.ar[3] + i,
                          doubleToTms(static_cast<double>(static_cast<int32_t>(bus.readWord(cpu.ar[5]++)))));
        if (offset) {
            // 0AAC1  soma o deslocamento +7..+9 (uma palavra antes sem flags & 1)
            const uint32_t base = (cpu.r[5] & 1u) ? rec : rec - 1;
            for (uint32_t i = 0; i < 3; ++i)
                bus.writeWord(cpu.ar[3] + i, doubleToTms(tmsToDouble(bus.readWord(base + 7 + i)) +
                                                         tmsToDouble(bus.readWord(cpu.ar[3] + i))));
        }
        // 0AACF / 0AADF  gira e translada pela origem +1..+3
        cpu.r[2] = bus.readWord(0x5A);
        cpu.ar[2] = cpu.ar[3];
        cpu.r[3] = bus.readWord(0x5D);
        subs.call(0x941E, cpu, bus);
        cpu.ar[0] = bus.readWord(0x5D);
        for (uint32_t i = 0; i < 3; ++i) {
            cpu.r[1] = doubleToTms(tmsToDouble(bus.readWord(cpu.ar[0]++)) + tmsToDouble(bus.readWord(rec + 1 + i)));
            bus.writeWord(cpu.ar[3] + i, cpu.r[1]);
        }
        // 0AAEF  tipo do ponto
        cpu.r[2] = bus.readWord(cpu.ar[5]++);
        cpu.r[1] = bus.readWord(cpu.ar[5]++);
        cpu.r[0] = cpu.r[1] & 0xF00u;
        if (cpu.r[0] == 0xB00u) {
            cpu.r[3] = cpu.r[1];
            cpu.r[1] = bus.readWord(kCurSeg) << 8;
            cpu.r[0] = cpu.r[3] & 0xFFu;
            if (offset) cpu.r[0] = 0xFFu - cpu.r[0];
            cpu.r[0] |= cpu.r[1];
            bus.writeWord(cpu.ar[3] + 3, cpu.r[0]);
            bus.writeWord(cpu.ar[3], tmsFix(tmsToDouble(bus.readWord(cpu.ar[3]))));
            cpu.r[0] = (tmsFix(tmsToDouble(bus.readWord(cpu.ar[3] + 1))) & ~1u) | cpu.r[6];
            bus.writeWord(cpu.ar[3] + 1, cpu.r[0]);
            cpu.r[0] = tmsFix(tmsToDouble(bus.readWord(cpu.ar[3] + 2)));
            bus.writeWord(cpu.ar[3] + 2, cpu.r[0]);
            cpu.ar[3] -= 1;
            sortedInsert(cpu, bus);
            subs.call(0xAA40, cpu, bus);
            cpu.ar[3] += 1;
        }
        cpu.r[4] -= 1;
    } while (static_cast<int32_t>(cpu.r[4]) >= 0);
    // 0AB0D  restaura e copia os nós ordenados para o AR3 do chamador
    for (int i : {4, 5, 6}) cpu.r[i] = saved.r[i];
    for (int i : {0, 1, 2, 3, 4, 5, 7}) cpu.ar[i] = saved.ar[i];
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0], ar1 = cpu.ar[1];
    flushSorted(cpu, bus);
    cpu.r[0] = r0;
    cpu.ar[0] = ar0;
    cpu.ar[1] = ar1;
}

/// 0AA66  nós dos trechos AR0..AR1 (+3 à frente) em `[AA2F]`; fim = 4 × −1.
void buildNodes(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[1] = cpu.ar[1] + 2;
    bus.writeWord(kLastSeg, cpu.r[1]);
    cpu.ar[2] = bus.readWord(kNextRec);
    cpu.ar[4] = bus.readWord(kNextSeg);
    while (cpu.ar[4] != cpu.ar[0]) {
        skipRecord(cpu, bus);
        cpu.ar[4] += 1;
    }
    cpu.ar[3] = bus.readWord(kNodes);
    do {
        subs.call(0xAA9D, cpu, bus);
        skipRecord(cpu, bus);
        cpu.ar[4] += 1;
    } while (static_cast<int32_t>(cpu.ar[4]) <= static_cast<int32_t>(cpu.ar[1]));
    bus.writeWord(kNextRec, cpu.ar[2]);
    bus.writeWord(kNextSeg, cpu.ar[4]);
    cpu.ar[1] += 3;
    do {
        subs.call(0xAA9D, cpu, bus);
        skipRecord(cpu, bus);
        cpu.ar[4] += 1;
    } while (static_cast<int32_t>(cpu.ar[4]) <= static_cast<int32_t>(cpu.ar[1]));
    cpu.r[0] = 0xFFFFFFFFu;
    for (int i = 0; i < 4; ++i) bus.writeWord(cpu.ar[3]++, cpu.r[0]);
}

/// Remove AR (`reg`) da lista que começa em `[head]` (próximo em `+0x20`).
/// @return true se achou (e removeu).
bool unlink(MemoryBus& bus, uint32_t head, uint32_t node) {
    uint32_t prev = head - 0x20;
    for (;;) {
        const uint32_t next = bus.readWord(prev + 0x20);
        if (next == 0) return false;
        if (next == node) {
            bus.writeWord(prev + 0x20, bus.readWord(node + 0x20));
            return true;
        }
        prev = next;
    }
}

} // namespace

void skipRecord(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[1] = bus.readWord(cpu.ar[2]);
    cpu.ar[2] += 6;
    if (cpu.r[1] & 1u) cpu.ar[2] += 1;
    if (cpu.r[1] & 8u) cpu.ar[2] += 4;
    if (cpu.r[1] & 0x1000u) cpu.ar[2] += 1;
}

void registerTrackLoader(SubroutineDispatcher& d) {
    registerLoaders(d);
    // 03966  pista com processo extra [B0D6] (ID 0x840) e céu −35; cai em 0396B
    d.add(0x3966, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[2] = b.readWord(0xB0D6);
        c.r[2] = 0x840;
        s.call(0xA844, c, b);
        c.r[0] = doubleToTms(-35.0);
        b.writeWord(0xE3FD, c.r[0]);
        c.r[0] = b.readWord(0xC8C5) | 0x80u;
        b.writeWord(0xC8C5, c.r[0]);
    });
    d.add(0x396B, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.r[0] = b.readWord(0xC8C5) | 0x80u;
        b.writeWord(0xC8C5, c.r[0]);
    });
    d.add(0x396E, [](Tms32031State&, MemoryBus&, ISubroutines&) {});
    // 06754  zera a lista de carros [E404]; [E5EC] = 0x5577 ... Preserva R0.
    d.add(0x6754, [](Tms32031State&, MemoryBus& b, ISubroutines&) {
        b.writeWord(kCarList, 0);
        b.writeWord(kCarCount, 0);
        b.writeWord(0xE5EC, 0x5577);
        b.writeWord(0xE5F2, 0x28);
        b.writeWord(0xE5F3, 0x64);
        b.writeWord(0xE5F4, 0xB);
    });
    // 06762  AR4 no início da lista [E404]; [E5EE] += 1. Preserva R0.
    d.add(0x6762, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        b.writeWord(c.ar[4] + 0x20, b.readWord(kCarList));
        b.writeWord(kCarList, c.ar[4]);
        b.writeWord(kCarCount, b.readWord(kCarCount) + 1);
    });
    // 0676B  tira AR4 da lista de objetos [0x57]; [E5EE] −= 1 se achou. Preserva R0, AR1, AR3.
    d.add(0x676B, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        if (unlink(b, b.readWord(0x57), c.ar[4])) b.writeWord(kCarCount, b.readWord(kCarCount) - 1);
    });
    // 0AF55  zera a lista [E8EC]
    d.add(0xAF55, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.r[0] = 0;
        b.writeWord(kPropList, 0);
    });
    // 0AF58  AR4 no início de [E8EC]: +0xE |= 1 << 22, tipo 0x730, +0x1E = 0. Preserva R0.
    d.add(0xAF58, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        b.writeWord(c.ar[4] + 0x20, b.readWord(kPropList));
        b.writeWord(kPropList, c.ar[4]);
        b.writeWord(c.ar[4] + 0xE, b.readWord(c.ar[4] + 0xE) | (1u << 22));
        b.writeWord(c.ar[4] + 0xF, 0x730);
        b.writeWord(c.ar[4] + 0x1E, 0);
    });
    // 0AF66  tira AR2 da lista [[AF54]]. Preserva R0 e AR1.
    d.add(0xAF66, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { unlink(b, b.readWord(0xAF54), c.ar[2]); });
    // 0AA30  recomeça a conversão: [E854] = 0, [E856] = 1, [E855] = [A103]. Preserva R0.
    d.add(0xAA30, [](Tms32031State&, MemoryBus& b, ISubroutines&) {
        b.writeWord(0xE854, 0);
        b.writeWord(kNextSeg, 1);
        b.writeWord(kNextRec, b.readWord(kTrackData));
    });
    // 0AA39  esvazia a lista de ordenação. Preserva R0.
    d.add(0xAA39, [](Tms32031State&, MemoryBus& b, ISubroutines&) {
        b.writeWord(kSortPool, b.readWord(kSortBase));
        b.writeWord(kSortHead, 0);
    });
    // 0AA40  AR3 = próxima entrada (5 palavras). Preserva AR0.
    d.add(0xAA40, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.ar[3] = b.readWord(kSortPool);
        b.writeWord(kSortPool, c.ar[3] + 5);
    });
    d.add(0xAA48, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { sortedInsert(c, b); });
    d.add(0xAA54, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        const uint32_t r0 = c.r[0], ar0 = c.ar[0], ar1 = c.ar[1];
        flushSorted(c, b);
        c.r[0] = r0;
        c.ar[0] = ar0;
        c.ar[1] = ar1;
    });
    d.add(0xAA66, buildNodes);
    d.add(0xAA9D, buildSegment);
}

} // namespace rea::core::track
