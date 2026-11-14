#pragma once
/**
 * @file link_car.hpp
 * @brief Carro do outro gabinete (`0xA66E`–`0xA717`).
 *
 * @details No link, o processo de carro com R4 ≥ 8 (`0x50CB`) desvia para **`0xA66E`** com R0 = `[C8D8]` (o
 * carro escolhido no outro gabinete; 4/5/6/7 viram os modelos 0xE/0x11/0xF/3):
 * - cria o objeto do modelo (`0x700A`, sem objeto morre), `[E592]` = objeto e `[[508E] + R4]` = objeto;
 * - peças (`0x8F3E`), dados (`0xAE3A`), `+58` = 1, parâmetros da tabela de carros (`*+AR7(27)`);
 * - põe o carro no início da janela (`0x54FD` em `[[E46E]+21]`, nó `[E46E]+1E` & ~0xFF), tipo 0x216;
 * - cria o objeto `[C98B]` (modelo `[B125]` para o carro 8, senão `[B126]`; flags 0x482 | bit 27; `+10` =
 *   `0x9EDB([B127])`, `+1B` = número do carro);
 * - laço `0xA6EA`: contato com o chão (`0x2041`), envia o tipo 0x19 (`0x7BE0`) e dorme 1 quadro. A posição
 *   chega pelo link (tipo 7, `0x7B58`).
 *
 * **`0xA6F8`** (loop principal, com `[C98B]` ≠ 0): escala o objeto `[C98B]` por `1 / [[0x52] + (+1C >> 4)]`
 * (mínimo 16), coloca-o 35 · escala + 20 acima do carro `[E592]` e o encadeia logo depois dele na lista de
 * desenho, com os bits 0x3000 de `+E` do carro.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0xA66E`, `0xA6EA`, `0xA6F7` (laço) e `0xA6F8`.
void registerLinkCar(SubroutineDispatcher& d);

} // namespace rea::core::game
