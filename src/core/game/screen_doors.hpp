#pragma once
/**
 * @file screen_doors.hpp
 * @brief Portas de transição entre telas: fecham (`0x5DE0`, cortina `[B0EF]` do pós-corrida) e
 *        abrem (`0x5EC0`, `[B0FF]` na etapa seguinte), com a luz das paletas (`0x5FD2`).
 *
 * @details Quatro painéis (`[C60C..C60F]` ao fechar, `[C610..C613]` ao abrir; paletas
 * `0x9EDB([59EB])`/`([59EA])` alternadas) presos a um pivô em `*+AR7(10..12)` com inclinação
 * `*+AR7(17)` (rotX em `*+AR7(18)`). Os painéis D/E giram no próprio eixo (`*+AR7(14/15)`, passos
 * `[5DDF]`/`[5DDE]`); C e F ficam a −127/+128 no eixo girado por `*+AR7(14/15)` e giram por
 * `*+AR7(13/16)`.
 * - Fechar: começam abertos (±π/2, inclinação π/2, z = 3368) e em 30 quadros vão a z = 368 (25 %),
 *   inclinação 0 (×0,9); luz de 0,6/0,2 subindo 0,0133/0,0266 por quadro. No fim, `0x9EC2` da paleta
 *   0x22 e limpa o bit 0x400 dos painéis.
 * - Abrir: som 0x84; partem fechados e se afastam (x += a, y −= b, com a/b ×1,2 por quadro), z → 3368
 *   (5 %), inclinação → π/2 (10 %); a luz segue o ângulo (`0x5FC2`). No fim, libera os painéis e as
 *   paletas (`0x5FA1`).
 * - `0x5FAC` copia a paleta 0x22 (`[9E7B]`) para `[59EA]`/`[59EB]` e envia (`0x9F2F`); `0x5FD2`
 *   escala cada canal de 5 bits pela luz `[E85D]` (`[59EB]`) e `[E85E]` (`[59EA]`) e reenvia.
 *
 * **Quirk preservado:** a `0x5FD2` processa uma palavra a mais que a `0x5FAC` copia (contagem por
 * `DBU` a partir de cabeçalho/2, sem o −1).
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x5DE0`, `0x5EAB`, `0x5EC0`, `0x5F96`, `0x5FA1`, `0x5FAC`, `0x5FC2` e `0x5FD2`.
void registerScreenDoors(SubroutineDispatcher& d);

} // namespace rea::core::game
