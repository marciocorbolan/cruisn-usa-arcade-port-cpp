/**
 * @file scene_object.cpp
 * @brief Tradução de `0x700A`–`0x70C9`, `0x715F`, `0x7309` e `0x9ECD`.
 */
#include "core/scene/scene_object.hpp"

#include "core/scene/object_list.hpp"
#include "core/tms_float.hpp"

#include <utility>

namespace rea::core::scene {

using namespace sobj;

namespace {
/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
/// Liga ou desliga o bit C (carry) de ST, que a rotina original usa como retorno de sucesso ou falha.
inline void setCarry(Tms32031State& cpu, bool c) {
    cpu.st = c ? (cpu.st | Tms32031State::kStCarry) : (cpu.st & ~Tms32031State::kStCarry);
}
/// Lê o bit C (carry) de ST.
inline bool carry(const Tms32031State& cpu) { return (cpu.st & Tms32031State::kStCarry) != 0; }

/// `PUSH ST; PUSH R0; LDI code,R0; CALL $AB1A; POP R0; POP ST`
void logError(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t code) {
    const uint32_t st = cpu.st, r0 = cpu.r[0];
    cpu.r[0] = code;
    subs.call(0xAB1A, cpu, bus);
    cpu.r[0] = r0;
    cpu.st = st;
}

/// Insere AR2 depois de AR0 percorrendo a partir da célula AR1; `stop(chaveDoNó)` decide onde parar.
template <typename Stop>
void sortedInsert(Tms32031State& cpu, MemoryBus& bus, Stop stop) {
    cpu.r[0] = bus.readWord(cpu.ar[2] + kDepth);
    for (;;) {
        cpu.ar[0] = cpu.ar[1];
        cpu.r[1] = bus.readWord(cpu.ar[1]);
        if (cpu.r[1] == 0) break;
        cpu.ar[1] = cpu.r[1];
        if (stop(bus.readWord(cpu.ar[1] + kDepth))) break;
    }
    bus.writeWord(cpu.ar[2], cpu.r[1]);
    bus.writeWord(cpu.ar[0], cpu.ar[2]);
}
} // namespace

void allocObject(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r0 = cpu.r[0];
    // 07026  AR0 = [C983]
    cpu.r[0] = bus.readWord(cpu.direct(kFreeHead));
    cpu.ar[0] = cpu.r[0];
    if (cpu.r[0] == 0) {
        logError(cpu, bus, subs, kErrPoolEmpty);  // 0704F
        setCarry(cpu, true);
        cpu.r[0] = r0;
        return;
    }
    cpu.r[0] = bus.readWord(cpu.ar[0]);
    bus.writeWord(cpu.direct(kFreeHead), cpu.r[0]);
    cpu.r[0] = bus.readWord(cpu.direct(kFreeCount)) - 1;
    bus.writeWord(cpu.direct(kFreeCount), cpu.r[0]);
    cpu.r[0] = 0;
    for (uint32_t o : {0x0Eu, 0x17u, 0x1Eu, 0x0Fu, 0x1Fu, 0x20u, 0x21u}) bus.writeWord(cpu.ar[0] + o, cpu.r[0]);
    cpu.r[0] = doubleToTms(0.0);
    for (uint32_t o : {1u, 2u, 3u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 5u, 6u, 7u, 9u, 0xAu, 0xBu})
        bus.writeWord(cpu.ar[0] + o, cpu.r[0]);
    cpu.r[0] = doubleToTms(1.0);
    for (uint32_t o : {4u, 8u, 0xCu}) bus.writeWord(cpu.ar[0] + o, cpu.r[0]);
    setCarry(cpu, false);
    cpu.r[0] = r0;
}

void textureSlot(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t ar2 = cpu.ar[2];
    setCarry(cpu, true);
    cpu.dp = 0;
    cpu.ar[2] += bus.readWord(kTexTablePtr);
    cpu.r[0] = bus.readWord(cpu.ar[2]);
    if (cpu.r[0] != 0) {
        cpu.r[0] = (cpu.r[0] >> 16) << 8;
        setCarry(cpu, false);
    }
    cpu.ar[2] = ar2;
}

void computeDepth(Tms32031State& cpu, MemoryBus& bus) {
    auto f = [&](uint32_t a) { return tmsToDouble(bus.readWord(a)); };
    // 0705C  (pos − [809800..02]) · [80980F..11]
    const double dx = f(cpu.ar[2] + 1) - f(0x809800), dy = f(cpu.ar[2] + 2) - f(0x809801),
                 dz = f(cpu.ar[2] + 3) - f(0x809802);
    const double d = dx * f(0x80980F) + dy * f(0x809810) + dz * f(0x809811);
    cpu.r[0] = tmsFix(d);
    bus.writeWord(cpu.ar[2] + kDepth, cpu.r[0]);
}

void linkThird(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], ar0 = cpu.ar[0], ar1 = cpu.ar[1];
    computeDepth(cpu, bus);
    cpu.r[0] = bus.readWord(cpu.ar[2] + kFlags) | kListMask;
    bus.writeWord(cpu.ar[2] + kFlags, cpu.r[0]);
    cpu.ar[1] = bus.readWord(cpu.direct(kThirdHeadPtr));
    // 0707E  CMPI *+AR1(1C),R0; BLE continua → para no 1º com chave < R0
    sortedInsert(cpu, bus, [&](uint32_t key) { return s(cpu.r[0] - key) > 0; });
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}

