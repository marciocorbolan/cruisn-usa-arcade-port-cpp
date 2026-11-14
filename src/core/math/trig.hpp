#pragma once
/**
 * @file trig.hpp
 * @brief Seno, cosseno e matriz de rotação em Y (`0x92DF`, `0x92E2`, `0x938A`).
 *
 * @details A ROM usa uma tabela de 1/4 de onda do seno: 257 floats em `[0x9301]`
 * (= 0xC10DC2), com 1024 passos por volta.
 * - índice = `FIX(|ângulo| · [0x9300] + 0,5) & 0x3FF`, com `[0x9300]` = 512/π;
 * - ângulo negativo: usa |ângulo| + π, porque sen(|a| + π) = −sen(|a|) = sen(a);
 * - `0x92DF` (cosseno) soma `[0x9302]` = π/2 antes.
 *
 * O resultado é a palavra da tabela (exata) ou a sua negação (`NEGF`).
 */

#include "core/subroutines.hpp"

namespace rea::core::math {

inline constexpr uint32_t kTrigScalePtr  = 0x9300; ///< 512/π
inline constexpr uint32_t kSinTablePtr   = 0x9301; ///< 0xC10DC2
inline constexpr uint32_t kHalfPiPtr     = 0x9302;  ///< `[9302]` = π/2.
inline constexpr uint32_t kPiPtr         = 0x9303;  ///< `[9303]` = π.

/// @brief `0x92E2`: R0 = sen(R2). `0x92DF`: R0 = cos(R2). Preserva AR0.
void sinCos(Tms32031State& cpu, MemoryBus& bus, bool cosine);

/**
 * @brief `0x938A`: matriz 3×3 de rotação em Y em AR2 (R2 = ângulo).
 * @details `[0]` = `[8]` = cos, `[6]` = sen, `[2]` = −sen, `[4]` = 1; o resto 0.
 *          Preserva R0.
 */
void rotationY(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0xB01A` (`cosine = false`): R0 = sen(R2). `0xB042`: R0 = cos(R2).
 * @details Versão polinomial, a `SIN`/`COS` da biblioteca da TI:
 * 1. N = round(|x| / π), via `[C8BD]` = 1/π; o cosseno usa |x| + π/2 e N − 0,5;
 * 2. redução de Cody-Waite: f = (|x| − N·3,140625) − N·`[C8BE]`;
 * 3. R0 = ((((r4·g + r3)·g + r2)·g + r1)·g)·f + f, com g = f² e os
 *    coeficientes `[C8BF]`–`[C8C2]`;
 * 4. o sinal troca com N ímpar (paridade pela `0xA5A9`) e, no seno, com x < 0.
 *
 * Saída extra: RC = N; R2 = g (seno); R3 = sinal (seno) ou f (cosseno); DP = 0.
 * Preserva R4.
 * @note O port calcula em double, e o TMS em precisão estendida. Por isso o
 *       último bit da mantissa pode diferir.
 */
void sinCosPoly(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, bool cosine);

/// @brief `0x939D`: como a `0x938A`, mas com `0xB042`/`0xB01A`. Preserva R0–R4.
void rotationYPoly(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief `0x9377`: matriz de rotação em X em AR2 (R2 = ângulo): `[0]` = 1, `[4]` = `[8]` = cos, `[5]` = sen, `[7]` = −sen. Preserva R0.
void rotationX(Tms32031State& cpu, MemoryBus& bus);

/// @brief Registra `0x92DF`, `0x92E2`, `0x938A`, `0x9377`, `0xB01A`, `0xB042` e `0x939D`.
void registerTrig(SubroutineDispatcher& d);

} // namespace rea::core::math
