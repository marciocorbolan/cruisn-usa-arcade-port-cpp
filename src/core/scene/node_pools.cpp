/**
 * @file node_pools.cpp
 * @brief Tradução de `0x8EF1`–`0x8F3D`, `0x8FF0`–`0x9035` e `0x7648`.
 */
#include "core/scene/node_pools.hpp"

namespace rea::core::scene {

namespace {
using namespace pool_vars;

/// Liga ou desliga o bit C (carry) de ST, que a rotina original usa como retorno de sucesso ou falha.
void setCarry(Tms32031State& cpu, bool on) {
    if (on) cpu.st |= Tms32031State::kStCarry;
    else cpu.st &= ~Tms32031State::kStCarry;
}
} // namespace

void initPool(Tms32031State& cpu, MemoryBus& bus) {
    // 08EF3  [R3] = 0; AR0 = R2; RPTB: *AR0 = AR2; AR0 = AR2; AR2 += RS; *AR0 = 0
    bus.writeWord(cpu.r[3], 0);
    uint32_t link = cpu.r[2];
    const uint32_t count = cpu.rc + 1;
    for (uint32_t i = 0; i < count; ++i) {
        bus.writeWord(link, cpu.ar[2]);
        link = cpu.ar[2];
        cpu.ar[2] += cpu.rs;
    }
    bus.writeWord(link, 0);
    cpu.rc = 0xFFFFFFFFu;
}

void allocToList(Tms32031State& cpu, MemoryBus& bus) {
    // 08F03  R0 = [AR2]; 0 → carry = 0
    cpu.r[0] = bus.readWord(cpu.ar[2]);
    if (cpu.r[0] == 0) {
        setCarry(cpu, false);
        return;
    }
    // 08F05  [AR2] = [nó]; [nó] = [R2]; [R2] = nó
    cpu.ar[0] = bus.readWord(cpu.r[0]);
    bus.writeWord(cpu.ar[2], cpu.ar[0]);
    cpu.ar[0] = cpu.r[0];
    bus.writeWord(cpu.ar[0], bus.readWord(cpu.r[2]));
    bus.writeWord(cpu.r[2], cpu.ar[0]);
    setCarry(cpu, true);
}

void allocNode(Tms32031State& cpu, MemoryBus& bus) {
    // 08F14  nó = [AR2]; [AR2] = [nó]; AR0 = nó
    const uint32_t node = bus.readWord(cpu.ar[2]);
    if (node == 0) {
        setCarry(cpu, false);
        return;
    }
    bus.writeWord(cpu.ar[2], bus.readWord(node));
    cpu.ar[0] = node;
    setCarry(cpu, true);
}

void pushFront(Tms32031State& cpu, MemoryBus& bus) {
    // 08F21  [AR2] = [R2]; [R2] = AR2
    bus.writeWord(cpu.ar[2], bus.readWord(cpu.r[2]));
    bus.writeWord(cpu.r[2], cpu.ar[2]);
}

void moveNode(Tms32031State& cpu, MemoryBus& bus) {
    // 08F2D  procura o antecessor de AR2 a partir de R2
    uint32_t prev = cpu.r[2];
    for (;;) {
        const uint32_t next = bus.readWord(prev);
        if (next == 0) return;
        if (next == cpu.ar[2]) break;
        prev = next;
    }
    // 08F33  [prev] = [AR2]; [AR2] = [R3]; [R3] = AR2
    bus.writeWord(prev, bus.readWord(cpu.ar[2]));
    bus.writeWord(cpu.ar[2], bus.readWord(cpu.r[3]));
    bus.writeWord(cpu.r[3], cpu.ar[2]);
}

void registerNodePools(SubroutineDispatcher& d) {
    d.add(0x8EF1, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { initPool(c, b); });
    d.add(0x8F01, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { allocToList(c, b); });
    d.add(0x8F13, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { allocNode(c, b); });
    d.add(0x8F1F, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { pushFront(c, b); });
    d.add(0x8F29, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { moveNode(c, b); });

    // 07648  pool dos textos; [E91B] = 0xA0 (R2 fica 0xA0)
    d.add(0x7648, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[2] = b.readWord(kTextPoolBase);
        c.r[2] = b.readWord(kTextFreePtr);
        c.r[3] = b.readWord(kTextListPtr);
        c.rc = kTextNodes - 1;
        c.rs = kTextNodeSize;
        s.call(0x8EF1, c, b);
        c.r[2] = kTextNodes;
        b.writeWord(kTextFreeCount, c.r[2]);
    });
    // 08FF0  pool de matrizes (preserva AR2, R2, R3, RC, RS)
    d.add(0x8FF0, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const Tms32031State saved = c;
        c.ar[2] = b.readWord(kMatPoolBase);
        c.r[2] = b.readWord(kMatFreePtr);
        c.r[3] = b.readWord(kMatListPtr);
        c.rc = 0xAE;
        c.rs = 0x13;
        s.call(0x8EF1, c, b);
        c.ar[2] = saved.ar[2];
        c.r[2] = saved.r[2];
        c.r[3] = saved.r[3];
        c.rc = saved.rc;
        c.rs = saved.rs;
    });
    // 09001  nó de [DB11] com identidade em +7
    d.add(0x9001, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t r0 = c.r[0];
        c.ar[0] = b.readWord(kMatFree);
        if (c.ar[0] == 0) {
            setCarry(c, false);
        } else {
            b.writeWord(kMatFree, b.readWord(c.ar[0]));
            c.ar[0] += 7;
            s.call(0x93D7, c, b);
            c.ar[0] -= 7;
            b.writeWord(c.ar[0] + 0x12, 0);
            setCarry(c, true);
        }
        c.r[0] = r0;
    });
    // 09011  devolve AR2 a [DB11]
    d.add(0x9011, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        b.writeWord(c.ar[2], b.readWord(kMatFree));
        b.writeWord(kMatFree, c.ar[2]);
    });
    // 09019  0x19 nós de 0x5B palavras; [E3F7] = 0
    d.add(0x9019, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.ar[0] = b.readWord(kBigFreePtr);
        c.ar[1] = b.readWord(kBigPoolBase);
        for (int i = 0; i < 0x19; ++i) {
            b.writeWord(c.ar[0], c.ar[1]);
            c.ar[0] = c.ar[1];
            c.ar[1] += 0x5B;
        }
        c.rc = 0xFFFFFFFFu;
        c.r[0] = 0;
        b.writeWord(c.ar[0], 0);
        b.writeWord(kBigInUse, 0);
    });
    // 09024  nó de [E3F6]; [E3F7] += 1
    d.add(0x9024, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.ar[0] = b.readWord(kBigFree);
        if (c.ar[0] == 0) {
            setCarry(c, false);
            return;
        }
        b.writeWord(kBigFree, b.readWord(c.ar[0]));
        b.writeWord(kBigInUse, b.readWord(kBigInUse) + 1);
        setCarry(c, true);
    });
    // 09032  devolve AR2 a [E3F6]; [E3F7] −= 1
    d.add(0x9032, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        b.writeWord(c.ar[2], b.readWord(kBigFree));
        b.writeWord(kBigFree, c.ar[2]);
        b.writeWord(kBigInUse, b.readWord(kBigInUse) - 1);
    });
}

} // namespace rea::core::scene
