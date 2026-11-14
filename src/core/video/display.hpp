#pragma once
/**
 * @file display.hpp
 * @brief Composição da imagem exibida a partir da VRAM e da paleta (equivalente ao `screen_update` do MAME).
 */

#include <cstdint>
#include <vector>

#include "core/memory_bus.hpp"

namespace rea::core::video {

/// Área visível padrão do Cruis'n USA (tabela de timing da ROM: 512 × 400).
inline constexpr int kVisibleWidth = 512;
inline constexpr int kVisibleHeight = 400;  ///< Linhas visíveis.

/**
 * @brief Converte a página exibida em RGBA8888 (0xRRGGBBAA).
 * @details Usa a página indicada pelo bit 0 de `[0x980040]` (0 → 0x900000,
 *          1 → 0x940000). Cada pixel `p & 0x7FFF` indexa a paleta, e cada
 *          entrada tem 5 bits por canal, expandidos como no `pal5bit` do MAME
 *          (`(c << 3) | (c >> 2)`).
 * @param out Redimensionado para `width × height`.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param width Largura da imagem gerada, em pixels.
 * @param height Altura da imagem gerada, em pixels.
 */
void composeDisplay(const MemoryBus& bus, std::vector<uint32_t>& out, int width = kVisibleWidth,
                    int height = kVisibleHeight);

} // namespace rea::core::video
