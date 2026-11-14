#pragma once
/**
 * @file texture_pack.hpp
 * @brief Pacote de texturas externas (re-skin) e exportação da RAM de texturas como BMP de 8 bits.
 *
 * @details No V-Unit a RAM de texturas (`0xA00000`–`0xBFFFFF`, 4 MB) guarda **índices de paleta** de 8 bits: o quad
 * soma o texel à base de paleta (`pixdata`) e a cor só é resolvida na exibição, pela paleta `0x9E0000`. Por isso o
 * re-skin é indexado, fiel ao hardware:
 * - a RAM é vista como 64 páginas de 256 × 256 texels (64 KB cada; a linha `v` avança 256 bytes, como no
 *   endereçamento `texbase + v·256 + u` do renderizador);
 * - `assets/textures/page_NN.bmp` (NN = 00..63, decimal), BMP **8 bits sem compressão, 256 × 256**, substitui a
 *   página NN inteira: o renderizador lê o texel do arquivo em vez da RAM (a RAM continua sendo escrita pelo jogo);
 * - `--dump-textures dir` exporta as páginas não vazias com a paleta usada por último em cada página (aproximação:
 *   uma página pode servir a vários quads com bases de paleta diferentes). Edite preservando a paleta/índices.
 */

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/memory_bus.hpp"

namespace rea::core::video {

/// @brief Páginas de textura externas (`assets/textures/page_NN.bmp`) que substituem a RAM de texturas no desenho.
class TexturePack {
public:
    static constexpr int kPageSide = 256;  ///< Lado de uma página de textura, em texels.
    static constexpr std::size_t kPageBytes = static_cast<std::size_t>(kPageSide) * kPageSide;  ///< Bytes por página (256 × 256 texels de 8 bits).
    static constexpr int kPages = 64;  ///< 4 MB / 64 KB

    /**
     * @brief Carrega todas as `page_NN.bmp` de @p dir.
     * @return Número de páginas carregadas (arquivos inválidos são ignorados e listados em rejected()).
     */
    int loadDir(const std::filesystem::path& dir);

    /// @return Nomes das `page_NN.bmp` do último loadDir() que existiam mas não eram BMP 8 bits 256 × 256.
    const std::vector<std::string>& rejected() const { return rejected_; }

    /// @brief Carrega um BMP 8 bits 256 × 256 como a página @p page. @return false se o arquivo for inválido.
    bool loadPage(int page, const std::filesystem::path& file);

    /// @return true se a página @p page veio de um arquivo externo.
    bool hasPage(int page) const { return page >= 0 && page < kPages && pages_[static_cast<std::size_t>(page)]; }
    /// @return true se nenhuma página foi carregada.
    bool empty() const { return count_ == 0; }

    /// @brief Texel no deslocamento @p byte_offset da RAM de texturas, se a página for externa.
    bool texel(std::size_t byte_offset, uint8_t& out) const {
        const std::size_t p = byte_offset / kPageBytes;
        if (p >= static_cast<std::size_t>(kPages) || !pages_[p]) return false;
        out = (*pages_[p])[byte_offset % kPageBytes];
        return true;
    }

    /// @brief Nome do arquivo da página (`page_NN.bmp`).
    static std::string pageName(int page);

    /**
     * @brief Escreve um BMP 8 bits 256 × 256 (linhas de baixo para cima, campos little-endian).
     * @param pixels 65 536 índices, linha 0 = v 0.
     * @param rgb 256 cores 0xRRGGBB.
     * @param file Arquivo BMP a gravar.
     */
    static bool writeBmp8(const std::filesystem::path& file, const uint8_t* pixels, const std::array<uint32_t, 256>& rgb);

    /**
     * @brief Exporta as páginas não vazias da RAM de texturas como `page_NN.bmp` em @p dir.
     * @param paletteBase Base de paleta de cada página (ex.: `DmaRenderer::lastPalette`); cor i = paleta[base + i].
     * @param bus Barramento de memória simulado (RAM, ROM e I/O).
     * @param dir Pasta de destino dos BMP (`page_NN.bmp`).
     * @return Número de páginas escritas.
     */
    static int dumpRam(const MemoryBus& bus, const std::function<uint16_t(int)>& paletteBase,
                       const std::filesystem::path& dir);

private:
    std::array<std::unique_ptr<std::vector<uint8_t>>, kPages> pages_{};  ///< Páginas carregadas (nullptr = usa a RAM de texturas).
    int count_ = 0;  ///< Quantas páginas foram carregadas.
    std::vector<std::string> rejected_;  ///< Arquivos recusados no último loadDir().
};

} // namespace rea::core::video
