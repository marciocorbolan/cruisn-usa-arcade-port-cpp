#pragma once
/**
 * @file dma_renderer.hpp
 * @brief Renderizador de quads do V-Unit (fila de DMA `0x600000`), equivalente ao `midvunit_renderer` do MAME.
 *
 * @details É hardware, não código da ROM. O software:
 * 1. escreve até 16 palavras em `0x600000` (`dma_queue_w`). A fila guarda só
 *    os 16 bits baixos de cada uma (`uint16_t m_dma_data[16]` no MAME), então
 *    a ROM pode mandar A e A >> 16 sem mascarar;
 * 2. lê `0x980083` (`dma_trigger_r` com offset 1), o que desenha o quad e
 *    esvazia a fila.
 *
 * `0x980082` e `0x980000` devolvem 0: a fila nunca está ocupada.
 *
 * | Palavra | Conteúdo |
 * |---|---|
 * | 0 | modo: bits 0–7 somados à cor; `& 0x300 == 0x100` = texturizado; `& 0xC00` = transparência/máscara; bit 13 = dither (pixel sim, pixel não) |
 * | 1 | base de paleta (pixdata) |
 * | 2–9 | x, y dos 4 vértices (16 bits com sinal; +0,5) |
 * | 10–13 | u (bits 0–7), v (bits 8+) de cada vértice |
 * | 14 | página de textura (×256 bytes) |
 *
 * O quad é desenhado na página do bit 2 de `[0x980040]`, recortado na área
 * visível (registradores 2, 5, 6, 7, 10 e 11 de `0x980020`), com o
 * rasterizador `poly_manager::render_polygon<4, N>` do MAME (float, centro de
 * pixel, `round_coordinate`).
 *
 * Peculiaridade preservada: com cor 0 e sem dither, o preenchimento liso usa
 * `memset` até `stopx` **inclusive**, um pixel a mais que os outros modos.
 */

#include <array>
#include <cstdint>

#include "core/memory_bus.hpp"
#include "core/video/texture_pack.hpp"

namespace rea::core::video {

/// @brief Rasterizador de quads do V-Unit: recebe a fila de DMA e desenha na VRAM (detalhes no cabeçalho do arquivo).
class DmaRenderer final : public IIoDevice {
public:
    static constexpr uint32_t kQueuePort = 0x600000;  ///< Fila de DMA: até 16 palavras por quad.
    static constexpr uint32_t kEntriesPort = 0x980000;  ///< Porta lida pela ROM antes de usar a fila (sempre 0).
    static constexpr uint32_t kTriggerPort = 0x980083;  ///< Ler esta porta dispara o desenho do quad da fila (`0x980082` só informa o status).

    /// @param bus Barramento cuja VRAM e RAM de texturas são usadas no desenho.
    explicit DmaRenderer(MemoryBus& bus) : bus_(bus) {}

    bool ioRead(uint32_t addr, uint32_t& value) override;
    void ioWrite(uint32_t addr, uint32_t value) override;

    /// @brief Desenha o quad descrito por @p dma (16 palavras), como `process_dma_queue`.
    void processQuad(const std::array<uint32_t, 16>& dma);

    /// @brief Páginas de textura externas (re-skin); nulo = só a RAM de texturas.
    void setTexturePack(const TexturePack* pack) { pack_ = (pack && !pack->empty()) ? pack : nullptr; }
    /// @brief Base de paleta (`pixdata`) do último quad texturizado que começou na página @p page (0..63).
    uint16_t lastPalette(int page) const { return last_palette_[static_cast<std::size_t>(page)]; }

    /// @return Quads desenhados desde a criação.
    uint64_t quadsDrawn() const { return quads_; }
    /// @return Pixels escritos desde a criação.
    uint64_t pixelsDrawn() const { return pixels_; }

private:
    MemoryBus& bus_;  ///< Barramento de memória simulado.
    std::array<uint32_t, 16> queue_{};  ///< Palavras recebidas do quad atual.
    uint32_t index_ = 0;  ///< Próxima posição livre na fila.
    uint64_t quads_ = 0;  ///< Quads desenhados.
    uint64_t pixels_ = 0;  ///< Pixels escritos.
    const TexturePack* pack_ = nullptr;  ///< Texturas externas, ou nullptr.
    std::array<uint16_t, TexturePack::kPages> last_palette_{};  ///< Base de paleta do último quad texturizado de cada página (para `--dump-textures`).
};

} // namespace rea::core::video
