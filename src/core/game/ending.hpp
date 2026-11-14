#pragma once
/**
 * @file ending.hpp
 * @brief Encerramento depois da última etapa (`0x8481`–`0x863B`) e os 4 painéis que abrem e fecham a
 *        tela (`0xB07B`–`0xB07F` → `0xC100B1`–`0xC1019D`).
 *
 * @details `0x8481` roda como chamada local do pós-corrida (`[B101]`, volta em `[B102]` = `0x3BF3`)
 * quando o item 0x24 = 1:
 * - câmera `[0x45]` em (0, −600, −12000); paletas `[B14D..B14F]`, gráficos `[B150..B152]` (`0xA3C2`),
 *   cenário `[B153]` (`0x7309`, objeto em `[E5FB]`) e as duas linhas de texto `[847F]`/`[8480]`;
 * - processos `[B154]` = `0x8632` (legendas, ID 0x1234), `[B156]` = `0x853F` (volume do som 0xBA pela
 *   distância ao objeto `[E5FC]`, ID 0x1235) e `[B157]` = `0x855F` (sequência de sons, ID 0x1234);
 * - `0x858C` percorre a lista `[0x40]` e anima os objetos 0x6D0/0x621/0x622/0x623/0x615 (`0x860F`) e
 *   0x616 (`0x861C`) com as tabelas de modelos `[435C]`, `[85AA]`, `[85B2]`, `[85BA]`, `[85C2]`, `[85CD]`;
 * - a câmera se aproxima por 181 quadros, espera 300, fecha os painéis (`0xB075`), recua por 101
 *   quadros, descarrega tudo e volta (`0x853B`).
 *
 * Painéis: `0xB07E` (`0xC1014F`) cria 4 objetos (`[B06B..B06E]`, flags `[B136..B139]`) em
 * `*+AR7(C..F)`; `0xC100B1`/`0xC100B3` (fator 1.9/1.1, de `0xB07B`/`0xB07C` = `[B133]`) os afastam
 * girando por 65 quadros e os soltam; `0xC1010A` (`0xB07D` = `[B134]`) os traz de volta por 101 quadros.
 * Ambos ligam `[E5F8]` no fim. `0xB07F` (`0xC10194`) copia RC + 1 palavras de AR0 para AR1.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x8481`, `0x853F`, `0x855F`, `0x858C`, `0x85D4`…`0x8608`, `0x860F`, `0x861C`,
///        `0x8632`, `0x863B`, `0xB075`, `0xB07B`–`0xB07F`, `0xC100B1`, `0xC100B3`, `0xC1010A`,
///        `0xC1014F`, `0xC10194` e os pontos de retomada.
void registerEnding(SubroutineDispatcher& d);

} // namespace rea::core::game
