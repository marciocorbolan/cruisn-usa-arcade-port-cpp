#pragma once
/**
 * @file car_proximity.hpp
 * @brief Simulação dos carros chamada pelo loop principal (`0x21D0`) e os laços de
 *        proximidade (`0x21E9`–`0x2232`, `0x25E8`–`0x2632`, `0x238C`).
 *
 * @details **`0x21D0`**, por modo (`[C8C5]` & 0xF), com AR7 = `[A840]`:
 * - modo 4 (corrida): `0x25E8`, `0x260E`, `0x21EC`, `0x21E9`, `0x21F1`, `0x21EF`, `0x691B`, `0x238C`;
 * - modo 2 (atrato) sem a flag 0x20: `0x260E`, `0x21F1`, `0x21EF`, `0x238C`.
 *
 * **Listas.** `[E404]` é a lista dos carros (próximo em `+0x20`). `[0x57]`, `[0x58]` e `[AF54]`
 * são endereços de células de cabeça; o original usa a cabeça − 0x20 como nó sentinela.
 * Campos usados: `+1`/`+3` posição x/z (float), `+0xE` flags (0x10 = sem colisão),
 * `+0xF` tipo, `+0x1B` dados do carro, `+0x1C` distância, `+0x1D` raio (inteiro).
 *
 * | Rotina | Efeito |
 * |---|---|
 * | `0x25E8` | carro `[E876]` × lista `[0x57]`: círculos se tocam → caixas (`0x2863`); colisão → `[E58E]` = 1 + 0,02 · (100 − min(v1 + v2, 100)) e `0x2645` |
 * | `0x260E` | cada par de carros de `[E404]`: círculos → `0x2863` → `0x2645` na 1ª colisão |
 * | `0x21E9` / `0x21EC` | carro `[E876]` × objetos `[AF54]` / `[0x58]` (`0x2207`) |
 * | `0x21EF` / `0x21F1` | cada carro (tipo ≠ 0x270, 0 ≤ `+0x1C` ≤ 50000) × `[AF54]` / `[0x58]` |
 * | `0x2207` | carro AR0 × lista AR1: distância² ≤ raio² do carro e sem a flag 0x10 → `0x221F` |
 * | `0x238C` | lista `[E8EC]`: `+0x1F` = 0 e `+0xE & 0x3000` = 0x2000 → `0x7100` |
 *
 * As rotinas de colisão (`0x2863`, caixas orientadas; `0x221F`, carro × objeto; `0x2645`,
 * resposta carro × carro) são chamadas pelo despachante.
 * @note Os laços originais usam desvios atrasados (`BUD`/`BNED`): os 3 slots já leem o
 *       próximo nó, inclusive o nó 0 (endereço 1/3) no fim da lista. O port não reproduz
 *       essas leituras, que não têm efeito.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// Variáveis da simulação dos carros.
namespace car_vars {
inline constexpr uint32_t kCarList     = 0xE404;  ///< Lista de carros (`0x6762`).
inline constexpr uint32_t kFocusCar    = 0xE876;  ///< objeto do carro focado (jogador/câmera)
inline constexpr uint32_t kCarCellPtr  = 0x0057;   ///< `[0x57]`: célula de cabeça dos carros visíveis.
inline constexpr uint32_t kObjCellPtr  = 0x0058;   ///< `[0x58]`: célula de cabeça dos objetos.
inline constexpr uint32_t kObjCell2Ptr = 0xAF54;   ///< `[AF54]`: outra lista de objetos.
inline constexpr uint32_t kBumpFactor  = 0xE58E;  ///< ganho do empurrão no carro focado (`0x25E8`)
inline constexpr uint32_t kAnimList    = 0xE8EC;  ///< Lista de objetos 0x730 (`0xAF58`).
inline constexpr uint32_t kProcAr7Ptr  = 0xA840;  ///< `[0xA840]` = 0xC980 (pseudo-nó da lista ativa).
inline constexpr uint32_t kNext        = 0x20;  ///< Campo do próximo nó da célula.
inline constexpr uint32_t kNoCollide   = 0x10;  ///< Flag em `+0xE`: objeto sem colisão.
} // namespace car_vars

/// @brief `0x21D0`: simulação por quadro (ver o arquivo).
void carSimulation(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x2207`: carro AR0 contra a lista cuja célula de cabeça é AR1.
void carVsObjects(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x260E`: pares de carros de `[E404]`.
void carVsCars(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra `0x21D0`, `0x21E9`, `0x21EC`, `0x21EF`, `0x21F1`, `0x2207`, `0x25E8`, `0x260E` e `0x238C`.
void registerCarProximity(SubroutineDispatcher& d);

} // namespace rea::core::game
