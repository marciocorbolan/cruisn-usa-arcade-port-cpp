/**
 * @file service_ctx.cpp
 * @brief Esperas ocupadas do menu de serviço.
 */
#include "core/service/service_ctx.hpp"

#include <algorithm>
#include <cstdlib>

namespace rea::core::service {

void Ctx::burn(uint64_t n) {
    while (n > 0) {
        const uint32_t step = static_cast<uint32_t>(std::min<uint64_t>(n, 1u << 30));
        if (!wait_.spin(cpu, step)) throw Aborted{};
        n -= step;
    }
}

uint64_t Ctx::drawCycles(uint32_t addr) const {
    auto span = [](uint32_t a, uint32_t b) {
        return static_cast<uint64_t>(std::llabs(static_cast<long long>(s32(a)) - s32(b))) + 1;
    };
    switch (addr) {
    case 0xA7F4: {
        uint64_t chars = 0;
        for (uint32_t a = cpu.ar[2]; chars < 256; ++a) {
            const uint32_t w = bus.readWord(a);
            int k = 0;
            for (; k < 4 && ((w >> (8 * k)) & 0xFFu) != 0; ++k) ++chars;
            if (k < 4) break;
        }
        return chars * cycles::kTextChar;
    }
    case 0xAC23:
        return std::max(span(cpu.ar[2], cpu.r[3]), span(cpu.r[2], cpu.rc)) * cycles::kLinePixel;
    case 0xABFC:
        return 2 * (span(cpu.ar[2], cpu.r[3]) + span(cpu.r[2], cpu.rc)) * cycles::kLinePixel;
    case 0xA7E5:
        return span(cpu.ar[2], cpu.r[3]) * span(cpu.r[2], cpu.rc) * cycles::kRectPixel;
    case 0xA824:
        return 25;
    case 0x8E79:
        return 262144;
    case 0x8E7D:
        return 131072;
    case 0x8E98:
        return static_cast<uint64_t>(cpu.r[3]) + 1;
    default:
        return 0;
    }
}

void Ctx::waitVblank() {
    // 04EC7  R0 = [C930]; repete enquanto [C930] == R0
    const uint32_t r0 = rd(vars::kFrameCounter);
    while (rd(vars::kFrameCounter) == r0) burn(cycles::kPoll);
    cpu.r[0] = r0;
}

void Ctx::waitEnterDown() {
    // 005F6  repete enquanto [C8C9] & 0x10 (ENTER solta)
    for (;;) {
        cpu.r[0] = rd(vars::kKeys);
        if ((cpu.r[0] & keys::kEnter) == 0) return;
        burn(cycles::kPoll);
    }
}

void Ctx::waitEnterUp() {
    // 005FA  repete enquanto ([C8C9] & 0x10) == 0 (ENTER apertada)
    for (;;) {
        cpu.r[0] = rd(vars::kKeys);
        if (cpu.r[0] & keys::kEnter) return;
        burn(cycles::kPoll);
    }
}

void Ctx::waitReleased(uint32_t mask) {
    for (;;) {
        cpu.r[0] = rd(vars::kKeys) & mask;
        if (cpu.r[0] == mask) return;
        burn(cycles::kPoll);
    }
}

} // namespace rea::core::service
