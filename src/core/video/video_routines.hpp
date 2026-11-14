#pragma once
/**
 * @file video_routines.hpp
 * @brief Primitivas de vídeo da ROM: páginas, preenchimentos, pixel, texto e números.
 *
 * @details **VRAM** (`0x900000`–`0x97FFFF`, 16 bits por palavra):
 * - duas páginas de 512 × 512 pixels: página 0 em 0x900000, página 1 em 0x940000;
 * - cada pixel é um índice de 15 bits na paleta (`0x9E0000`, formato xRRRRRGGGGGBBBBB).
 *
 * **Controle de página** (`[0x980040]`, MAME `page_control`):
 * - bit 0: página exibida;
 * - bit 2: página onde o DMA de polígonos desenha.
 *
 * O software desenha na página cujo endereço está em `[CE13]`.
 *
 * | Rotina | Efeito |
 * |---|---|
 * | `0x8E4E` | desenha na página 1 (`[CE13]` = 0x940000), exibe a 0 (`bit0 = 0`, `bit2 = 1`); com `[8E64] != 0` vai para `0x8E65` |
 * | `0x8E59` | desenha na página 0, exibe a 1; com `[8E64] != 0` vai para `0x8E65` |
 * | `0x8E65` | `[CE13]` = 0x940000 e `page |= 5` (exibe e desenha na página 1) |
 * | `0x8E79` | limpa as duas páginas (cor 0) |
 * | `0x8E7D` | limpa a página 0 |
 * | `0x8E88` / `0x8E8D` | limpa as últimas 0x400 palavras (linhas 504–511) da página 0 / 1 |
 * | `0x8E92` | preenche a página 0 com `[8E97]` |
 * | `0x8E98` | preenche RC + 1 palavras a partir de AR2 com R2 (R3 = contagem − 1) |
 * | `0x8EB1` | zera a paleta |
 * | `0xAECC` | retângulo pelo DMA de quads (modelo `[0x48]`) |
 * | `0x8E6D` | limpa a página de render a cada quadro (se `[C940] == 0`) |
 * | `0xA824` | pixel: `[[CE13] + y·512 + x] = cor` (AR2 = x, R2 = y, R3 = cor) |
 * | `0xA7E5` | retângulo: x de AR2 a R3, y de R2 a RC, cor RS |
 * | `0xA7F4` | texto empacotado (AR2) em (R2, R3) com a cor RC: fonte 8×7 de `[0xA730]` |
 * | `0xA762` / `0xA74E` | inteiro → texto decimal (`0xA74E`: mínimo 2 dígitos) |
 * | `0xA7AC` | inteiro → texto hexadecimal |
 *
 * As escritas na VRAM são cercadas por `[0x808064] = 0x1000` (estados de
 * espera do barramento) e `0x470` (valor normal), com uma leitura descartada do
 * endereço 0. O port reproduz essas escritas.
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::video {

/// Variáveis e constantes de vídeo.
namespace video_vars {
inline constexpr uint32_t kDrawBase       = 0xCE13; ///< Endereço da página de desenho do software.
inline constexpr uint32_t kPageTable      = 0x8E49; ///< 0x93F000, 0x97FC00, 0x900000, 0x940000, 0x3FFFF.
inline constexpr uint32_t kPage0Ptr       = 0x8E4B;  ///< `[0x8E4B]` = 0x900000: início da página 0 da VRAM.
inline constexpr uint32_t kPage1Ptr       = 0x8E4C;  ///< `[0x8E4C]` = 0x940000: início da página 1.
inline constexpr uint32_t kPageWordsM1Ptr = 0x8E4D;  ///< `[0x8E4D]` = 0x3FFFF: palavras por página − 1.
inline constexpr uint32_t kForcePage1     = 0x8E64; ///< != 0: as trocas mantêm a página 1.
inline constexpr uint32_t kFillColorPtr   = 0x8E97;  ///< Palavra com a cor dos preenchimentos.
inline constexpr uint32_t kFillCountPtr   = 0x8E96;  ///< Palavra com a contagem dos preenchimentos.
inline constexpr uint32_t kFontPtr        = 0xA730; ///< `[0xA730]` = 0xC122A8: 7 palavras por glyph, a partir de '('.
inline constexpr uint32_t kFillIeMask     = 0x99DF; ///< IE durante os preenchimentos (DP = 0x80).
inline constexpr uint32_t kBusWait        = 0x1000;  ///< Wait states do barramento durante os preenchimentos da VRAM.
inline constexpr uint32_t kBusNormal      = 0x0470;  ///< Wait states normais do barramento.
inline constexpr uint32_t kQuadTemplatePtr = 0x0048; ///< `[0x48]` = 0xEA34: modelo de 19 palavras do quad da `0xAECC`.
inline constexpr uint32_t kFirstGlyph     = 0x28;  ///< Primeiro caractere da fonte ('(').
inline constexpr uint32_t kGlyphRows      = 7;  ///< Linhas por caractere da fonte.
} // namespace video_vars

/// @brief `0x8E98`: `*AR2++ = R2`, R3 + 1 vezes. Preserva R0, AR1, AR2, IE e DP; RC = −1.
void fillWords(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0xA824`: grava um pixel. Preserva AR1, AR2, R2 e IE; DP = 0; liga o GIE (`RETIU` em `0xA731`).
void plotPixel(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0xA7E5`: retângulo preenchido (inclusive) com a cor RS.
void fillRect(Tms32031State& cpu, MemoryBus& bus);
/**
 * @brief `0xA7F4`: desenha texto.
 * @details Lê 4 caracteres por palavra (LSB primeiro) até o byte 0.
 *          - espaço avança 8 pixels;
 *          - os demais usam o glyph `(c − 0x28)` de 7 linhas: cada linha é um
 *            byte, bit 7 à esquerda.
 *
 *          Saída: R2 = x depois do texto; AR2 = palavra com o byte 0.
 *          Preserva R4 e R5.
 */
