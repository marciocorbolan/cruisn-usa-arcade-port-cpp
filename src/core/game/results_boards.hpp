#pragma once
/**
 * @file results_boards.hpp
 * @brief Placares da tela de resultados, criados pelo `0x3A9B`: velocidade média (`0x3DCF`), recorde
 *        da pista (`0x3E4F`), tempo da etapa (`0x3ED8`), posição (`0x3F01`), aviso de recorde
 *        (`0x3E82`/`0x3E87`) e o letreiro do vencedor do link (`0x39CF`).
 *
 * @details Os três placares de texto usam o mesmo deslize (`0x3DFE`): rótulo `*+AR7(16/17)` entra da
 * esquerda até x = 250 (10 % por quadro, passo mínimo 8) e o valor `*+AR7(18/19)` da direita até
 * x = 260 (20 %, passo até −12), em 32 quadros; ficam até `[E483]` ≠ 0, tocam 0x7E e saem (rótulo para
 * −20, valor para 532). Texto da frente em x, sombra em x + 3.
 * - Velocidade média = `[E47E]` (odômetro, ×1,6665 com o item 0x22) / `[E608]`; zera os dois.
 * - Recorde: iniciais (`0x9AD2`/`0x9B06`) + " " + tempo (`0x6099`).
 * - Tempo da etapa: `[E85F]` (ou `[B119]` no fim de tempo); `[E4BC]` = tempo, `[E85F]` = 0.
 * - Posição: as duas imagens de `[B1A5]` descem de y = −60 a 78 (20 %), ficam e sobem.
 * - Aviso de recorde: `[B114]`/`[B115]` sobe de y = 450 a 150 piscando (30 de 35 quadros) e desce.
 * - `0x39CF` (só no link, se `[C8E2]` ≥ posição): letreiro `[B0D8]` (`0x39BF`) e três objetos que
 *   piscam (z ± 5000) até `[E53B]`; depois são devolvidos à lista livre (`0x3A2C`).
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x3DCF`, `0x3E4F`, `0x3ED8`, `0x3F01`, `0x3E82`, `0x3E87`, `0x3ECC`, `0x39CF`,
///        `0x39BF`, `0x3A2C` e as retomadas.
void registerResultsBoards(SubroutineDispatcher& d);

} // namespace rea::core::game
