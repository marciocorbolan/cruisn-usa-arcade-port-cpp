#pragma once
/**
 * @file scenery_collision.hpp
 * @brief Colisão do carro com objetos do cenário (`0x221F`) e os processos que animam o objeto
 *        atingido (`0x2334`, `0x239C`, `0x23B0`).
 *
 * @details **`0x221F`** (AR0 = carro, AR1 = objeto; chamada pela `0x2207` quando os círculos se
 * tocam):
 * 1. Monta os cantos da caixa do carro (`0x28A8` em `[0x4F]` = 0x809D00).
 * 2. Testa o centro do objeto contra as 4 arestas do retângulo de cima (cantos 2→6→7→3→2, tabela
 *    `[290C]`). Se algum lado der ≤ 0, o objeto está fora e nada acontece.
 * 3. Despacha pelo tipo `[AR1+0xF]`:
 *
 * | Tipo | Efeito |
 * |---|---|
 * | `& 0xF0` = 0x60 | ignorado |
 * | `& 0xFF0` = 0x750 | `[AR1+0x1B]` = 1, `0x6971` (som com intervalo mínimo), `0x6959` |
 * | `& 0xF0` = 0x30 | **arremessado** (só se \|Δy\| ≤ 250): velocidade pelo rumo do carro ± 0,1 e pela velocidade dele; processo de voo `0x2334` |
 * | `& 0xF0` = 0x20 e os demais | **tombado**: gira para o rumo do carro, sai da lista `[0x58]` (`0x23D1`), o carro perde 20 % da velocidade (mín. 10), processo `0x239C` |
 * | `& 0xFFF` = 0x615 | cria 1..3 processos `[B124]` (ID 0x615) e depois é **sólido** |
 * | `& 0xF0` = 0x40 | **sólido**: o carro é empurrado (`0x2633`), velocidade · 0,6 (mín. 37), giro curto ou rodada (5 %, só acima de 37) |
 *
 * **Processos** (herdam AR4 = objeto e R7 do criador):
 * - `0x2334` (ID 0x290): voo. Gira o objeto, soma velocidade e gravidade (`[C92F]` · 2), quica no chão
 *   (`0x1FB0`) com ½ da velocidade e som pela distância; acaba ao passar de 100 em y ou devagar no chão.
 * - `0x239C`: tomba girando R7 por quadro até 1,5 rad no total.
 * - `0x23B0`: balança: R7, depois R7 · −0,4 por 4 quadros, depois · −0,6 a cada 4 quadros até \|R7\| ≤ 0,01.
 *
 * | Rotina | Efeito |
 * |---|---|
 * | `0x23D1` | tira AR4 da lista `[0x58]`; `+0xE`: limpa o bit 31, liga o bit 27 |
 * | `0x6959` | objeto 0x750 a ≤ 30000: tipo 3 → `0x6AD3`; senão `0x6997`, posição `[E8ED]`/`[E8EE]` e `0x6A66`. Carry = 0 |
 * | `0x6971` | a cada `[C62C]` minutos: som da tabela `[C62B]` (5 palavras por tipo) |
 * | `0x6997` | `[AR1+0x21]` ≠ −1: modelo `[AR1+0xD]` da tabela `[C62B]`; `[AR4+0x21]` = −1 |
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x221F`, `0x23D1`, `0x6959`, `0x6971`, `0x6997` e os processos `0x2334`/`0x2380`,
///        `0x239C`/`0x23AC`, `0x23B0`/`0x23B9`/`0x23C4`.
void registerSceneryCollision(SubroutineDispatcher& d);

} // namespace rea::core::game
