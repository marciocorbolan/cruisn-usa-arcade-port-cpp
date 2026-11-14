/**
 * @file link_packet.cpp
 * @brief Tradução da montagem do pacote de envio do link.
 */
#include "core/link/link_packet.hpp"

#include "core/link/link_routines.hpp"
#include "core/tms_float.hpp"

namespace rea::core::link {

using namespace packet;

namespace {
/// `LDI @($C8CC),R0 / TSTB 4,R0 / BNE`: true se a DIP Linking está desligada.
bool linkingOff(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[0] = bus.readWord(cpu.direct(link_vars::kDipShadow));
    return (cpu.r[0] & 0x0004u) != 0;
}
} // namespace

void appendWord(Tms32031State& cpu, MemoryBus& bus) {
    // 079AF  PUSH R0, AR0
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0];
    if (!linkingOff(cpu, bus)) {
        // 079B4  R0 = [C8D3] + 1; > 0x258: descarta
        cpu.r[0] = bus.readWord(cpu.direct(link_vars::kTxLength)) + 1;
        if (static_cast<int32_t>(cpu.r[0]) <= static_cast<int32_t>(kMaxWords)) {
            // 079B8  [C8D3] = R0; *(buffer + R0 − 1) = AR2
            bus.writeWord(cpu.direct(link_vars::kTxLength), cpu.r[0]);
            cpu.ar[0] = bus.readWord(cpu.direct(link_vars::kTxBufferPtr)) + cpu.r[0];
            bus.writeWord(cpu.ar[0] - 1, cpu.ar[2]);
        }
    }
    // 079BC  POP AR0, R0
    cpu.ar[0] = ar0;
    cpu.r[0] = r0;
}

void appendBlock(Tms32031State& cpu, MemoryBus& bus) {
    // 07998  PUSH AR0, AR2, AR3, R0
    const uint32_t ar0 = cpu.ar[0], ar2 = cpu.ar[2], ar3 = cpu.ar[3], r0 = cpu.r[0];
    if (!linkingOff(cpu, bus)) {
        // 0799F  R0 = RC; AR3 = buffer + [C8D3]; R0 += [C8D3] + 1 (novo tamanho)
        cpu.r[0] = cpu.rc;
        const uint32_t length = bus.readWord(cpu.direct(link_vars::kTxLength));
        cpu.ar[3] = bus.readWord(cpu.direct(link_vars::kTxBufferPtr)) + length;
        cpu.r[0] += length + 1;
        if (static_cast<int32_t>(cpu.r[0]) <= static_cast<int32_t>(kMaxWords)) {
            // 079A6  [C8D3] = R0; RPTB (RC + 1 vezes): *AR3++ = *AR2++
            bus.writeWord(cpu.direct(link_vars::kTxLength), cpu.r[0]);
            cpu.rs = 0x79A8;
            cpu.re = 0x79A9;
            for (;;) {
                cpu.r[0] = bus.readWord(cpu.ar[2]++);
                bus.writeWord(cpu.ar[3]++, cpu.r[0]);
                if (static_cast<int32_t>(--cpu.rc) < 0) break;
            }
        }
    }
    // 079AA  POP R0, AR3, AR2, AR0 (RC não é restaurado)
    cpu.r[0] = r0;
    cpu.ar[3] = ar3;
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
}

void buildStatusMessage(Tms32031State& cpu, MemoryBus& bus) {
    auto put = [&](uint32_t v) { bus.writeWord(cpu.ar[2]++, v); };
    // 07A41  AR2 = rascunho; tipo 1
    cpu.ar[2] = bus.readWord(cpu.direct(kScratchPtr));
    cpu.r[0] = 1;
    put(cpu.r[0]);
    // 07A44  [C8C5] em 4 bytes (LSB primeiro; o envio usa só o byte baixo)
    cpu.r[0] = bus.readWord(cpu.direct(kStatusMode));
    put(cpu.r[0]);
    for (int i = 0; i < 3; ++i) {
        cpu.r[0] >>= 8;
        put(cpu.r[0]);
    }
    // 07A4C  [C8DA] em 2 bytes
    cpu.r[0] = bus.readWord(cpu.direct(kVarC8DA));
    put(cpu.r[0]);
    cpu.r[0] >>= 8;
    put(cpu.r[0]);
    // 07A50  [C8E4], [E607], [C8E8]
    for (uint32_t addr : {kVarC8E4, kVarE607, kVarC8E8}) {
        cpu.r[0] = bus.readWord(cpu.direct(addr));
        put(cpu.r[0]);
    }
    // 07A56  FIX([E639] × 100.0); STI *AR2--(10) volta AR2 ao início
    cpu.r[0] = tmsFix(tmsToDouble(bus.readWord(cpu.direct(kFloatE639))) * 100.0);
    bus.writeWord(cpu.ar[2], cpu.r[0]);
    cpu.ar[2] -= 10;
    // 07A5A  RC = 10; BR $7998 (anexa as 11 palavras)
    cpu.rc = 0x000A;
    appendBlock(cpu, bus);
}

void beginPacket(Tms32031State& cpu, MemoryBus& bus) {
    // 07989  PUSH R0; [C8D3] = 0; CALL $7A41; POP R0
    const uint32_t r0 = cpu.r[0];
    cpu.r[0] = 0;
    bus.writeWord(cpu.direct(link_vars::kTxLength), cpu.r[0]);
    buildStatusMessage(cpu, bus);
    cpu.r[0] = r0;
}

void endPacket(Tms32031State& cpu, MemoryBus& bus) {
    // 0798F  PUSH R0, AR0, AR2; AR2 = 0; CALL $79AF; POP
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0], ar2 = cpu.ar[2];
    cpu.ar[2] = 0;
    appendWord(cpu, bus);
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
    cpu.r[0] = r0;
}

void registerPacketRoutines(SubroutineDispatcher& d) {
    d.add(0x7989, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { beginPacket(c, b); });
    d.add(0x798F, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { endPacket(c, b); });
    d.add(0x7998, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { appendBlock(c, b); });
    d.add(0x79AF, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { appendWord(c, b); });
    d.add(0x7A41, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { buildStatusMessage(c, b); });
}

} // namespace rea::core::link
