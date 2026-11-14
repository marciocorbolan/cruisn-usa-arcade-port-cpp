/**
 * @file palette_slots.cpp
 * @brief Tradução de `0x9DF6`–`0x9FB5`.
 */
#include "core/video/palette_slots.hpp"
#include "core/tms_float.hpp"

namespace rea::core::video {

using namespace palslot;

namespace {
/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// `PUSH ST; PUSH R0; LDI code,R0; CALL $AB1A; POP R0; POP ST`
void logError(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t code) {
    const uint32_t st = cpu.st, r0 = cpu.r[0];
    cpu.r[0] = code;
    subs.call(0xAB1A, cpu, bus);
    cpu.r[0] = r0;
    cpu.st = st;
}

/**
 * Procura um slot livre (RPTB com `BEQ` para fora do laço).
 * @return true se achou; AR0 = entrada + 1, RC = 0x7F − índice. Senão R0 = última entrada lida.
 */
bool findFreeSlot(Tms32031State& cpu, MemoryBus& bus) {
    cpu.dp = 0;
    cpu.ar[0] = bus.readWord(kOwnerTablePtr);
    cpu.rc = 0x7F;
    cpu.r[0] = bus.readWord(cpu.ar[0]++);
    for (;;) {
        if (cpu.r[0] == 0) return true;
        cpu.r[0] = bus.readWord(cpu.ar[0]++);
        if (s(--cpu.rc) < 0) return false;
    }
}

/// `0x9F84`: tira um nó da lista livre e o põe no início da fila. AR0 = nó; `[CC11]` = 1.
void allocJob(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r0 = cpu.r[0];
    cpu.r[0] = 1;
    bus.writeWord(cpu.direct(kPending), cpu.r[0]);
    cpu.ar[0] = bus.readWord(cpu.direct(kFreeHead));
    cpu.r[0] = bus.readWord(cpu.ar[0]);
    bus.writeWord(cpu.direct(kFreeHead), cpu.r[0]);
    cpu.r[0] = bus.readWord(cpu.direct(kQueueHead));
    bus.writeWord(cpu.ar[0], cpu.r[0]);
    bus.writeWord(cpu.direct(kQueueHead), cpu.ar[0]);
    cpu.r[0] = r0;
}

/// `0x9F8F`: tira o nó AR0 da fila e o devolve à lista livre.
void freeJob(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r0 = cpu.r[0], ar1 = cpu.ar[1];
    cpu.r[1] = bus.readWord(cpu.direct(kQueueCellPtr));
    for (;;) {
        cpu.ar[1] = cpu.r[1];
        cpu.r[1] = bus.readWord(cpu.ar[1]);
        if (cpu.r[1] == 0) break;
        if (cpu.ar[0] != cpu.r[1]) continue;
        cpu.r[1] = bus.readWord(cpu.ar[0]);
        bus.writeWord(cpu.ar[1], cpu.r[1]);
        cpu.r[1] = bus.readWord(cpu.direct(kFreeHead));
        bus.writeWord(cpu.ar[0], cpu.r[1]);
        bus.writeWord(cpu.direct(kFreeHead), cpu.ar[0]);
        break;
    }
    cpu.ar[1] = ar1;
    cpu.r[0] = r0;
}

/// `0x9F5C`: enfileira a cópia de AR2 (origem) para `0x9E0000 + R2` com o cabeçalho R3, com as interrupções desligadas.
void enqueue(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0];
    const uint32_t ie = cpu.ie;
    cpu.ie = 0;
    cpu.st &= ~Tms32031State::kStGie;  // AND $DFFF,ST
    cpu.ie = ie;
    allocJob(cpu, bus);
    bus.writeWord(cpu.ar[0] + 1, cpu.ar[2]);
    cpu.rc = (0x9Eu << 16) + cpu.r[2];
    bus.writeWord(cpu.ar[0] + 2, cpu.rc);
    bus.writeWord(cpu.ar[0] + 3, cpu.r[3]);
    cpu.st |= Tms32031State::kStGie;  // CALL $4C33 (RETIU)
    cpu.ar[0] = ar0;
    cpu.r[0] = r0;
}

/// `0x9F72`: fila vazia; encadeia 128 nós de 4 palavras a partir de `[9F71]` na lista livre `[9F70]`.
void initJobs(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0], ar1 = cpu.ar[1];
    cpu.r[0] = 0;
    bus.writeWord(cpu.direct(kQueueHead), cpu.r[0]);
    cpu.ar[0] = bus.readWord(cpu.direct(kFreeCellPtr));
    cpu.ar[1] = bus.readWord(cpu.direct(kNodesPtr));
    cpu.rc = 0x7F;
    for (;;) {
        bus.writeWord(cpu.ar[0], cpu.ar[1]);
        cpu.ar[0] = cpu.ar[1];
        cpu.ar[1] += 4;
        if (s(--cpu.rc) < 0) break;
    }
    cpu.r[0] = 0;
    bus.writeWord(cpu.ar[0], cpu.r[0]);
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
    cpu.r[0] = r0;
}

