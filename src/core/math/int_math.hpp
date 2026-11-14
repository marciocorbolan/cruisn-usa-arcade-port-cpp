#pragma once
/**
 * @file int_math.hpp
 * @brief Aritmética inteira da ROM: divisão (`0xA531`), resto (`0xA5A9`) e aleatório (`0x8EB7`).
 *
 * @details A `0xA531` e a `0xA5A9` são as rotinas `DIV_I`/`MOD_I` da biblioteca
 * de runtime da TI para o C3x. Normalizam os operandos pelo expoente do `FLOAT`
 * e iteram `SUBC`. O resultado é o da divisão truncada do C:
 * - quociente com o sinal de `a ^ b`;
 * - resto com o sinal do dividendo.
 *
 * O port calcula o mesmo resultado direto em C++.
 *
 * **Desvios documentados:**
 * - Divisão por zero devolve 0. No hardware, o laço de `SUBC` com divisor 0
 *   produz um valor sem significado; nenhum chamador conhecido divide por 0.
 * - Os registradores usados como rascunho (R1, AR0, AR1, RC) não são reproduzidos.
 *   Os chamadores tratam essas chamadas como se destruíssem esses registradores.
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::math {

/// @brief `0xA531`: R0 = R0 / R1 (inteiros com sinal, truncado).
uint32_t divI(uint32_t a, uint32_t b);
/// @brief `0xA5A9`: R0 = R0 % R1 (sinal do dividendo).
/// @details Efeitos colaterais registrados: AR0 = dividendo, R1 = −|resto|. Não
///          modelados: AR1 (divisor deslocado) e RC (recebe o endereço de retorno).
uint32_t modI(uint32_t a, uint32_t b);

/**
 * @brief `0x8EB7`: gerador pseudoaleatório.
 * @details `x = [C8CE]`; `y = x << 1`; se `(x ^ y) < 0`, `y |= 1`.
 *          Depois `[C8CE] = R0 = MPYI(y, 0x794F)`, multiplicação de 24 bits.
 *          Preserva R1.
 */
void random(Tms32031State& cpu, MemoryBus& bus);

inline constexpr uint32_t kRandomSeed = 0xC8CE;  ///< Semente do gerador pseudoaleatório; o RESET grava o marcador 0x5A5A5A5A.

/**
 * @brief `0x8EE9`: R0 = aleatório em [0, 1000) se for menor que AR2; senão 0.
 * @details Carry = 1 quando o valor é menor que AR2 (o chamador testa com `LDILO`/`BLO`).
 */
void randomBelow(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra `0xA531`, `0xA5A9`, `0x8EB7`, `0x8EE9` e os aleatórios `0x8EDB`, `0x8EE0`,
///        `0x8EE3`, `0x8EC2` (float 0..R0) e `0x8ED1` (float ±R0).
void registerRoutines(SubroutineDispatcher& d);

} // namespace rea::core::math