void linkObject(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], ar0 = cpu.ar[0], ar1 = cpu.ar[1];
    computeDepth(cpu, bus);
    const bool near = s(cpu.r[0]) >= -5000 && s(cpu.r[0] - bus.readWord(cpu.direct(cull::kFarRangePtr))) < 0;
    if (!near) {
        // 0709D  distante: início da lista [[0x41]]
        cpu.r[0] = bus.readWord(cpu.ar[2] + kFlags) | 0x2000u;
        bus.writeWord(cpu.ar[2] + kFlags, cpu.r[0]);
        cpu.ar[1] = bus.readWord(cpu.direct(cull::kFarHeadPtr));
        cpu.r[0] = bus.readWord(cpu.ar[1]);
        bus.writeWord(cpu.ar[2], cpu.r[0]);
        bus.writeWord(cpu.ar[1], cpu.ar[2]);
    } else {
        // 070A5  visível: antes do 1º com chave ≤ R0 (CMPI *+AR1(1C),R0; BLT continua)
        cpu.r[0] = bus.readWord(cpu.ar[2] + kFlags) | 0x1000u;
        bus.writeWord(cpu.ar[2] + kFlags, cpu.r[0]);
        cpu.ar[1] = bus.readWord(cpu.direct(objlist::kHeadCellPtr));
        sortedInsert(cpu, bus, [&](uint32_t key) { return s(cpu.r[0] - key) >= 0; });
    }
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}

void findByType(Tms32031State& cpu, MemoryBus& bus, bool third) {
    const uint32_t r0 = cpu.r[0];
    cpu.ar[0] = bus.readWord(cpu.direct(third ? kThirdHeadPtr : objlist::kHeadCellPtr));
    bool found = false;
    for (cpu.r[0] = bus.readWord(cpu.ar[0]); cpu.r[0] != 0; cpu.r[0] = bus.readWord(cpu.ar[0])) {
        cpu.ar[0] = cpu.r[0];
        if (bus.readWord(cpu.ar[0] + objlist::kType) == cpu.ar[2]) {
            found = true;
            break;
        }
    }
    setCarry(cpu, found);
    cpu.r[0] = r0;
}