/// `0x9E80`: zera as tabelas e reinicia a fila.
void initSlots(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0];
    cpu.dp = 0;
    cpu.r[0] = 0;
    auto fill = [&](uint32_t ptr, uint32_t n) {
        cpu.ar[0] = bus.readWord(ptr);
        for (uint32_t i = 0; i < n; ++i) bus.writeWord(cpu.ar[0]++, cpu.r[0]);
    };
    fill(kOtherTablePtr, 0x180);
    fill(kOwnerTablePtr, 0x80);
    fill(kByPtrTablePtr, 0x80);
    fill(kByIdTablePtr, 0xFE);
    cpu.rc = 0xFFFFFFFFu;
    initJobs(cpu, bus);
    cpu.ar[0] = ar0;
    cpu.r[0] = r0;
}

/// `0x9F9F`: libera o slot do id AR2 (zera a entrada, sem olhar a contagem).
void releaseById(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0];
    cpu.dp = 0;
    cpu.ar[0] = bus.readWord(kByIdTablePtr) + cpu.ar[2];
    cpu.r[0] = bus.readWord(cpu.ar[0]);
    if (cpu.r[0] != 0) {
        // 09FA6  LSH −16 / LSH 16: a contagem vai a 0, e o teste seguinte sempre libera
        cpu.r[0] = (cpu.r[0] >> 16) << 16;
        bus.writeWord(cpu.ar[0], cpu.r[0]);
        cpu.rc = cpu.r[0];
        cpu.r[0] <<= 16;
        if (cpu.r[0] == 0) {
            bus.writeWord(cpu.ar[0], cpu.r[0]);
            cpu.dp = 0;
            cpu.ar[0] = bus.readWord(kOwnerTablePtr) + (cpu.rc >> 16);
            bus.writeWord(cpu.ar[0], cpu.r[0]);
        }
    }
    cpu.ar[0] = ar0;
    cpu.r[0] = r0;
}
} // namespace

void acquireById(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r2 = cpu.r[2], r3 = cpu.r[3], ar1 = cpu.ar[1], ar0 = cpu.ar[0], ar2 = cpu.ar[2];
    cpu.ar[1] = bus.readWord(cpu.direct(kByIdTablePtr)) + cpu.ar[2];
    cpu.r[0] = bus.readWord(cpu.ar[1]);
    if (cpu.r[0] != 0) {
        cpu.r[0] += 1;  // já carregada: mais uma referência
        bus.writeWord(cpu.ar[1], cpu.r[0]);
    } else if (!findFreeSlot(cpu, bus)) {
        logError(cpu, bus, subs, kErrNoSlotById);
    } else {
        // 09F16  ocupação = id | 0x8000; entrada = (slot << 16) + 1; enfileira a cópia
        cpu.r[2] = cpu.ar[2];
        cpu.dp = 0;
        cpu.ar[2] = bus.readWord(bus.readWord(kSourceTablePtr) + cpu.ar[2]);
        cpu.r[2] |= 0x8000u;
        bus.writeWord(cpu.ar[0] - 1, cpu.r[2]);
        cpu.rc -= 0x7F;
        cpu.r[0] = 0u - cpu.rc;
        cpu.r[0] = (cpu.r[0] << 16) + 1;
        bus.writeWord(cpu.ar[1], cpu.r[0]);
        cpu.r[3] = bus.readWord(cpu.ar[2]++);
        cpu.r[2] = (cpu.r[0] >> 16) << 8;
        enqueue(cpu, bus);
        cpu.ar[2] -= 1;
    }
    // 09F27  R0 = (entrada >> 16) << 8
    cpu.r[0] = (cpu.r[0] >> 16) << 8;
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
    cpu.ar[1] = ar1;
    cpu.r[3] = r3;
    cpu.r[2] = r2;
}