void drawText(Tms32031State& cpu, MemoryBus& bus);
/**
 * @brief `0xA762` (`pad2 = false`) e `0xA74E` (`pad2 = true`): R2 → texto decimal em AR2.
 * @details
 *  - Negativos começam com o caractere `>` (o sinal de menos da fonte).
 *  - Com `pad2`, valor absoluto ≤ 9 ganha um `0` à esquerda; o zero vira "00".
 *    Com valor absoluto > 9, a `0xA74E` usa o AR7 do chamador como indicador
 *    (só gera o `0` extra se AR7 == 1), como no original.
 *  - Saída: R0 = bits ocupados na última palavra (0 = a palavra inteira é o terminador).
 *  - Preserva os demais registradores.
 */
void formatDecimal(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, bool pad2);
/// @brief `0xA7AC`: R2 → texto hexadecimal (maiúsculas) em AR2. R0 = bits ocupados na última palavra.
void formatHex(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0xAECC`: desenha um retângulo pelo DMA de quads.
 * @details Entrada: x de AR2 a R3, y de R2 a RC, cor base RS, página de textura RE.
 *          O modo (palavra 0) e as coordenadas de textura vêm do modelo `[0x48]`.
 *          Grava os vértices no modelo, espera `[0x980082] & 4 == 0`, envia
 *          15 palavras a `0x600000` (os offsets 4, 7, 10 e 13 do modelo ficam
 *          de fora) e dispara lendo `0x980083`.
 *          Preserva AR0, AR1, R0, R1, RC, RS e RE; AR3 = 0.
 */
void submitQuad(Tms32031State& cpu, MemoryBus& bus);
/**
 * @brief `0xAED6`: envia o modelo de quad `[0x48]` à fila (a 2ª metade da `0xAECC`).
 * @details Usada direto por quem monta o modelo por conta própria (ex.: o texto `0x774A`).
 *          Preserva AR0, AR1, R0, R1, RC, RS e RE; AR3 = 0.
 */
void sendQuadTemplate(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0x8E6D`: se `[C940] == 0`, limpa a página de render (modo 0, cor 0, 512 × 400) com a `0xAECC`.
void clearRenderPage(Tms32031State& cpu, MemoryBus& bus);

/// @brief Registra as rotinas deste módulo.
/**
 * @brief `0xAC23`: linha de (AR2, R2) a (R3, RC) com a cor RS, ponto a ponto (`0xA824`).
 * @details Vertical e horizontal vão da menor à maior coordenada. Inclinada: com |dx/dy| ≤ 1 percorre
 *          y somando dx/dy a x (`FIX` = piso); senão percorre x somando dy/dx a y. O recíproco usa
 *          `0xA585` + `RND`. Preserva R4–R7, AR3 e DP; RS fica com a ponta final em x.
 */
void drawLine(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0xABFC`: retângulo de (AR2, R2) a (R3, RC) com a cor RS: topo, base, esquerda e direita
 *        (4 chamadas da `0xAC23`). Preserva R4, R5, AR3 e DP.
 */
void drawRect(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

void registerRoutines(SubroutineDispatcher& d);

} // namespace rea::core::video