void unlinkObject(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], ar1 = cpu.ar[1], ar2 = cpu.ar[2];
    // 07163  lista conforme +0xE & 0x3000: 0x2000 → [0x41]; 0x3000 → [0x42]; senão [0x40]
    cpu.r[0] = bus.readWord(cpu.ar[2] + kFlags) & kListMask;
    cpu.r[1] = bus.readWord(cpu.direct(objlist::kHeadCellPtr));
    if (cpu.r[0] == 0x2000) cpu.r[1] = bus.readWord(cpu.direct(cull::kFarHeadPtr));
    if (cpu.r[0] == 0x3000) cpu.r[1] = bus.readWord(cpu.direct(kThirdHeadPtr));
    for (;;) {
        cpu.ar[1] = cpu.r[1];
        cpu.r[1] = bus.readWord(cpu.ar[1]);
        if (cpu.r[1] == 0) {
            logError(cpu, bus, subs, kErrNotInList);  // 0716E
            break;
        }
        if (cpu.ar[2] != cpu.r[1]) continue;
        // 07177  *AR1 = *AR2; +0xE &= ~0x3000
        cpu.r[1] = bus.readWord(cpu.ar[2]);
        bus.writeWord(cpu.ar[1], cpu.r[1]);
        cpu.r[0] = bus.readWord(cpu.ar[2] + kFlags) & ~kListMask;
        bus.writeWord(cpu.ar[2] + kFlags, cpu.r[0]);
        break;
    }
    cpu.ar[2] = ar2;
    cpu.ar[1] = ar1;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}

bool allocFromModel(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    allocObject(cpu, bus, subs);
    if (carry(cpu)) return false;  // 0700B  RETSLO
    // 0700C  PUSH R0, R1
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1];
    bool hung = false;
    bus.writeWord(cpu.ar[0] + kModel, cpu.ar[2]);
    cpu.r[0] = bus.readWord(cpu.ar[2]);
    bus.writeWord(cpu.ar[0] + kRadius, cpu.r[0]);
    cpu.r[0] = bus.readWord(cpu.ar[2] - 1);
    if (cpu.r[0] & 0x1000u) {
        cpu.r[1] = bus.readWord(cpu.ar[0] + kFlags) | 0x40u;
        bus.writeWord(cpu.ar[0] + kFlags, cpu.r[1]);
    }
    if (cpu.r[0] & 0x2000u) {
        cpu.r[1] = bus.readWord(cpu.ar[0] + kFlags) | 0x400u;
        bus.writeWord(cpu.ar[0] + kFlags, cpu.r[1]);
        cpu.ar[2] = cpu.r[0] & 0x0FFFu;
        textureSlot(cpu, bus);
        hung = carry(cpu);  // 0701F  BLO $701F: trava até o watchdog
        bus.writeWord(cpu.ar[0] + kTexture, cpu.r[0]);
    }
    setCarry(cpu, false);
    cpu.r[1] = r1;
    cpu.r[0] = r0;
    return hung;
}

/**
 * `0x44BB`: como a `0x440C`, mas no espaço da câmera do percurso: as posições da lista passam pela
 * rotação Y(`[E402]`) (matriz `[0x5A]`, `0x939D`) e somam `[E3FF]`..`[E401]`; o ângulo soma `[E402]`.
 * A cadeia `+0x1F` começa em `[B0D5]` (= 0xE47D, a mesma da `0x440C`).
 */
