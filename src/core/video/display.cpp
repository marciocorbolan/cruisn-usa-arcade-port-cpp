/**
 * @file display.cpp
 * @brief Composição da página exibida.
 */
#include "core/video/display.hpp"

#include "core/vunit_hw.hpp"

namespace rea::core::video {

namespace {
/// Expande um canal de cor de 5 bits para 8 bits (repete os 3 bits altos embaixo).
inline uint32_t pal5(uint32_t c) { return ((c & 0x1Fu) << 3) | ((c & 0x1Fu) >> 2); }
} // namespace

void composeDisplay(const MemoryBus& bus, std::vector<uint32_t>& out, int width, int height) {
    out.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    const uint32_t base = (bus.readWord(vunit::kPageControl) & 1u) ? vunit::kVideoPageWords : 0u;
    std::size_t o = 0;
    for (int y = 0; y < height; ++y) {
        const uint32_t row = base + static_cast<uint32_t>(y) * vunit::kVideoStride;
        for (int x = 0; x < width; ++x) {
            const uint32_t pen = bus.videoWord(row + static_cast<uint32_t>(x)) & 0x7FFFu;
            const uint32_t c = bus.paletteWord(pen);
            out[o++] = pal5(c >> 10) << 24 | pal5(c >> 5) << 16 | pal5(c) << 8 | 0xFFu;
        }
    }
}

} // namespace rea::core::video
