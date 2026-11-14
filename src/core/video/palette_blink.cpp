/**
 * @file palette_blink.cpp
 * @brief Tradução de `0x1F0F`–`0x1F22` e `0x1F2A`–`0x1F3B`.
 */
#include "core/video/palette_blink.hpp"

namespace rea::core::video {

namespace {

constexpr uint32_t kTablePtr = 0x1F3C; ///< `[1F3C]` = 0x1F3D: duração inicial seguida dos passos.

/// 01F31  envia as 5 cores de AR4 e dorme pela duração do passo. AR4 = passo, AR6 = slot.
void step(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    const uint32_t ar4 = c.ar[4], ar6 = c.ar[6];
    c.ar[2] = ar4;
    c.r[2] = ar6 + 0xFB;
    c.r[3] = 5;
    s.call(0x9F5C, c, b);
    c.ar[6] = ar6;
    uint32_t p = ar4 + 5;                  // LDI *++AR4(5),R0
    if (static_cast<int32_t>(b.readWord(p)) < 0) p = b.readWord(kTablePtr);
    c.ar[2] = b.readWord(p);
    c.ar[4] = p + 1;
    c.ret = 0x1F31;
    s.call(0xA874, c, b);
}

/// 01F2A  entrada.
void entry(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.ar[4] = b.readWord(kTablePtr) + 1;
    const uint32_t ar4 = c.ar[4];
    c.ar[2] = 0x2E;
    s.call(0x9ECD, c, b);
    if (c.st & Tms32031State::kStCarry) return s.call(0xA896, c, b);
    c.ar[4] = ar4;
    c.ar[6] = c.r[0];
    step(c, b, s);
}

/// 01F12  laço do pisca da paleta 0x5F: some se o slot não existir; senão envia a cor do passo
///        (`*AR5`, 10 palavras, cabeçalho 0x8000000A) e dorme pela duração seguinte. −1 recomeça em `[B232]`.
void cycleLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.ar[2] = 0x5F;
    s.call(0x9ECD, c, b);
    if (c.st & Tms32031State::kStCarry) return s.call(0xA896, c, b);
    c.ar[2] = b.readWord(c.ar[5]++);
    if (c.ar[2] == 0xFFFFFFFFu) {
        c.ar[5] = b.readWord(0xB232);
        c.ar[2] = b.readWord(c.ar[5]++);
    }
    c.r[2] = c.r[0];
    c.r[3] = 0x8000000Au;                  // LDI −8000h / LSH 16 / OR 0Ah
    s.call(0x9F5C, c, b);
    c.ar[2] = b.readWord(c.ar[5]++);
    c.ret = 0x1F22;
    s.call(0xA874, c, b);
}

} // namespace

void registerPaletteBlink(SubroutineDispatcher& d) {
    d.add(0x1F2A, entry);
    d.add(0x1F31, step);
    // 01F0F  processo `[B0D6]`: espera 20 quadros e percorre a tabela `[B231]` (fonte, duração)
    d.add(0x1F0F, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[2] = 0x14;
        c.ret = 0x1F11;
        s.call(0xA874, c, b);
    });
    d.add(0x1F11, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[5] = b.readWord(0xB231);
        cycleLoop(c, b, s);
    });
    d.add(0x1F22, cycleLoop);           // BU 1F12
}

} // namespace rea::core::video
