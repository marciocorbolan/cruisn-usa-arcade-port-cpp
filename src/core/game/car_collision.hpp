#pragma once
/**
 * @file car_collision.hpp
 * @brief Colisão entre carros: caixas orientadas (`0x2863`, `0x28A8`) e resposta (`0x2645`).
 *
 * @details **`0x28A8`** (AR0 = objeto, AR2 = destino): monta os 8 cantos da caixa do modelo em
 * D = `[obj+0x1B]` (x ∈ {`[D+0x3B]`, `[D+0x3A]`}, y ∈ {`[D+0x3D]`, `[D+0x3C]`}, z ∈ {`[D+0x3F]`, `[D+0x3E]`},
 * com x variando mais rápido) em AR2+0..23 e os transforma pela matriz do objeto (`+4`..`+0xC`, por
 * linhas) mais a posição (`+1`..`+3`, copiada também para `[0x4C]`−1..+1) em AR2+24..47. Devolve
 * AR2 + 48.
 *
 * **`0x2863`** (AR0, AR1 = carros): as caixas vão para `[0x4F]` (0x809D00) e 0x809D30; os 12 planos
 * (6 faces de cada caixa, triângulos da tabela `[28E7]`, normal por `0x9466`, d = n·a) para
 * 0x809D60. Carry = 1 se algum canto de B fica do lado de dentro (n·v ≥ d) das 6 faces de A, ou
 * algum canto de A dentro das 6 faces de B; AR3 aponta o y do canto em contato. Preserva R2, R3, R5,
 * IR0, AR0 e AR1.
 *
 * **`0x2645`** (resposta, AR0 = A, AR1 = B, AR3 = canto da `0x2863`):
 * 1. Ignora: B tipo 0x270 longe (> metade do raio `+0x1D`), dois 0x260, ou |Δy| > 750.
 * 2. `0x8928`; `0x2633` dá a direção A − B em `[0x5D]` e a velocidade relativa; `0x2835` toca o som;
 *    A anda +½·v·dir e B −½·v·dir (B tipo 0x26x → `0x9BEF`).
 * 3. Focado com Δv² ≥ 40000: chance Δv²/70 em 1000 e velocidade > 160 → `0x23EF` (batida forte).
 * 4. Choque elástico pelas massas `[D+0x30]` (a do focado × `[E87E]`³): nova velocidade = ¼ elástica
 *    + ¾ do centro de massa ± 10·dir (B × `[E58E]` se A é o focado); rumos novos (`0x931A`), B ± 0,333.
 * 5. `0x2727` em B (Δv = vA − vB) e em A (registradores trocados): impulso de giro `[E58F]` pelo braço
 *    do contato; escolhe rodopio (`[D+0x31]`/`0x2E`/`0x32`), empurrão ou giro leve.
 *
 * Particularidades reproduzidas: `0x3037` (via `0x2824`) não preserva AR0/AR2, e a 2ª chamada da
 * `0x2727` usa o AR0 resultante; o rodopio (`0x27A6`) sobrescreve R4 antes da 2ª chamada; `0x281D`
 * carrega um inteiro em R3 que depois é lido como float (expoente antigo mantido).
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x2633`, `0x2645`, `0x2727`, `0x278F`, `0x281D`, `0x2824`, `0x2835`, `0x2863`,
///        `0x28A8` e `0x5375`.
void registerCarCollision(SubroutineDispatcher& d);

} // namespace rea::core::game
