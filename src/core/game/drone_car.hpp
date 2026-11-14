#pragma once
/**
 * @file drone_car.hpp
 * @brief Carros controlados pela CPU: criação do processo `0x50CB`, dados do carro
 *        (`0x2945`, `0xAE3A`), rodas/peças (`0x8F3E`, `0xAE6A`), fator de velocidade
 *        (`0x509A`) e posição inicial na pista (`0x54FD`).
 *
 * @details **`0x50CB`** (processo ID 0x200, R4 = número do carro):
 * - Entrada `[[5051] + 6·R4]`, 6 palavras:
 *   - modelo;
 *   - nós à frente na largada;
 *   - posição lateral (float);
 *   - fator de velocidade;
 *   - `+4` → `[AR7+0x26]`;
 *   - textura (`0x9EDB`).
 * - Carros ≥ 8 com link (`[C8E3]`) seguem para `0xA66E`.
 * - Cria o objeto (`0x700A` com o modelo `[[961B] + 11·modelo]`) e o guarda em `[E8A3 + R4]`.
 * - Monta as peças (`0x8F3E`) e os dados do carro (`0xAE3A` → `0x2945`, 0x5B palavras em `[obj+0x1B]`).
 * - Põe o carro na pista (`0x54FD`) e marca o tipo 0x216.
 * - PCB, campos AR7+n:
 *   - `+0xC` = 2 ou 3 (lado);
 *   - `+0xF` = `+0x21` = 1;
 *   - `+0x22` = 1,2 ± 0,05;
 *   - `+0x23` = `+0x24` = 0x78 (contadores);
 *   - `+0x25` = 0.
 * - Depois entra no laço de IA `0x513A` (`drone_ai.cpp`).
 *
 * **`0x54FD`**: parte do nó AR2 da lista de trechos e anda AR3 passos de 5500 na direção
 * do nó seguinte (a cada passo, avança o nó enquanto ele está a ≤ 5000). Depois desloca
 * lateralmente `[AR7+0x1B]` (rotação Y) e grava a posição do objeto. `[AR7+0x10]` = nó,
 * `[AR7+0x13]` = índice. R2 = rumo.
 *
 * @note Ponte do port: enquanto o laço `0x513A` não estiver traduzido, o PC do PCB é
 *       gravado como 0x513A antes de chamá-lo, para o escalonador não repetir a criação.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x50CB`, `0x2EFD`, `0x509A`, `0x54FD`, `0x5360`, `0x536C`, `0x2945`, `0x8F3E`,
///        `0xAE3A` e `0xAE6A`.
void registerDroneCar(SubroutineDispatcher& d);

} // namespace rea::core::game
