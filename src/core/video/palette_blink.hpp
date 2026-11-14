#pragma once
/**
 * @file palette_blink.hpp
 * @brief Processos de pisca de paleta: `0x1F2A` (alterna 5 cores de um slot de textura) e `0x1F0F`
 *        (`[B0D6]`: cicla a paleta 0x5F pela tabela `[B231]` = 0x1F23, pares fonte/duração, −1 recomeça).
 *
 * @details Pega o slot da textura 0x2E (`0x9ECD`; sem slot, morre) e, em laço, envia 5 cores
 * (formato 15 bits) da tabela `[1F3C]` para `0x9E0000 + slot + 0xFB` (`0x9F5C`, cabeçalho 5).
 * Cada entrada da tabela tem 5 cores seguidas da duração em quadros; −1 volta ao início.
 * Na ROM a tabela tem 4 passos: 0x6B40/0x7FE0 (amarelo escuro/claro) alternando as lâmpadas,
 * com 4 e 2 quadros.
 */

#include "core/subroutines.hpp"

namespace rea::core::video {

/// @brief Registra `0x1F2A`, `0x1F0F` e as retomadas `0x1F11`, `0x1F22` e `0x1F31`.
void registerPaletteBlink(SubroutineDispatcher& d);

} // namespace rea::core::video
