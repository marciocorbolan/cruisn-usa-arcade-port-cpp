#pragma once
/**
 * @file node_pools.hpp
 * @brief Listas encadeadas e *pools* de nós da ROM (`0x8EF1`–`0x8F3D`, `0x8FF0`–`0x9035`, `0x7648`).
 *
 * @details Um nó guarda o próximo na palavra 0. Uma "lista" é a palavra com o primeiro nó.
 *
 * | Rotina | Efeito |
 * |---|---|
 * | `0x8EF1` | inicia um pool: `[R3]` = 0 (lista vazia); a lista livre `[R2]` recebe RC + 1 nós de RS palavras a partir de AR2. Preserva R0 e AR0; AR2 = fim do pool; RC = −1 |
 * | `0x8F01` | tira um nó da lista livre `[AR2]` e o põe na frente da lista `[R2]`. Achou: AR0 = R0 = nó, carry = 1; senão R0 = 0, carry = 0. Preserva R1 e AR1 |
 * | `0x8F13` | tira um nó da lista livre `[AR2]`: AR0 = nó, carry = 1; vazia: carry = 0 (AR0 intacto). Preserva R0 |
 * | `0x8F1F` | põe AR2 na frente da lista `[R2]`. Preserva R2 e AR0 |
 * | `0x8F29` | procura AR2 na lista `[R2]`; achou → o tira de lá e o põe na frente da lista `[R3]`. Preserva R0, R1, AR0 e AR1 |
 * | `0x7648` | pool dos textos sobrepostos: 0xA0 nós de 0xD palavras em `[7640]` = 0x1E568; livres `[763E]`, lista `[763F]`; `[E91B]` = 0xA0 |
 * | `0x8FF0` | pool de 0xAF nós de 0x13 palavras em `[8FED]` = 0xCE14; livres `[8FEE]` = 0xDB11, lista `[8FEF]` = 0xDB12. Preserva AR2, R2, R3, RC e RS |
 * | `0x9001` | tira um nó de `[DB11]`: identidade em `+7` (`0x93D7`), `+0x12` = 0; carry = 1 (vazia: 0). Preserva R0 |
 * | `0x9011` | devolve AR2 a `[DB11]`. Preserva R0 |
 * | `0x9019` | 0x19 nós de 0x5B palavras a partir de `[9017]` = 0xDB13 na lista livre `[9018]` = 0xE3F6; `[E3F7]` = 0 (em uso) |
 * | `0x9024` | tira um nó de `[E3F6]` (AR0), `[E3F7]` += 1, carry = 1 (vazia: carry = 0). Preserva R0 |
 * | `0x9032` | devolve AR2 a `[E3F6]`, `[E3F7]` −= 1. Preserva R0 |
 */

#include "core/subroutines.hpp"

namespace rea::core::scene {

/// Endereços dos pools.
namespace pool_vars {
inline constexpr uint32_t kTextPoolBase  = 0x7640; ///< `[7640]` = 0x1E568.
inline constexpr uint32_t kTextFreePtr   = 0x763E; ///< `[763E]` = 0xE919.
inline constexpr uint32_t kTextListPtr   = 0x763F; ///< `[763F]` = 0xE91A.
inline constexpr uint32_t kTextFreeCount = 0xE91B;  ///< Nós de texto livres (a `0x7648` começa com 0xA0).
inline constexpr uint32_t kTextNodes     = 0xA0;  ///< Nós no pool de textos.
inline constexpr uint32_t kTextNodeSize  = 0xD;  ///< Palavras por nó de texto.
inline constexpr uint32_t kMatPoolBase   = 0x8FED; ///< `[8FED]` = 0xCE14.
inline constexpr uint32_t kMatFreePtr    = 0x8FEE; ///< `[8FEE]` = 0xDB11.
inline constexpr uint32_t kMatListPtr    = 0x8FEF; ///< `[8FEF]` = 0xDB12.
inline constexpr uint32_t kMatFree       = 0xDB11;  ///< Lista livre do pool de 0x13 palavras (matrizes).
inline constexpr uint32_t kBigPoolBase   = 0x9017; ///< `[9017]` = 0xDB13.
inline constexpr uint32_t kBigFreePtr    = 0x9018; ///< `[9018]` = 0xE3F6.
inline constexpr uint32_t kBigFree       = 0xE3F6;  ///< Lista livre do pool de 0x5B palavras.
inline constexpr uint32_t kBigInUse      = 0xE3F7;  ///< Nós em uso no pool de 0x5B palavras.
} // namespace pool_vars

/// @brief `0x8EF1`: inicia um pool (ver tabela).
void initPool(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0x8F01`: aloca da lista livre `[AR2]` para a lista `[R2]`.
void allocToList(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0x8F13`: aloca da lista livre `[AR2]`.
void allocNode(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0x8F1F`: insere AR2 na frente da lista `[R2]`.
void pushFront(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0x8F29`: move AR2 da lista `[R2]` para a frente da lista `[R3]`.
void moveNode(Tms32031State& cpu, MemoryBus& bus);

/// @brief Registra as rotinas deste arquivo.
void registerNodePools(SubroutineDispatcher& d);

} // namespace rea::core::scene