void spawnObjectListRotated(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r4 = cpu.r[4], r5 = cpu.r[5], ar0 = cpu.ar[0], ar2 = cpu.ar[2], ar4 = cpu.ar[4],
                   ar5 = cpu.ar[5], ar6 = cpu.ar[6], ar7 = cpu.ar[7];
    constexpr uint32_t kChain = 0x1F;
    auto rf = [&](uint32_t a) { return tmsToDouble(bus.readWord(a)); };
    cpu.ar[6] = bus.readWord(cpu.direct(0xB0D5)) - kChain;
    cpu.ar[5] = cpu.ar[2] + 1;
    cpu.r[4] = bus.readWord(cpu.ar[5]++) - 1;
    cpu.r[2] = bus.readWord(0xE402);
    cpu.ar[2] = bus.readWord(0x5A);
    subs.call(0x939D, cpu, bus);
    do {
        // 044CD
        cpu.ar[2] = bus.readWord(cpu.ar[5]++);
        subs.call(0x700A, cpu, bus);
        if (carry(cpu)) break;  // BLO $44F4
        cpu.ar[4] = cpu.ar[0];
        bus.writeWord(cpu.ar[6] + kChain, cpu.ar[4]);
        cpu.ar[6] = cpu.ar[4];
        cpu.ar[2] = bus.readWord(0x5D);
        for (uint32_t i = 0; i < 3; ++i) {
            cpu.r[1] = doubleToTms(static_cast<double>(s(bus.readWord(cpu.ar[5]++))));
            bus.writeWord(cpu.ar[2] + i, cpu.r[1]);
        }
        cpu.r[2] = bus.readWord(0x5A);
        cpu.r[3] = cpu.ar[2];
        subs.call(0x941E, cpu, bus);
        for (uint32_t i = 0; i < 3; ++i) {
            cpu.r[1] = doubleToTms(rf(cpu.ar[2] + i) + rf(0xE3FF + i));
            bus.writeWord(cpu.ar[4] + 1 + i, cpu.r[1]);
        }
        cpu.r[2] = doubleToTms(rf(cpu.ar[5]++) + rf(0xE402));
        bus.writeWord(cpu.ar[4] + 0x15, cpu.r[2]);
        cpu.ar[2] = cpu.ar[4] + 4;
        subs.call(0x939D, cpu, bus);
        cpu.r[1] = bus.readWord(cpu.ar[5]++);
        cpu.r[2] = cpu.r[1];
        cpu.r[1] &= 0x0FFFu;
        bus.writeWord(cpu.ar[4] + objlist::kType, cpu.r[1]);
        cpu.ar[2] = cpu.ar[4];
        subs.call(0x7093, cpu, bus);
        cpu.r[4] -= 1;
    } while (s(cpu.r[4]) >= 0);
    cpu.r[0] = 0;
    bus.writeWord(cpu.ar[6] + kChain, cpu.r[0]);
    cpu.r[0] = bus.readWord(cpu.direct(0xE47D));
    cpu.ar[7] = ar7;
    cpu.ar[6] = ar6;
    cpu.ar[5] = ar5;
    cpu.ar[4] = ar4;
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
    cpu.r[5] = r5;
    cpu.r[4] = r4;
}

void spawnObjectList(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r4 = cpu.r[4], r5 = cpu.r[5], ar0 = cpu.ar[0], ar2 = cpu.ar[2], ar4 = cpu.ar[4],
                   ar5 = cpu.ar[5], ar6 = cpu.ar[6], ar7 = cpu.ar[7];
    constexpr uint32_t kChain = 0x1F;
    // 04414  AR6 = [B0D1] − 0x1F (assim *+AR6(1F) = [E47D]); AR5 = AR2 + 1; R4 = quantidade − 1
    cpu.ar[6] = bus.readWord(cpu.direct(0xB0D1)) - kChain;
    cpu.ar[5] = cpu.ar[2] + 1;
    cpu.r[4] = bus.readWord(cpu.ar[5]++) - 1;
    do {
        // 0441B
        cpu.ar[2] = bus.readWord(cpu.ar[5]++);
        subs.call(0x700A, cpu, bus);
        if (carry(cpu)) break;  // BLO $4434
        cpu.ar[4] = cpu.ar[0];
        bus.writeWord(cpu.ar[6] + kChain, cpu.ar[4]);
        cpu.ar[6] = cpu.ar[4];
        for (uint32_t i = 1; i <= 3; ++i) {
            cpu.r[1] = doubleToTms(static_cast<double>(s(bus.readWord(cpu.ar[5]++))));
            bus.writeWord(cpu.ar[4] + i, cpu.r[1]);
        }
        cpu.r[2] = bus.readWord(cpu.ar[5]++);
        bus.writeWord(cpu.ar[4] + 0x15, cpu.r[2]);
        cpu.ar[2] = cpu.ar[4] + 4;
        subs.call(0x939D, cpu, bus);
        cpu.r[1] = bus.readWord(cpu.ar[5]++);
        cpu.r[2] = cpu.r[1];
        cpu.r[1] &= 0x0FFFu;
        bus.writeWord(cpu.ar[4] + objlist::kType, cpu.r[1]);
        cpu.ar[2] = cpu.ar[4];
        subs.call(0x7093, cpu, bus);
        cpu.r[4] -= 1;
    } while (s(cpu.r[4]) >= 0);
    // 04434  fecha a cadeia; R0 = [E47D]
    cpu.r[0] = 0;
    bus.writeWord(cpu.ar[6] + kChain, cpu.r[0]);
    cpu.r[0] = bus.readWord(cpu.direct(0xE47D));
    cpu.ar[7] = ar7;
    cpu.ar[6] = ar6;
    cpu.ar[5] = ar5;
    cpu.ar[4] = ar4;
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
    cpu.r[5] = r5;
    cpu.r[4] = r4;
}

