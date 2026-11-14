#pragma once
/**
 * @file ground_height.hpp
 * @brief Altura do chão sob um ponto: busca nas listas de objetos de pista (`0x1F8B`),
 *        teste ponto-em-polígono no plano XZ (`0x20FF`) e altura do plano (`0x20B4`).
 *
 * @details **`0x1F8B`** (AR4 = ponto x,y,z em float):
 * - Percorre a lista `[E403]` (trechos de estrada, tipo 0x300) e, se nada achar, `[E406]`
 *   (tipo 0x900). Os objetos se encadeiam por `+0x20`.
 * - Objeto candidato: distância XZ² até `[obj+1]`/`[obj+3]` < raio² (`FLOAT [obj+0x1D]`).
 * - Para cada candidato chama `0x20B4`. Achou → carry = 1 e R0 = altura do chão relativa ao
 *   ponto. Nada → carry = 0 e R0 = 0,0.
 * - `0x1FB0` é o mesmo com AR4 + 1.
 *
 * **`0x20FF`** (AR2 = objeto, R2 = ponto): transforma os vértices do modelo `[obj+0xD]`
 * para coordenadas relativas ao ponto (v' = M·v + (obj − p), M = 9 floats em `[obj+4]`),
 * gravando-os em `[0x4F]` (3 floats cada). Modelo:
 * - cabeçalho: byte baixo = nº de vértices − 1, `>> 16` = nº de faces − 1;
 * - vértices: (y:16 | x:16 com sinal), z inteiro;
 * - uma palavra pulada e faces de 5 palavras; a 1ª traz 4 índices de byte (a, b, c, d).
 * Para cada aresta p→q do polígono (c→d só se c ≠ d; depois d→a, a→b, b→c):
 * x_p·z_q − z_p·x_q > |x_p − x_q| → fora (faces em sentido horário no plano XZ). Dentro → `[E577..E579]` = vértices a, b, c e
 * carry = 1.
 *
 * **`0x20B4`**: normal n = `0x9466` (em `[E57B]`), R0 = −(n·a) / −n.y (`0xA509`), carry = 1.
 * Preserva R4, R5, R6, R7 e AR2.
 *
 * **`0x9466`** (AR2 → 3 ponteiros a, b, c; AR0 = saída): n = (c − b) × (a − b) componente a
 * componente como na ROM (R0..R2 = a − b, R3..R5 = c − b).
 */

#include "core/subroutines.hpp"

namespace rea::core::track {

/// @brief Registra `0x1F8B`, `0x1F8F`, `0x1FB0`, `0x20B4`, `0x20FF` e `0x9466`.
void registerGroundHeight(SubroutineDispatcher& d);

} // namespace rea::core::track
