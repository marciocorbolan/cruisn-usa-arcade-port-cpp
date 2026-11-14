#pragma once
/**
 * @file adc_handler.hpp
 * @brief Tradução da INT3 = fim de conversão do ADC0844 (ROM `0x4D6F`–`0x4DE5`).
 */

#include <cstdint>

#include "core/memory_bus.hpp"
#include "core/subroutines.hpp"
#include "core/tms32031_state.hpp"

namespace rea::core::irq {

/// Valores filtrados das entradas analógicas (inteiros 0–255).
namespace adc_vars {
inline constexpr uint32_t kWheel = 0xC8C8;  ///< Volante filtrado (variação limitada a ±65 por amostra).
inline constexpr uint32_t kGas = 0xC93B;    ///< Acelerador filtrado.
inline constexpr uint32_t kBrake = 0xC93C;  ///< Freio filtrado.
inline constexpr uint32_t kState = 0xC93D;  ///< 0 volante, 1 acelerador, 2 freio, 3 fim.
} // namespace adc_vars

/**
 * @brief Tratador da INT3: lê o canal convertido, filtra e inicia o próximo.
 *
 * @details A `0x4CEF` (chamada pela INT0) zera o estado e inicia o canal 1.
 * Cada INT3 então:
 *
 * | Estado | Lê       | Filtro (novo, anterior)                         | Próximo   |
 * |--------|----------|-------------------------------------------------|-----------|
 * | 0      | volante  | Δ limitado a ±65; 2703/8192·novo + 2744/4096·ant | canal 2   |
 * | 1      | acelerador | 2703/8192·novo + 2744/4096·ant                | canal 3   |
 * | 2 (ou 3) | freio  | 0,25·novo + 0,75·ant                            | (nenhum)  |
 *
 * Os pesos 2703/8192 ≈ 0,329956 e 2744/4096 ≈ 0,669922 são as constantes
 * imediatas exatas (float curto de 16 bits) e somam 0,99988, não 1. `FIX`
 * arredonda para baixo (piso).
 *
 * Desvios atrasados (`BUD`, `BNED`) executam as 3 instruções seguintes antes
 * de saltar; a tradução as coloca antes do desvio.
 */
class AdcHandler {
public:
    /**
     * @brief Constrói o tratador.
     * @param bus Barramento.
     * @param subs Despacho (não há chamadas a sub-rotinas não triviais aqui).
     */
    AdcHandler(MemoryBus& bus, ISubroutines& subs) : bus_(bus), subs_(subs) {}

    /**
     * @brief Atende uma INT3.
     * @param cpu Registradores do código interrompido; todos restaurados na
     *            saída, exceto IF.INT3 (limpo) e ST.GIE (ligado pelo RETI).
     */
    void run(Tms32031State& cpu);

private:
    MemoryBus& bus_;  ///< Barramento de memória simulado.
    ISubroutines& subs_;  ///< Despachante das sub-rotinas traduzidas.
};

} // namespace rea::core::irq
