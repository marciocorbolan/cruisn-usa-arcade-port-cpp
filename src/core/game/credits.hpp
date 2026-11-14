#pragma once
/**
 * @file credits.hpp
 * @brief Créditos finais (`0xA179`–`0xA2AA`), chamada local do pós-corrida depois das 14 etapas
 *        (`[B105]` = `0xA1F1`, volta em `[B106]` = `0x3C16`).
 *
 * @details `0xA1F1` zera a câmera, cria `[B148]` = `0xA1A1` (anel de 11 objetos `[B144]` girando atrás do
 * texto) e `[B149]` = `0xA179` (devolve à lista livre os textos que passaram de y = −25), todos com ID
 * 0xFFFF88D0, e interpreta o roteiro `[A2AB]`. Cada linha nasce em y = R6 (R6 cresce do espaçamento
 * `[E5F9]` e desce 1 por quadro até 402) com velocidade −1 e o estilo `[E5FA]`:
 * | Comando | Ação |
 * |---|---|
 * | 10, estilo | `[E5FA]` = estilo, espaçamento 10 |
 * | 8, x, texto | linha sem centralizar |
 * | 7, x, texto | alinhada à direita (0x200) |
 * | 2, x, a, x, b | duas linhas na mesma altura, a segunda à direita |
 * | 3, x, texto | centralizada; `[B14C]` = `0xA26D` troca o texto por `[A26C]` ao chegar a y = 250 |
 * | 99, x, texto | x = −344, centralizada; `[B14B]` = `0xA24C` a traz até x = 256/259 |
 * | 4 | congela os textos (`[E91C]`) por 60 quadros |
 * | 0 | linha em branco |
 * | −1 | mata o ID 0xFFFF88D0 e volta |
 * | outro, x, texto | centralizada (0x100) |
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0xA179`, `0xA1A1`, `0xA1E4`, `0xA1F1`, `0xA24C`, `0xA26D`, `0xA295`, `0xA29D`,
///        `0xA2A4` e os pontos de retomada.
void registerCredits(SubroutineDispatcher& d);

} // namespace rea::core::game