void loadByPointer(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r2 = cpu.r[2], r3 = cpu.r[3], ar1 = cpu.ar[1], ar0 = cpu.ar[0], ar2 = cpu.ar[2];
    if (!findFreeSlot(cpu, bus)) {
        logError(cpu, bus, subs, kErrNoSlotByPtr);
    } else {
        // 09F44  ocupação = −1; R0 = (slot << 16) + 1; registra o ponteiro
        cpu.r[2] = 0xFFFFFFFFu;
        bus.writeWord(cpu.ar[0] - 1, cpu.r[2]);
        cpu.rc -= 0x7F;
        cpu.r[0] = 0u - cpu.rc;
        cpu.r[0] = (cpu.r[0] << 16) + 1;
        cpu.r[3] = bus.readWord(cpu.ar[2]++);
        cpu.r[2] = (cpu.r[0] >> 16) << 8;
        enqueue(cpu, bus);
        cpu.ar[2] -= 1;
        cpu.r[0] >>= 16;
        cpu.ar[0] = cpu.r[0];
        cpu.dp = 0;
        cpu.ar[0] += bus.readWord(kByPtrTablePtr);
        bus.writeWord(cpu.ar[0], cpu.ar[2]);
    }
    cpu.r[0] <<= 8;  // 09F55
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
    cpu.ar[1] = ar1;
    cpu.r[3] = r3;
    cpu.r[2] = r2;
}

void servicePaletteQueue(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    cpu.r[7] = 0;
    cpu.dp = 0;
    cpu.r[0] = bus.readWord(kQueueHead);
    if (cpu.r[0] == 0) {
        bus.writeWord(kPending, cpu.r[0]);
        return;
    }
    cpu.r[0] = bus.readWord(kQueueHead);
    for (;;) {
        // 09EA4
        cpu.ar[0] = cpu.r[0];
        if (cpu.ar[0] == 0) return;
        cpu.r[0] = bus.readWord(cpu.ar[0] + 3);
        cpu.ar[1] = bus.readWord(cpu.ar[0] + 1);
        cpu.ar[2] = bus.readWord(cpu.ar[0] + 2);
        if (s(cpu.r[0]) < 0) {
            // empacotado: N = (cab & 0x7FFFFFFF) >> 1 palavras → 2N cores
            cpu.r[0] = (cpu.r[0] << 1) >> 2;
            cpu.r[0] -= 1;
            cpu.rc = cpu.r[0];
            do {
                cpu.r[2] = bus.readWord(cpu.ar[1]++);
                bus.writeWord(cpu.ar[2]++, cpu.r[2]);
                cpu.r[2] >>= 16;
                bus.writeWord(cpu.ar[2]++, cpu.r[2]);
            } while (s(--cpu.rc) >= 0);
        } else {
            cpu.r[0] -= 2;
            cpu.r[2] = bus.readWord(cpu.ar[1]++);
            if (s(cpu.r[0]) >= 0) {
                // RPTS R0: LDI *AR1++,R2 || STI R2,*AR2++
                for (int32_t i = 0; i <= s(cpu.r[0]); ++i) {
                    const uint32_t old = cpu.r[2];
                    cpu.r[2] = bus.readWord(cpu.ar[1]++);
                    bus.writeWord(cpu.ar[2]++, old);
                }
                cpu.rc = 0xFFFFFFFFu;
            }
            bus.writeWord(cpu.ar[2]++, cpu.r[2]);
        }
        // 09E9F  próximo; o atual volta à lista livre
        cpu.r[7] += 1;
        if (s(cpu.r[7]) > static_cast<int32_t>(kMaxCopiesPerFrame)) return;
        cpu.r[0] = bus.readWord(cpu.ar[0]);
        freeJob(cpu, bus);
    }
}

void loadBasePalettes(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    auto byPtr = [&](uint32_t var) {
        cpu.ar[2] = bus.readWord(var);
        subs.call(0x9F2F, cpu, bus);
    };
    // 095F5  reinicia as tabelas e a fila (a 0x9503 acabou de zerar a BSS)
    subs.call(0x9E80, cpu, bus);
    byPtr(0xC710);
    // 095F8  paleta[0xFF] = 0
    cpu.r[0] = 0;
    bus.writeWord(0x9E00FF, cpu.r[0]);
    cpu.dp = 0;
    for (uint32_t v = 0xC711; v <= 0xC715; ++v) byPtr(v);
    subs.call(0x96FF, cpu, bus);
    // 09607  lista de ids [C716] (0x9DF6); id 5 (0x9EFA)
    cpu.ar[2] = bus.readWord(0xC716);
    subs.call(0x9DF6, cpu, bus);
    cpu.ar[2] = 5;
    subs.call(0x9EFA, cpu, bus);
    for (uint32_t v = 0xC717; v <= 0xC71B; ++v) byPtr(v);
    cpu.ar[2] = bus.readWord(0xC71C);
    subs.call(0x9DF6, cpu, bus);
}

