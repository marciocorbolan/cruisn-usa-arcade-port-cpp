#pragma once
/**
 * @file tms32031_state.hpp
 * @brief Registradores do TMS320C31 usados pelas rotinas traduzidas.
 */

#include <array>
#include <cstdint>

namespace rea::core {

/**
 * @brief Banco de registradores do TMS320C31.
 *
 * @details As rotinas traduzidas manipulam os registradores por nome, como no
 * assembly, para que cada linha C++ seja comparável à instrução original.
 * Os registradores de precisão estendida R0–R7 (40 bits em ponto flutuante)
 * são guardados aqui só pela parte inteira de 32 bits; rotinas que usam
 * aritmética de ponto flutuante convertem com `tms_float.hpp`.
 */
struct Tms32031State {
    std::array<uint32_t, 8> r{};   ///< R0–R7 (parte inteira).
    std::array<uint32_t, 8> ar{};  ///< AR0–AR7 (registradores auxiliares/ponteiros).
    uint32_t dp = 0;   ///< Data Page: bits 23–16 dos endereços diretos `@($xxxx)`.
    uint32_t ir0 = 0;  ///< Registrador de índice 0.
    uint32_t ir1 = 0;  ///< Registrador de índice 1.
    uint32_t bk = 0;   ///< Tamanho de bloco (endereçamento circular).
    uint32_t sp = 0;   ///< Ponteiro de pilha (cresce para cima).
    uint32_t st = 0;   ///< Status.
    uint32_t ie = 0;   ///< Habilitação de interrupções da CPU.
    uint32_t if_ = 0;  ///< Flags de interrupções pendentes (`IF`).
    uint32_t iof = 0;  ///< Configuração dos pinos XF0/XF1.
    uint32_t rs = 0;   ///< Início do bloco de repetição.
    uint32_t re = 0;   ///< Fim do bloco de repetição.
    uint32_t rc = 0;   ///< Contador de repetição.
    /// Port: endereço de retorno que o `CALL` empilharia. O chamador só o preenche
    /// antes de rotinas que fazem `POP` dele (ex.: `0xA874`, que guarda o ponto de retomada do processo).
    uint32_t ret = 0;
    /// Port: salto não-local pendente (0 = nenhum). Rotinas que, no original,
    /// descartam a pilha (`LDI @($63),SP`) e saltam para o loop principal
    /// (ex.: `0x1E17` → `BU $4C1B`) gravam aqui o destino e retornam. O
    /// escalonador (`0xA872`) para de despachar, e o MainLoop consome o valor.
    uint32_t jump = 0;

    // --- bits do registrador ST -------------------------------------------
    static constexpr uint32_t kStCarry = 1u << 0;   ///< C: carry/borrow (usado por BLO/BHS).
    static constexpr uint32_t kStZero = 1u << 2;    ///< Z: resultado zero (usado por BEQ/BNE).
    static constexpr uint32_t kStCacheEnable = 1u << 11; ///< CE: cache de instruções ligado.
    static constexpr uint32_t kStGie = 1u << 13;    ///< GIE: habilitação global de interrupções.

    // --- bits do registrador IE --------------------------------------------
    static constexpr uint32_t kIeInt0 = 1u << 0;  ///< EINT0 (VBLANK).
    static constexpr uint32_t kIeInt1 = 1u << 1;  ///< EINT1.
    static constexpr uint32_t kIeInt2 = 1u << 2;  ///< EINT2.
    static constexpr uint32_t kIeInt3 = 1u << 3;  ///< EINT3 (A/D).

    /**
     * @brief Endereço direto `@($offset)` = (DP << 16) | offset.
     * @param offset 16 bits baixos codificados na instrução.
     * @return Endereço de palavra de 24 bits.
     */
    uint32_t direct(uint32_t offset) const { return ((dp & 0xFFu) << 16) | (offset & 0xFFFFu); }

    /// @return true se o flag de carry/borrow (condição LO) está ligado.
    bool carry() const { return (st & kStCarry) != 0; }

    /// @return true se o flag de zero (condição EQ) está ligado.
    bool zero() const { return (st & kStZero) != 0; }

    /// Liga ou desliga um bit de ST.
    void setFlag(uint32_t bit, bool on) { st = on ? (st | bit) : (st & ~bit); }
};

/**
 * @brief Instrução `LSH count,dst` (deslocamento lógico) do TMS320C3x.
 * @details A contagem são os 7 bits baixos de @p count, com sinal (−64…+63):
 *          positiva desloca à esquerda e negativa desloca à direita (sem
 *          extensão de sinal). Com |contagem| ≥ 32 todos os bits saem e o
 *          resultado é 0. Diferente do C++, onde `x << 32` é indefinido.
 * @param value Operando de 32 bits.
 * @param count Registrador/imediato com a contagem.
 * @return Resultado do deslocamento.
 */
inline uint32_t tmsLsh(uint32_t value, uint32_t count) {
    const int32_t c = static_cast<int32_t>(count << 25) >> 25;
    if (c >= 0) return c < 32 ? value << c : 0u;
    return c > -32 ? value >> -c : 0u;
}

/**
 * @brief `MPYI`: multiplica os 24 bits baixos (com sinal) dos operandos; resultado = 32 bits baixos.
 */
inline uint32_t tmsMpyi(uint32_t a, uint32_t b) {
    const int64_t x = static_cast<int32_t>(a << 8) >> 8;
    const int64_t y = static_cast<int32_t>(b << 8) >> 8;
    return static_cast<uint32_t>(static_cast<uint64_t>(x * y));
}

} // namespace rea::core
