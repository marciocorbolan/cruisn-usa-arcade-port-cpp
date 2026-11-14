#pragma once
/**
 * @file race_traffic.hpp
 * @brief Posição na corrida (`0x659F`) e geradores de carros extras (`0x65ED`, `0x6639`).
 *
 * @details
 * **`0x659F`** (a cada 0xE quadros, AR4 = `[E876]` carro focado, AR5 = `[E878]` seus dados) chama
 * `0x65A5`, que percorre a lista de carros `[E404]`:
 * - `[E5F1]` = quantos carros estão com a distância `[obj+0x1C]` em −5000..30000;
 * - `[E607]` = 1 + quantos carros do tipo 0x216 estão à frente do focado. "À frente" =
 *   índice de nó `+0x1E` maior (ou contador de voltas `[D+0x57]` maior, se `[D+0x58]` ≠ 0); no mesmo
 *   nó compara a distância ao nó seguinte descontando o quadrado da distância lateral `[D+0x46]`.
 *
 * **`0x65ED`** (sai se há link `[C8E3]`): espera 0x258 quadros e, em ciclos de
 * 2·(`[E5F2]` + aleatório(`[E5F3]`)) quadros, espera `[E5EB]` ≠ 0 e o grid solto; com
 * `[E5EE]` − 1 ≤ `[E5F4]` sorteia pela posição (≤ 1: sempre; ≤ 4: 70%; ≤ 7: 60%; senão 35%) e cria
 * o processo `[B12B]` (ID 0x214). Enquanto esse carro estiver a menos de 25000 de `[E46D]`, espera.
 *
 * **`0x6639`**: zera o contador `[E5F5]` (`0x6636`), guarda a posição inicial do focado e, fora
 * do modo 2, espera ele andar 11000. Depois, em ciclos (50 quadros se posição ≤ 2, senão
 * `[E5F2]` + aleatório(`[E5F3]`)), com as mesmas condições (`[E5EE]` ≤ `[E5F4]`, limiares 75/60/35%) e
 * no máximo 8 carros próximos (`[E5F1]`), cria o processo `[B12C]` (ID 0x213) se `0x6680` permitir.
 *
 * | Rotina | Efeito |
 * |---|---|
 * | `0x5268` | compara `[D+0x57]` do carro `[E592]` (R0) e do focado (R1) |
 * | `0x6628` | `[AR5+0x58]` = 0; `[E5F5]` = próximo número 10..0x7F; `[AR5+0x59]` = número \| 0x80 sem o bit 0 de `[C8CC]` |
 * | `0x6636` | `[E5F5]` = 10 |
 * | `0x6680` | R0 = 1 sem link; com link, 1 se o focado está à frente de `[E592]` (ou empatado e sem o bit 0 de `[C8CC]`) |
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x659F`, `0x65A5`, `0x65ED` (e retomadas `0x65F2`, `0x65F8`, `0x661A`),
///        `0x6628`, `0x6636`, `0x6639` (e retomadas `0x663C`, `0x6641`, `0x6645`, `0x6653`, `0x665D`),
///        `0x6680` e `0x5268`.
void registerRaceTraffic(SubroutineDispatcher& d);

} // namespace rea::core::game
