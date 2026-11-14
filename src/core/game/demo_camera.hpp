#pragma once
/**
 * @file demo_camera.hpp
 * @brief Funções do roteiro de câmera da demonstração (`0x5624`–`0x5942`), chamadas pelo
 *        interpretador `0x559E` (ver `attract_demo.hpp`).
 *
 * @details Um passo do roteiro é: `início, (argumentos), duração, por quadro`. A câmera do
 * processo fica em `AR7+0x17..0x1D`: posição, ângulos (X, Y, Z) e matriz.
 *
 * **Pista**: `[AA2F]` aponta para os nós da pista, com 4 inteiros cada (x, y, z, índice).
 * `0x585A` acha o nó de índice ≥ R2. `0x584F` dá o nó do carro AR0 (`[[AR0+0x17]+0x12]`, ou
 * pelo índice `[[[AR0+0x1B]+0x37]+0x1E]` se `[[AR0+0x17]+0x25]` = 0). `0x5847` procura o
 * índice R2 na lista `[E46E]` (campo `+0x1E`, próximo `+0x21`). AR2 = 0 quando não acha.
 *
 * **Escolha do carro**:
 * - `0x57F0`: o R2-ésimo carro (tipo `[+0xF]` = 0x216) mais adiantado na pista. Os
 *   escolhidos ficam em `AR7+0x2C..0x35`. Sem carro, o original trava (laço infinito);
 *   aqui fica pendente `0x581B`.
 * - `0x581E`: o carro mais próximo dentro de ±0,78 rad do eixo da câmera. AR1 = carro,
 *   R0 = R5 = distância (sem carro: AR1 = 0, R5 = máximo).
 *
 * **Tipos de passo**:
 * - perseguição `0x5624`/`0x5671`/`0x5680`/`0x568F`: vão para trás/frente do carro N na
 *   pista (`0x569E`), à altura `[AR7+0x39]` (−350 ou −300). A cada quadro, `0x5719` anda
 *   `[AR7+0x27]`·`[C92F]` na direção da pista, acompanha a altura do chão (`0x1F8B`) ou a
 *   rampa entre os nós, e mira o carro (`0x5784`);
 * - panorâmica `0x5633` (arg: índice do nó): câmera parada num nó, girando para seguir o
 *   carro `[AR7+0x28]` (`0x56C5`). Na pista 2 (com o item 0x1E), um giro ≥ 0,18 rad toca o
 *   som 0x30;
 * - presa ao carro `0x5869`/`0x5861`/`0x587C` → `0x58EF`: deslocamento `AR7+0x13..0x16`
 *   relativo ao carro, interpolado em `AR7+0xF..0x12` (`0x591A`). A matriz vem do carro
 *   (`0x5883`). O passo acaba quando o índice do nó do carro chega à duração AR5.
 *   Ao entrar ou sair do modo `[+0xE]` & 0x3000, o carro é ligado (`0x7093`) ou desligado
 *   (`0x715F`) conforme `|[AR7+0x11]|` e 100.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra as funções de `0x5624` a `0x591A`.
void registerDemoCamera(SubroutineDispatcher& d);

} // namespace rea::core::game
