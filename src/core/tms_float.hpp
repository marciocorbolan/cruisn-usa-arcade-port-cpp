#pragma once
/**
 * @file tms_float.hpp
 * @brief Conversão entre o ponto flutuante nativo do TMS320C3x e IEEE 754.
 *
 * @details O TMS320C31 NÃO usa IEEE 754. Seu formato curto de 32 bits é:
 *
 * | Bits  | Campo                                   |
 * |-------|-----------------------------------------|
 * | 31–24 | expoente `e`, inteiro com sinal (comp. de 2) |
 * | 23    | sinal `s`                               |
 * | 22–0  | fração `f` (23 bits)                    |
 *
 * Valor: `s = 0 → (1 + f) × 2^e`; `s = 1 → (−2 + f) × 2^e`;
 * `e = −128 → 0` (zero, independentemente de s/f).
 * Toda tabela/rotina de física e projeção que use floats da ROM deve passar
 * por estas funções antes de virar `float`/`double` no PC.
 */

#include <cmath>
#include <cstdint>

namespace rea::core {

/**
 * @brief Converte uma palavra no formato curto do TMS320C3x para double.
 * @param raw Palavra de 32 bits lida do barramento.
 * @return Valor em ponto flutuante do host.
 */
inline double tmsToDouble(uint32_t raw) {
    const auto exponent = static_cast<int8_t>(static_cast<uint8_t>(raw >> 24));
    if (exponent == -128) return 0.0;
    const bool negative = (raw >> 23) & 1u;
    const double frac = static_cast<double>(raw & 0x7FFFFFu) / static_cast<double>(1u << 23);
    const double mantissa = negative ? (-2.0 + frac) : (1.0 + frac);
    return std::ldexp(mantissa, exponent);
}

/**
 * @brief Converte um double para o formato curto do TMS320C3x.
 * @param value Valor do host.
 * @return Palavra de 32 bits (fração truncada; saturação nos limites do expoente).
 */
inline uint32_t doubleToTms(double value) {
    if (value == 0.0 || std::isnan(value)) return 0x80000000u;
    int e = 0;
    double m = std::frexp(value, &e);   // value = m × 2^e, 0.5 ≤ |m| < 1
    m *= 2.0;
    e -= 1;                             // 1 ≤ |m| < 2
    uint32_t sign = 0;
    if (m < 0.0) {
        // Negativo: mantissa = −2 + f, f ∈ [0,1).  m ∈ (−2,−1].
        if (m == -1.0) {                // −1 × 2^e = −2 × 2^(e−1)
            m = -2.0;
            e -= 1;
        }
        sign = 1;
        m += 2.0;                       // f
    } else {
        m -= 1.0;                       // f
    }
    if (e > 127) return sign ? 0x7F800000u : 0x7F7FFFFFu;
    if (e < -127) return 0x80000000u;
    const auto frac = static_cast<uint32_t>(m * static_cast<double>(1u << 23)) & 0x7FFFFFu;
    return (static_cast<uint32_t>(static_cast<uint8_t>(static_cast<int8_t>(e))) << 24)
         | (sign << 23) | frac;
}

/**
 * @brief Instrução `RND` do TMS320C3x: arredonda a mantissa para os 24 bits do float curto, **ao mais próximo**
 *        (empate para +∞, como a soma de meio bit em complemento de dois do hardware).
 * @details Diferente de doubleToTms(), que trunca como o `STF`. Ex.: 1 − 2⁻³² vira 1,0 aqui e 0,99999994 com
 *          truncamento; na divisão `0xA509` isso faz x / 1 voltar exatamente x.
 * @param value Valor em double.
 * @return O valor arredondado (já representável no float TMS; satura nos extremos).
 */
inline double tmsRound(double value) {
    if (value == 0.0 || std::isnan(value)) return 0.0;
    int e = 0;
    std::frexp(value, &e);  // |value| = m × 2^e, 0,5 ≤ m < 1: 24 bits significativos = escala 2^(24 − e)
    const double q = std::floor(std::ldexp(value, 24 - e) + 0.5);
    return tmsToDouble(doubleToTms(std::ldexp(q, e - 24)));
}

/**
 * @brief Instrução `FIX` do TMS320C3x: float → inteiro de 32 bits.
 * @details Arredonda para −∞ (floor), não para zero como o cast do C++. Fora
 *          da faixa, satura em 0x7FFFFFFF / 0x80000000.
 * @param value Valor (já convertido de TMS para double).
 * @return Inteiro com sinal, como palavra de 32 bits.
 */
inline uint32_t tmsFix(double value) {
    const double f = std::floor(value);
    if (f >= 2147483647.0) return 0x7FFFFFFFu;
    if (f <= -2147483648.0) return 0x80000000u;
    return static_cast<uint32_t>(static_cast<int32_t>(f));
}

/**
 * @brief Imediato curto de ponto flutuante do TMS (16 bits: expoente 4, sinal 1, fração 11).
 * @param v Os 16 bits baixos da instrução (ex.: `MPYF 0.117645,R2` → 0xC70F).
 * @return O valor em double (expoente −8 = 0,0).
 */
inline double shortFloat(uint16_t v) {
    const int e = static_cast<int8_t>(static_cast<uint8_t>(v >> 12) << 4) >> 4;
    if (e == -8) return 0.0;
    const double f = static_cast<double>(v & 0x7FFu) / 2048.0;
    const double m = (v & 0x800u) ? -2.0 + f : 1.0 + f;
    return std::ldexp(m, e);
}

} // namespace rea::core
