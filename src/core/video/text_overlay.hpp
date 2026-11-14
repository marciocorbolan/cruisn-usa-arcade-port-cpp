#pragma once
/**
 * @file text_overlay.hpp
 * @brief Textos 2D sobrepostos (`0x774A`, `0x7720`): fonte em textura desenhada
 *        com quads de DMA.
 *
 * @details O loop principal chama a `0x774A` a cada quadro. Ela percorre a
 * lista de textos `[[0x763F]]` (cabeça em 0xE91A); cada nó tem:
 *
 * | Offset | Campo |
 * |---|---|
 * | 0 | próximo |
 * | 1 | string empacotada (4 caracteres por palavra, byte baixo primeiro, 0 = fim) |
 * | 3, 4 | x, y (float) |
 * | 5, 6 | velocidade em x, y (float; somada a cada quadro se `[E91C]` = 0) |
 * | 7 | quadros de vida |
 * | 8 | flags: 0x100 centraliza, 0x200 alinha à direita, 0x400 permanente, 0x800 modo `0x0D00` \| cor (senão `0x0900`) |
 * | 9 | altura do glyph (pixels e linhas de textura) |
 * | 0xA | fonte: 4 palavras por glyph a partir de '0' |
 * | 0xB | página de textura |
 * | 0xC | base de paleta |
 *
 * **Glyph** (4 palavras): `g0` = deslocamento x (16 bits baixos, com sinal) e
 * espaço extra (16 bits altos); `g1`/`g2` = u esquerdo/direito; `g3` = v.
 * O avanço é `g2 − g1 + (g0 >> 16)`. O caractere '/' vira '@', e o espaço
 * repete o último avanço.
 *
 * Cada caractere vira um quad no modelo `[0x48]` (= 0xEA34), enviado pela
 * `0xAED6`. Quando a vida acaba (sem a flag 0x400), o nó volta para a lista
 * livre `[[0x763E]]` e `[E91B]` é incrementado.
 */

#include "core/subroutines.hpp"

namespace rea::core::video {

/// Variáveis dos textos sobrepostos.
namespace text_vars {
inline constexpr uint32_t kListPtr   = 0x763F; ///< `[0x763F]` = 0xE91A (célula da cabeça).
inline constexpr uint32_t kFreePtr   = 0x763E; ///< `[0x763E]` = 0xE919 (lista livre).
inline constexpr uint32_t kFreeCount = 0xE91B;  ///< Nós de texto livres.
inline constexpr uint32_t kFrozen    = 0xE91C; ///< != 0: não move os textos.
inline constexpr uint32_t kQuadMode  = 0xEA34; ///< Palavra 0 do modelo `[0x48]`.
inline constexpr uint32_t kVolumeTimer  = 0xE56D; ///< Quadros restantes da indicação de volume (`0x7594`).
inline constexpr uint32_t kVolumeDigits = 0xB122; ///< `[B122]`: buffer do número do volume.
inline constexpr uint32_t kVolumeLabel  = 0xB123; ///< `[B123]`: rótulo do volume.
} // namespace text_vars

/// @brief `0x7720`: R0 = largura em pixels da string AR2 com a fonte do nó AR4. Preserva RS, R1, R2 e AR0–AR2.
void textWidth(Tms32031State& cpu, MemoryBus& bus);

/// @brief `0x774A`: desenha, move e envelhece os textos. Preserva AR4, AR5, R4 e R7.
void drawTextOverlays(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x7660`: cria um texto (`0x8F01` da lista livre `[763E]` para `[763F]`; `[E91B]` −= 1).
 * @details AR2 = string, R2/R3 = x/y (float), RC = vida. Velocidade 0, flags 0 e o estilo
 *          `0x76F0` (altura 0x11, página `[7644]`, paleta `[C6CC]`, fonte `[7645]`).
 *          Como no original, não testa o carry: com o pool vazio, escreve no AR0 anterior.
 *          Preserva R2; AR2 sai com o argumento da paleta do estilo (`[C6CC]`). `0x765F` = o mesmo com RC = 1.
 */
void createText(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x7651`: texto com sombra: AR1 = frente; AR0 = sombra (flags 0x800, x + 2, y + 2).
void createShadowedText(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/**
 * @brief `0x7594`: indicação de volume enquanto `[E56D]` > 0 (fora do modo 1).
 * @details Número FIX(item 8 da CMOS × 0,117645) em (180, 310) e o rótulo `[B123]` em (60, 310),
 *          ambos com sombra (`0x7651`) e a paleta `[B120]` (`0x7450`).
 */
void showVolume(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief Registra `0x7720`, `0x774A`, `0x7660`, `0x765F`, `0x7651`, os estilos
 *        `0x7676`–`0x771F`, `0x7450` e `0x7594`.
 * @details Estilos (simples AR0 / duplo AR0 + AR1): altura `+9`, página `+0xB`, paleta `+0xC`, fonte `+0xA`.
 *
 * | Simples | Duplo | Altura | Página | Paleta (rotina) | Fonte |
 * |---|---|---|---|---|---|
 * | `0x7676` | `0x7680` | 0x0C | `[7643]` | `[C6C4]`/`[C6C5]` (`0x9ECD`) | `[B087]` |
 * | `0x768E` | `0x7698` | 0x16 | `[7643]` | `[C6C6]`/`[C6C7]` (`0x9ECD`) | `[B086]` |
 * | `0x76A7` | `0x76B1` | 0x28 | `[76A6]` | `[C6C8]`/`[C6C9]` (`0x9ECD`) | `[B088]` |
 * | `0x76BF` | `0x76C9` | 0x2A | `[7646]` | `[C6CA]`/`[C6CB]` (`0x9ECD`) | `[B089]` |
 * | `0x76D8` | `0x76E2` | 0x0C | `[7647]` | 0x42 (`0x9ECD`) | `[76D7]` |
 * | `0x76F0` | `0x76FA` | 0x11 | `[7644]` | `[C6CC]`/`[C6CD]` (`0x9ECD`) | `[7645]` |
 * | `0x7708` | `0x7712` | 0x06 | `[7641]` | `[C6CE]`/`[C6CF]` (`0x9EDB`) | `[7642]` |
 */
void registerTextOverlay(SubroutineDispatcher& d);

} // namespace rea::core::video