void scalePalette(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const Tms32031State saved = cpu;
    const double f = tmsToDouble(cpu.r[0]);
    const uint32_t dst = cpu.ar[1];
    cpu.r[7] = cpu.r[0];
    // 09FC1  cabeçalho; 128 palavras com duas cores cada
    bus.writeWord(cpu.ar[1]++, bus.readWord(cpu.ar[0]++));
    for (cpu.ar[5] = 0x7F; static_cast<int32_t>(cpu.ar[5]) >= 0; --cpu.ar[5]) {
        cpu.r[4] = bus.readWord(cpu.ar[0]++);
        cpu.r[1] = (scaleColor15(cpu.r[4] >> 16, f) << 16) | scaleColor15(cpu.r[4] & 0xFFFFu, f);
        bus.writeWord(cpu.ar[1]++, cpu.r[1]);
    }
    // 09FF6  envia: slot da paleta (0x9EDB) e cópia (0x9F5C, R3 = cabeçalho)
    cpu.ar[2] = dst;
    subs.call(0x9EDB, cpu, bus);
    cpu.r[3] = bus.readWord(cpu.ar[2]++);
    cpu.r[2] = cpu.r[0];
    subs.call(0x9F5C, cpu, bus);
    cpu.r[7] = saved.r[7];
    cpu.r[6] = saved.r[6];
    cpu.r[1] = saved.r[1];
    cpu.r[0] = saved.r[0];
    cpu.ar[2] = saved.ar[2];
    cpu.ar[1] = saved.ar[1];
    cpu.ar[0] = saved.ar[0];
}

void registerPaletteSlots(SubroutineDispatcher& d) {
    d.add(0x95F5, loadBasePalettes);
    d.add(0x9FB6, scalePalette);
    // 096FF  19 paletas por ponteiro: [C71E]..[C730]
    d.add(0x96FF, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        for (uint32_t v = 0xC71E; v <= 0xC730; ++v) {
            c.ar[2] = b.readWord(v);
            s.call(0x9F2F, c, b);
        }
    });
    d.add(0x9E80, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { initSlots(c, b); });
    d.add(0x9E97, servicePaletteQueue);
    d.add(0x9EFA, acquireById);
    d.add(0x9F2F, loadByPointer);
    d.add(0x9F5C, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { enqueue(c, b); });
    d.add(0x9F72, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { initJobs(c, b); });
    d.add(0x9F84, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { allocJob(c, b); });
    d.add(0x9F8F, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { freeJob(c, b); });
    d.add(0x9F9F, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { releaseById(c, b); });
    // 09EC2  troca a paleta do id R0 pela de id R1 (se R0 estiver carregado)
    d.add(0x9EC2, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[2] = c.r[0];
        s.call(0x9ECD, c, b);
        if (c.carry()) return;
        c.dp = 0;
        c.ar[2] = b.readWord(b.readWord(kSourceTablePtr) + c.r[1]);
        c.r[3] = b.readWord(c.ar[2]++);
        c.r[2] = c.r[0];
        enqueue(c, b);
    });
    // 09DF6  AR6 = 1º id, AR5 = quantidade − 1: 0x9EFA para cada id
    d.add(0x9DF6, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[6] = b.readWord(c.ar[2]++);
        c.ar[5] = b.readWord(c.ar[2]) - 1;
        for (;;) {
            c.ar[2] = c.ar[6];
            s.call(0x9EFA, c, b);
            c.ar[6] += 1;
            const uint32_t res = (c.ar[5] - 1u) & 0x00FFFFFFu;
            c.ar[5] = (c.ar[5] & 0xFF000000u) | res;
            if (res & 0x00800000u) break;
        }
    });
    // 09DFE  idem com 0x9F9F (libera); preserva AR4–AR6
    d.add(0x9DFE, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t ar4 = c.ar[4], ar5 = c.ar[5], ar6 = c.ar[6];
        c.ar[6] = b.readWord(c.ar[2]++);
        c.ar[5] = b.readWord(c.ar[2]) - 1;
        for (;;) {
            c.ar[2] = c.ar[6];
            s.call(0x9F9F, c, b);
            c.ar[6] += 1;
            const uint32_t res = (c.ar[5] - 1u) & 0x00FFFFFFu;
            c.ar[5] = (c.ar[5] & 0xFF000000u) | res;
            if (res & 0x00800000u) break;
        }
        c.ar[6] = ar6;
        c.ar[5] = ar5;
        c.ar[4] = ar4;
    });
}

} // namespace rea::core::video
