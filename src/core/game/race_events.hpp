#pragma once
/**
 * @file race_events.hpp
 * @brief Eventos de pista (`0xACCA`), chegada de etapa (`0x3970`–`0x3998`) e objetos 0x75x
 *        empurrados pela câmera (`0x691B`).
 *
 * @details
 * **`0xACCA`**: AR0 = índice do evento (0 = nada) → `CALLU [[ACD0] + AR0]` (tabela em 0xACD1):
 * checkpoints (`0xAD03`: `[E869]`++; ao passar `[E63A]`, `0x7A36`, `[E63A]`++ e `[E604]` += `[E868]`,
 * com o som 0xFF no modo 4), bits de `[C8C5]` (0x40, 0x80, 0x400, 0x800), névoa/céu
 * (`[E3FB]`, `[E3FD]`, `[E5F2..E5F4]`), sons ambientes 0x15C..0x171, processos `[C619]` (0x214) e
 * `[C62A]` (0x500), paleta 0x4C e as 15 linhas de chegada.
 *
 * **Chegada** (`0x3998`): `[E480]` = limite, `[E481]` = etapa (1..0xE); só no modo 4: modo 5,
 * `[C8DA]` |= 0x800, dificuldade `[E639]` ±(0,07 / −0,035) em 0,8..1,25 (`0x5090`), item 0xDF =
 * etapa, auditoria 0x9A + 2·(etapa−1), para os sons de motor (`0x3C7A`), `[E5F4]` = 0xB, `[E868]` =
 * item 0x1D, posição (`0x65A5`) e o processo `[B0D7]` (ID 0x16, depois do atual).
 *
 * **`0x691B`**: para cada objeto `(+F & 0xFF0) == 0x750` da lista `[[0x40]]` com subtipo 1/2/4..,
 * desloca o objeto na direção da câmera `-[809807]` pelas distâncias `[C62B + 5·(n−1) + 3/4]` e
 * testa contra o carro (`0x221F`), voltando à posição se continuou ativo.
 *
 * **Quirk preservado:** se o primeiro teste zera o subtipo, a `0x692C` faz `POP AR3 / RETSU` sem
 * o endereço de retorno empilhado — sai direto da `0x691B` (AR3 = 0x6926) sem zerar `[E8ED/E8EE]`.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0xACCA`, seus tratadores (`0xACFF`–`0xAD90`, `0x9BEA`), `0x3966`,
///        `0x3970`–`0x3998`, `0x5090`, `0x3C7A`, `0x90DD`, `0x691B`, `0x692C` e `0x693D`.
void registerRaceEvents(SubroutineDispatcher& d);

} // namespace rea::core::game
