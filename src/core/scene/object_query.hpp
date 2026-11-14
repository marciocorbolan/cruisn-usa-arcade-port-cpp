#pragma once
/**
 * @file object_query.hpp
 * @brief Buscas e operações por tipo na lista visível (`0x36B3`–`0x3737`, `0x9EDB`).
 *
 * @details Percorrem a lista visível `[[0x40]]`, olhando o campo `+0xF` (tipo/flags).
 * O resultado volta em AR0 e no carry (`ST` bit 0):
 *
 * | Rotina | Critério | Onde começa | Sem resultado |
 * |---|---|---|---|
 * | `0x36B3` | `tipo & 0xFF == R1` | cabeça | carry = 1; AR0 = último nó visitado |
 * | `0x36C7` | `tipo & 0xFF == R1` | depois de AR0 | carry = 1; AR0 = 0 |
 * | `0x36DB` | `tipo & R1 != 0` | cabeça | carry = 1; AR0 = 0 |
 * | `0x36EF` | `tipo & R1 != 0` | depois de AR0 | carry = 1; AR0 = 0 |
 *
 * Todas preservam R0 e AR5. Com resultado, carry = 0.
 *
 * Aplicações sobre todos os que casam:
 * - `0x3712`: `tipo & 0xFF == R1` → `tipo |= R2`;
 * - `0x3702`: lista em AR3 terminada por 0. Cada palavra é um tipo (≤ 0xFF) ou
 *   uma faixa `(de << 8) | até`; aplica a `0x3712` a cada tipo;
 * - `0x371E`: `tipo & R1 != 0` → `+2 += R2` (float). Preserva R3;
 * - `0x372E`: `tipo & R1 != 0` → `CALL $715F` com AR2 = objeto.
 *
 * `0x9EDB`: procura AR2 na tabela `[0x9E7F]` (até 0x82 entradas: índices 0 a 0x81). Achou:
 * R0 = índice << 8 e carry = 1. Senão: R0 = −1 e carry = 0. DP = 0.
 */

#include "core/subroutines.hpp"

namespace rea::core::scene {

inline constexpr uint32_t kTypeField = 0x0F;  ///< Campo do tipo do objeto (`+0xF`).

/// @brief `0x36B3` (`from_head`) / `0x36C7`: procura `tipo & 0xFF == R1`.
void findTypeEq(Tms32031State& cpu, MemoryBus& bus, bool from_head);
/// @brief `0x36DB` (`from_head`) / `0x36EF`: procura `tipo & R1 != 0`.
void findTypeMask(Tms32031State& cpu, MemoryBus& bus, bool from_head);
/// @brief `0x9EDB`: índice de AR2 na tabela `[0x9E7F]`.
void findInTable9E7F(Tms32031State& cpu, MemoryBus& bus);

/// @brief `0xAE27`: próximo objeto (a partir do sucessor de AR0) com `+0xF` == R1. Achou → AR0 e carry = 0; senão AR0 = 0 e carry = 1. Preserva R0 e AR5.
void findNextType(Tms32031State& cpu, MemoryBus& bus);

/// @brief Registra as rotinas deste arquivo.
void registerObjectQuery(SubroutineDispatcher& d);

} // namespace rea::core::scene
