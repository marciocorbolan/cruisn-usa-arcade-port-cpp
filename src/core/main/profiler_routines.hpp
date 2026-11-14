#pragma once
/**
 * @file profiler_routines.hpp
 * @brief Profiler interno do loop principal (`0x4F08`, `0x4F27`) e recíproco (`0xA585`).
 *
 * @details O loop principal chama `0x4F27` entre as etapas de cada iteração.
 * Ela lê o contador do timer 0 (`0x808024`, contando livre desde o RESET),
 * converte para float e grava em `[C945]++`. `0x4F08` fecha a iteração:
 * acumula as 16 amostras e calcula a média por iteração.
 *
 * Bloco de 48 floats em `0xC947` (ponteiro na palavra `0x4F07`):
 *
 * | Faixa           | Conteúdo                                   |
 * |-----------------|--------------------------------------------|
 * | `C947`–`C956`   | amostras da iteração atual (`0x4F27`)      |
 * | `C957`–`C966`   | somas acumuladas                           |
 * | `C967`–`C976`   | médias = soma × (1 / `[C944]`)             |
 *
 * `[C944]` (float) conta as iterações; `[C946] != 0` zera tudo.
 *
 * **Aproximação do port:** o timer 0 ainda não é emulado; o contador lido é o
 * valor guardado no barramento (0 após o RESET), então as amostras valem 0.0.
 * A lógica é traduzida fielmente para quando o timer existir.
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::prof {

/// Variáveis RAM do profiler.
namespace prof_vars {
inline constexpr uint32_t kIterations = 0xC944;   ///< float: iterações acumuladas.
inline constexpr uint32_t kCursor = 0xC945;       ///< ponteiro da próxima amostra.
inline constexpr uint32_t kResetRequest = 0xC946; ///< != 0: zera o profiler na próxima `0x4F08`.
inline constexpr uint32_t kBlockPtr = 0x4F07;     ///< palavra que contém 0xC947.
inline constexpr uint32_t kSamplesPerIteration = 16;  ///< Amostras do timer guardadas por iteração.
} // namespace prof_vars

/**
 * @brief `0x4F27`: R0 = FLOAT(contador do timer 0); `*[C945]++ = R0`.
 * @param cpu Saída: R0 = amostra (float TMS). DP e AR0 preservados.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 */
void sample(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x4F08`: fecha uma iteração (acumula e calcula médias) ou zera o bloco.
 * @param cpu Saída: R0 = 1/iterações (ou 0.0 se zerou), R1 = última média, AR0 alterado.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void endIteration(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0xA585`: recíproco em ponto flutuante, R0 = 1 / R0.
 *
 * @details O original usa 4 iterações de Newton–Raphson (`x ← x·(2 − a·x)`)
 * a partir de uma semente obtida negando o expoente, mais uma correção final
 * após `RND`; o resultado tem a precisão do float de 32 bits do TMS. Aqui o
 * valor é calculado diretamente em `double` e convertido de volta.
 * Retorna pelo endereço em BK (`POP BK` / `BUD BK`), convenção irrelevante em C++.
 * @param cpu Entrada R0 (float). Saída: R0 = 1/R0; R1 = −|1/R0|; R2 preservado.
 */
void reciprocal(Tms32031State& cpu);

/// @brief Registra `0x4F27`, `0x4F08` e `0xA585`.
void registerRoutines(SubroutineDispatcher& d);

} // namespace rea::core::prof