void registerSceneObject(SubroutineDispatcher& d, uint64_t& hangs) {
    d.add(0x7025, allocObject);
    d.add(0x700A, [&hangs](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        if (allocFromModel(c, b, s)) ++hangs;
    });
    d.add(0x7058, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        const uint32_t r1 = c.r[1], r2 = c.r[2];
        computeDepth(c, b);
        c.r[1] = r1;
        c.r[2] = r2;
    });
    // 07309  aloca (0x7025); +0xD = modelo AR2; posição = FLOAT(R2, R3, RC); carry = 0 (pool vazio: carry da 0x7025)
    d.add(0x7309, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        s.call(0x7025, c, b);
        if (c.carry()) return;
        b.writeWord(c.ar[0] + 0xD, c.ar[2]);
        c.r[2] = doubleToTms(static_cast<double>(static_cast<int32_t>(c.r[2])));
        b.writeWord(c.ar[0] + 1, c.r[2]);
        c.r[3] = doubleToTms(static_cast<double>(static_cast<int32_t>(c.r[3])));
        b.writeWord(c.ar[0] + 2, c.r[3]);
        c.r[2] = doubleToTms(static_cast<double>(static_cast<int32_t>(c.rc)));
        b.writeWord(c.ar[0] + 3, c.r[2]);
        c.st &= ~Tms32031State::kStCarry;
    });
    d.add(0x7070, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { linkThird(c, b); });
    d.add(0x7087, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        b.writeWord(c.ar[2], b.readWord(c.direct(kStackA)));
        b.writeWord(c.direct(kStackA), c.ar[2]);
    });
    d.add(0x708D, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        b.writeWord(c.ar[2], b.readWord(c.direct(kStackB)));
        b.writeWord(c.direct(kStackB), c.ar[2]);
    });
    d.add(0x7093, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { linkObject(c, b); });
    d.add(0x70B8, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { findByType(c, b, true); });
    d.add(0x70BB, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { findByType(c, b, false); });
    d.add(0x715F, unlinkObject);
    d.add(0x440C, spawnObjectList);
    d.add(0x44BB, spawnObjectListRotated);
    d.add(0x9ECD, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { textureSlot(c, b); });
    // 070CA / 070CF / 070E5  tira AR2 da lista [[0x59]] / [[0x58]] / [[0x56]] (próximo em +0x20).
    // Fim da lista sem achar → erro 0x101/0x102 (0xAB1A). Preserva R1 e AR1.
    auto unlinkFrom = [](uint32_t headPtr, uint32_t err) {
        return [headPtr, err](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
            uint32_t prev = b.readWord(headPtr) - 0x20;
            for (;;) {
                const uint32_t next = b.readWord(prev + 0x20);
                if (next == 0) {
                    const uint32_t r0 = c.r[0], st = c.st;
                    c.r[0] = err;
                    s.call(0xAB1A, c, b);
                    c.r[0] = r0;
                    c.st = st;
                    return;
                }
                if (next == c.ar[2]) {
                    b.writeWord(prev + 0x20, b.readWord(c.ar[2] + 0x20));
                    return;
                }
                prev = next;
            }
        };
    };
    d.add(0x70CA, unlinkFrom(0x59, 0x101));
    d.add(0x70CF, unlinkFrom(0x58, 0x101));
    d.add(0x70E5, unlinkFrom(0x56, 0x102));
    // 07100  libera o objeto AR2: peças (bit 11, 0x9011), tira das listas marcadas em +0xE
    //        (bit 22 → [[AF54]], 23 → processo +0x17, 28 → [[0x56]], 21 → [[0x59]], 31 → [[0x58]]),
    //        da lista visível ([0x40] / [0x41] com 0x2000 / [0x42] com 0x3000) e devolve à lista
    //        livre [C983] (+1 em [C989]; +0x1F e +0xE zerados). Não achou na lista → erro 0x120.
    //        Preserva R0, R1, AR1 e AR2.
    d.add(0x7100, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t r0 = c.r[0], r1 = c.r[1], ar1 = c.ar[1], ar2 = c.ar[2];
        if (b.readWord(ar2 + 0xE) & 0x800u) {
            c.ar[0] = b.readWord(ar2 + 0x1A);
            do {
                c.r[0] = b.readWord(c.ar[0]);
                c.ar[2] = c.ar[0];
                s.call(0x9011, c, b);
                c.ar[0] = c.r[0];
            } while (c.ar[0] != 0);
            c.ar[2] = ar2;
        }
        // 07110  LSH −n: o carry é o último bit deslocado (bit n − 1)
        const std::pair<uint32_t, uint32_t> marks[] = {
            {22, 0xAF66}, {23, 0x70FB}, {28, 0x70E5}, {21, 0x70CA}, {31, 0x70CF}};
        for (const auto& [bit, routine] : marks) {
            if ((b.readWord(ar2 + 0xE) >> bit) & 1u) s.call(routine, c, b);
            c.ar[2] = ar2;
        }
        // 0711F  lista visível pelo campo 0x3000
        const uint32_t kind = b.readWord(ar2 + 0xE) & 0x3000u;
        c.r[0] = kind;
        uint32_t head = b.readWord(c.direct(0x40));
        if (kind == 0x2000u) head = b.readWord(c.direct(0x41));
        if (kind == 0x3000u) head = b.readWord(c.direct(0x42));
        c.ar[1] = head;
        for (;;) {
            c.r[1] = b.readWord(c.ar[1]);
            if (c.r[1] == 0) {
                const uint32_t st = c.st;
                c.r[0] = 0x120;
                s.call(0xAB1A, c, b);
                c.st = st;
                break;
            }
            if (c.r[1] == ar2) {
                b.writeWord(c.ar[1], b.readWord(ar2));
                b.writeWord(ar2, b.readWord(c.direct(kFreeHead)));
                b.writeWord(c.direct(kFreeHead), ar2);
                b.writeWord(c.direct(kFreeCount), b.readWord(c.direct(kFreeCount)) + 1);
                b.writeWord(ar2 + 0x1F, 0);
                b.writeWord(ar2 + 0xE, 0);
                break;
            }
            c.ar[1] = c.r[1];
        }
        c.r[0] = r0; c.r[1] = r1; c.ar[1] = ar1; c.ar[2] = ar2;
    });
    // 07143  libera todos os objetos da lista [[0x40]] com (+0xF & R1) = (R0 & R1). Preserva R0–R3, AR0, AR2.
    d.add(0x7143, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t ar0 = c.ar[0], ar2 = c.ar[2], r0 = c.r[0], r1 = c.r[1], r2 = c.r[2], r3 = c.r[3];
        const uint32_t want = c.r[0] & c.r[1], mask = c.r[1];
        uint32_t node = b.readWord(b.readWord(c.direct(0x40)));
        while (node != 0) {
            const uint32_t next = b.readWord(node);
            if ((b.readWord(node + 0xF) & mask) == want) {
                c.ar[2] = node;
                s.call(0x7100, c, b);
            }
            node = next;
        }
        c.ar[0] = ar0; c.ar[2] = ar2; c.r[0] = r0; c.r[1] = r1; c.r[2] = r2; c.r[3] = r3;
    });
    // 070FB  mata o processo [AR2+0x17] (0xA8AC). Preserva AR2.
    d.add(0x70FB, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t ar2 = c.ar[2];
        c.ar[2] = b.readWord(c.ar[2] + 0x17);
        s.call(0xA8AC, c, b);
        c.ar[2] = ar2;
    });
}

} // namespace rea::core::scene
