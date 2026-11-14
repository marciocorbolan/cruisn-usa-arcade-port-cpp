#pragma once
/**
 * @file matrix.hpp
 * @brief Matrizes 3×3 em float do TMS (`0x947F`, `0x93F1`, `0x9494`).
 *
 * @details As matrizes ficam na memória em ordem de linhas: 9 floats no formato
 * curto do TMS320C3x (`m[3·i + j]`).
 *
 * | Rotina | Efeito |
 * |---|---|
 * | `0x947F` | `[R3]` = `[R2]` × `[AR2]` (linhas de R2 × colunas de AR2) |
 * | `0x93F1` | copia 9 palavras de `[R2]` para `[AR2]` |
 * | `0x9494` | `[AR2]` = `[R2]` × `[AR2]`, usando `[0x4A]` como temporário |
 *
 * O produto é feito em double e gravado com `doubleToTms` (truncamento do
 * `STF`). No original, as somas parciais ficam nos registradores de 40 bits.
 * A diferença aparece só no último bit da mantissa.
 */

#include "core/subroutines.hpp"

namespace rea::core::math {

inline constexpr uint32_t kMatrixTempPtr = 0x004A; ///< `[0x4A]`: matriz temporária da `0x9494`.

/**
 * @brief Multiplica duas matrizes 3×3 da memória: `out = a × b`.
 * @details Grava cada elemento com o truncamento do `STF`. @p out pode ser
 *          igual a @p a ou @p b só se o chamador aceitar a sobreposição (a
 *          `0x947F` grava linha a linha).
 */
void mul3x3(MemoryBus& bus, uint32_t a, uint32_t b, uint32_t out);

/**
 * @brief `0x947F`: `[R3]` = `[R2]` × `[AR2]`.
 * @details Saída: AR0 = R2 + 9, AR1 = R3 + 9, IR0 = 3, IR1 = 5, RC = −1.
 *          R0–R2 ficam com valores intermediários, não reproduzidos.
 */
void matMul(Tms32031State& cpu, MemoryBus& bus);

/// @brief `0x9481`: AR1 = AR0 · AR2 (3×3) com os ponteiros já em AR0/AR1; avança AR0 e AR1 em 9.
void matMulRegs(Tms32031State& cpu, MemoryBus& bus);

/// @brief `0x93F1`: copia 9 palavras de `[R2]` para `[AR2]`. Preserva AR0, AR2 e R0; RC = −1.
void matCopy(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x9494`: `[AR2]` = `[R2]` × `[AR2]`, via `[0x4A]`.
 * @details Bug do original preservado: empilha AR0 no lugar de R0, então a
 *          rotina devolve **R0 = AR0** e AR2 = R3. Preserva R1–R3, AR0 e AR1.
 */
void matMulInPlace(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

inline constexpr uint32_t kEulerScratchPtr = 0x9342; ///< `[0x9342]` = 0x809A6D: cossenos e senos da `0x9343`.

/**
 * @brief `0x9343`: matriz de rotação em AR2 a partir de 3 ângulos em `[R2]`
 *        (a, b, c; seno/cosseno pela tabela, `0x92E2`/`0x92DF`).
 * @details Com cᵢ/sᵢ = cos/sen do ângulo i (rotação ZYX):
 *          | | | |
 *          |---|---|---|
 *          | c1·c2 | c1·s2 | −s1 |
 *          | s0·s1·c2 − c0·s2 | s0·s1·s2 + c0·c2 | s0·c1 |
 *          | c0·s1·c2 + s0·s2 | c0·s1·s2 − s0·c2 | c0·c1 |
 *
 *          Bug do original preservado: os POPs saem trocados e devolvem
 *          R1 = R0, R2 = R1 e R3 = R2 de entrada; R0 = c2·s0. Preserva AR1–AR3.
 */
void eulerMatrix(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief `0x93D7`: matriz identidade em AR0. Preserva R0.
void identity(Tms32031State& cpu, MemoryBus& bus);

/// @brief Registra `0x947F`, `0x93F1`, `0x9494`, `0x9343` e `0x93D7`.
void registerMatrix(SubroutineDispatcher& d);

} // namespace rea::core::math
