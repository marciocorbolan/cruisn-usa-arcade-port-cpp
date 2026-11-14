#pragma once
/**
 * @file float_math.hpp
 * @brief Aritmética de ponto flutuante da ROM: divisão (`0xA509`), raiz (`0xA5F1`),
 *        atan2 (`0x931A`), normalização de ângulos (`0x9308`, `0x9313`) e operações de
 *        vetor/matriz (`0x93C4`, `0x93E8`, `0x93FD`, `0x9414`, `0x941E`) e retas 2D (`0x94B4`, `0x94C9`).
 *
 * @details **Divisão `0xA509`**: R0 = R0 / R1. O TMS320C31 não divide. A semente de 1/|R1|
 * é 2^−(e+1), com e = expoente de |R1|; depois vêm 4 iterações de Newton-Raphson
 * `x·(2 − r·x)` e uma de correção `x + x·(1 − r·x)`, com `RND` no meio. O sinal vem do divisor.
 * Saída: R0 = quociente; R1 = −(R0 / |R1|). Preserva R2.
 *
 * **Raiz `0xA5F1`**: R0 = √R2 (R2 ≤ 0 → R0 = R2). A semente de 1/√x é 2^−(e(2x) >> 1);
 * depois vêm 5 iterações de `y·(1,5 − y²·x/2)` e R0 = x·y. Preserva R1 e R2.
 *
 * **atan2 `0x931A`**: R0 = atan2(R3, R2), ou seja, y = R3 e x = R2 (erro máximo de ~0,009 rad pela tabela). Divide o menor pelo maior em módulo,
 * indexa a tabela `[9340]` (129 entradas) por FIX(128·|q|) e corrige pelo octante
 * (`[9341]` + 2·octante: multiplicador e soma). Preserva R1, R4 e AR0.
 *
 * **Ângulos**: `0x9308` → (−π, π] (meia volta como 16 bits com sinal: 0x7FFF..0xFFFF viram
 * negativos); `0x9313` → [0, 2π). Fatores `[9306]` (65536/2π) e `[9307]` (2π/65536).
 *
 * @note A divisão e a raiz são emuladas com as mesmas iterações em double. O original
 *       usa registradores de 40 bits; o `RND` é reproduzido por tmsRound() (ao mais próximo), mas o último bit
 *       de um resultado intermediário ainda pode diferir.
 */

#include "core/subroutines.hpp"

namespace rea::core::math {

/// @brief `0xA509`: R0 = R0 / R1 (Newton-Raphson). R1 = −(R0 / |R1|).
void floatDivide(Tms32031State& cpu);
/// @brief `0xA5F1`: R0 = √R2.
void floatSqrt(Tms32031State& cpu);
/// @brief `0x931A`: R0 = atan2(R3, R2) pela tabela da ROM.
void atan2Rom(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x9308` (`signedRange`) / `0x9313`: normaliza o ângulo R2.
void wrapAngle(Tms32031State& cpu, MemoryBus& bus, bool signedRange);
/// @brief `0x941E`: `[R3]` = `[R2]` (3×3) × `[AR2]` (vetor). Preserva R0, R2 e AR1.
void matVec(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0x93FD`: `[AR2]` = transposta de `[R2]`. Preserva R0 e AR0.
void transpose(Tms32031State& cpu, MemoryBus& bus);

/// @brief Registra as rotinas deste arquivo.
void registerFloatMath(SubroutineDispatcher& d);

} // namespace rea::core::math
