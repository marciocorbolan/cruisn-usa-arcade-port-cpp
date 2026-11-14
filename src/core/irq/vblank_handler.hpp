#pragma once
/**
 * @file vblank_handler.hpp
 * @brief Tradução do tratador da INT0 = VBLANK (ROM `0x4C34`–`0x4CED`).
 */

#include <cstdint>

#include "core/memory_bus.hpp"
#include "core/subroutines.hpp"
#include "core/tms32031_state.hpp"

namespace rea::core::irq {

/// Variáveis RAM usadas pela INT0 (nomes provisórios onde a função não é certa).
namespace vblank_vars {
inline constexpr uint32_t kFrameCounter    = 0xC930; ///< +1 por quadro; `0x4EC7` espera ele mudar ("aguarda VBLANK").
inline constexpr uint32_t kFrameCountF     = 0xC932; ///< Contador de quadros em float TMS.
inline constexpr uint32_t kPlayMinutes     = 0xC93A; ///< Minutos acumulados (float TMS, += 1/3420 por quadro).
inline constexpr uint32_t kSecondDivider   = 0xC93E; ///< Conta 0–56: divide 57 quadros em ~1 segundo.
inline constexpr uint32_t kBackdropColor   = 0xC938; ///< Copiada para a paleta[0] a cada quadro.
inline constexpr uint32_t kGameMode        = 0xC8C5; ///< &0xF: 1 = boot/atração sem watchdog; 2/4 = outros modos.
inline constexpr uint32_t kEventFlag       = 0xC93F; ///< Pedido pontual tratado no próximo VBLANK.
inline constexpr uint32_t kSecondsTimerA   = 0xE86A; ///< Contagem regressiva por segundo (satura em 0).
inline constexpr uint32_t kSecondsTimerB   = 0xE604; ///< Contagem regressiva por segundo, só com `[C8C5] & 0x200`.
inline constexpr uint32_t kMinutesPerFrame = 0xB094; ///< Constante ROM: 1/3420 (= 1 / (57 × 60)).
inline constexpr uint32_t kSoftWatchdog    = 0x8099E0; ///< Quadros sem o loop principal responder.
} // namespace vblank_vars

/**
 * @brief Tratador da interrupção de VBLANK (INT0, scanline 399).
 *
 * @details Executado uma vez por quadro (~57,928 Hz). Em ordem:
 *  1. Salva todo o contexto; `IE = INT1 | máscara salva` (só INT1 e, em Slave,
 *     INT2 podem interromper); limpa IF.INT0; wait states 0x470.
 *  2. `CALL 0x9E97`; copia `[0xC938]` para a paleta[0]; `CALL 0x7F25`.
 *  3. **Religa as interrupções** (`CALL 0x4C33`) — o resto do tratador pode ser
 *     interrompido pelo sincronismo de link.
 *  4. **Watchdog de software:** fora do modo 1, incrementa `0x8099E0`; se passar
 *     de 300 quadros (~5 s sem o loop principal zerá-lo em `0x4C02`), chama
 *     `0xAB1A` com R0 = 0x800 e salta para o RESET.
 *  5. Alimenta o watchdog de hardware (`0x5029`).
 *  6. Trata eventos pendentes, contadores e, a cada 57 quadros, as auditorias
 *     de tempo na CMOS (itens 0x76, 0x77 e `[0xE871] + 0x91`).
 *  7. Acumula minutos e contadores de quadro; chama 8 rotinas de jogo
 *     (entradas `0x4CEF` incluída).
 *  8. Restaura o contexto; `IE = INT0 | INT1 | INT3 | máscara salva`; `RETI`.
 */
class VblankHandler {
public:
    /// Resultado do tratador.
    enum class Result {
        Returned,      ///< `RETI` normal.
        WatchdogReset, ///< Watchdog de software estourou: execução segue no RESET (0x4AD5).
    };

    /// Limite do watchdog de software (CMPI $12C).
    static constexpr uint32_t kSoftWatchdogLimit = 300;

    /**
     * @brief Constrói o tratador.
     * @param bus Barramento.
     * @param subs Despacho das sub-rotinas.
     */
    VblankHandler(MemoryBus& bus, ISubroutines& subs) : bus_(bus), subs_(subs) {}

    /**
     * @brief Atende uma interrupção INT0.
     * @param cpu Registradores do código interrompido. São restaurados na saída,
     *            exceto IE (redefinido), IF (bit 0 limpo) e ST.GIE (ligado pelo RETI).
     * @return Se retornou normalmente ou se o watchdog mandou reiniciar.
     */
    Result run(Tms32031State& cpu);

private:
    /// Chama a sub-rotina traduzida em @p addr (o `CALL` original).
    void call(uint32_t addr, Tms32031State& cpu) { subs_.call(addr, cpu, bus_); }
    /// Lê a palavra no endereço @p addr.
    uint32_t rd(uint32_t addr) const { return bus_.readWord(addr); }
    /// Grava a palavra @p v no endereço @p addr.
    void wr(uint32_t addr, uint32_t v) { bus_.writeWord(addr, v); }

    MemoryBus& bus_;  ///< Barramento de memória simulado.
    ISubroutines& subs_;  ///< Despachante das sub-rotinas traduzidas.
};

} // namespace rea::core::irq
