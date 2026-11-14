#pragma once
/**
 * @file car_wheels.hpp
 * @brief Processo das peças do carro (`0x8F68`): giro/esterço das rodas e inclinação da carroceria.
 *
 * @details Criado por `0x8F3E` com AR4 = objeto do carro; as peças ficam na lista `[obj+0x1A]`
 * (próxima em `+0`, matriz 3×3 em `+7`, tipo em `+0x12`). A cada quadro (dorme 3 ou 1):
 * 1. Com o grid preso (`[E5EC]` = 0x33BB, fora do modo 3 e sem o bit 0x10 de `[C8C5]`) só copia a
 *    velocidade `[D+0x26]` para R7. Longe da câmera (`[obj+0x1C]` > 0x4E20) não faz nada.
 * 2. Senão monta `[5A]` = rotY(esterço `[D+0x21]`·1,5), soma o giro `[D+0x26]`·0,02 em `[P+0xE]`,
 *    `[5B]` = rotX(giro) e `[5C]` = `[5A]`·`[5B]` (`0x9481`).
 * 3. Percorre as peças: tipo < 0 ignora; tipo 1 recebe `[5B]` (roda traseira); tipo > 1 recebe
 *    `[5C]` (roda dianteira); tipo 0 é a carroceria (`0x8FAE`) e encerra o quadro.
 *
 * `0x8FAE` calcula a arfagem (aceleração, rotação do motor) e a rolagem (variação do rumo
 * `[obj+0x15]`) com amortecimento, grava `[D+0x42]`/`[D+0x43]` e monta a matriz da carroceria.
 * Particularidade: a correção de ±2π da variação do rumo é feita com `ADDI` (soma inteira) sobre
 * o campo de mantissa do registrador estendido, e não com `ADDF`; a tradução reproduz isso.
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief `ADDI R1,R0` sobre floats no registrador estendido: soma os campos sinal+mantissa
///        (24 bits do formato de memória) e mantém o expoente de @p r0.
uint32_t addiFloatFields(uint32_t r0, uint32_t r1);

/// @brief Registra `0x8F68`, `0x8F72` (retomada), `0x8FAE` (carroceria) e as rodas simples
///        `0xAEA8`/`0xAEAD` (criadas por `0xAE6A`: só giro, sem esterço nem carroceria).
void registerCarWheels(SubroutineDispatcher& d);

} // namespace rea::core::game
