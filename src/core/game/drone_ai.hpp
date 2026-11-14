#pragma once
/**
 * @file drone_ai.hpp
 * @brief IA dos carros da CPU: laço `0x513A`, corpo `0x5140`, desvio de carros por um mapa de
 *        ocupação lateral e os laços fora da janela de trechos carregados.
 *
 * @details Processo do carro: AR4 = objeto, AR5 = dados D, AR7 = PCB.
 *
 * **`0x513A`** (a cada quadro):
 * - com `[E5EC]` = 0x33BB (grid): só `0x2EFD` + física `0x2C39`;
 * - senão o corpo `0x5140`;
 * - depois `0x7AD7` se houver link (`[C8E3]`) e dorme 1 quadro.
 *
 * **`0x5140`**:
 * 1. fim de corrida (`0x51C2`) quando o trecho do nó ≥ `[E853]`; som perto do jogador (`0x54CB`);
 * 2. o índice do nó `[AR7+0x13]` fora da janela → laços `0x5225` (atrás) ou `0x5208` (à frente);
 * 3. avança `[AR7+0x10]` enquanto o nó está a < 5000 ou o seguinte está mais perto;
 * 4. quadros até o nó = distância / (velocidade·`[C92F]`);
 * 5. mapa de ocupação em `[538C]` (50 faixas de 100 unidades): `0x53A2` (bloqueios da pista),
 *    `0x538D` (outros carros, marca 0x2000 + tempo), `0x539A` (jogador, 0x4000 + tempo);
 *    `0x53DA` escolhe o deslocamento lateral livre mais próximo → `[AR7+0x20]`;
 * 6. mira o ponto (nó + deslocamento) e esterça (diferença de rumo / quadros, ×2,5, ±0,09);
 *    acelerador `[D+0x24]` = `[AR7+0xF]` (`0x52E4`); `[D+0x21]` = esterço ÷ velocidade, ±0,3;
 * 7. `0x67E3`, `0x2EFD` e a dinâmica `0x2C18`.
 *
 * **Fora da janela** (`0x5208`/`0x5225`): o carro segue o caminho grosso `[AR7+0x12]` (4 palavras:
 * x, y, z, índice; −1 = fim) com `0x52B8`/`0x532B` até voltar à janela (`0x523C`).
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x513A`, `0x5140`, `0x51C2`, `0x51EA`, `0x51F4`, `0x5208`, `0x520F`, `0x5220`,
///        `0x5225`, `0x522C`, `0x523C`, `0x52B8`, `0x52E4`, `0x532B`, `0x538D`, `0x539A`,
///        `0x53A2`, `0x53DA`, `0x5441`, `0x54AA`, `0x54B5`, `0x54CB`, `0x66A7` e `0x94A7`.
void registerDroneAi(SubroutineDispatcher& d);

} // namespace rea::core::game
