#pragma once
/**
 * @file frame_runner.hpp
 * @brief Um quadro do arcade: entradas → INT0 (VBLANK) → cadeia de INT3 (ADC).
 */

#include <cstdint>
#include <functional>

#include "core/busy_wait.hpp"
#include "core/io/cabinet_io.hpp"
#include "core/irq/adc_handler.hpp"
#include "core/irq/vblank_handler.hpp"
#include "core/memory_bus.hpp"
#include "core/subroutines.hpp"
#include "core/tms32031_state.hpp"

namespace rea::core {

/**
 * @brief Controlador de interrupções simplificado do TMS320C31 para um quadro.
 *
 * @details Ordem real no hardware: na scanline 399 a INT0 é aceita; dentro dela
 * a `0x4CEF` inicia a conversão do volante, mas a INT3 só é aceita quando a
 * INT0 termina (durante a INT0, IE não inclui INT3). Depois disso o ADC
 * encadeia volante → acelerador → freio, uma INT3 por canal. Uma interrupção
 * só é aceita com `ST.GIE = 1` e o bit correspondente em `IE`.
 */
class FrameRunner {
public:
    /// Resultado de um quadro.
    enum class Result { Ok, WatchdogReset };

    /**
     * @brief Constrói o executor.
     * @param bus Barramento (com o `cabinet` já conectado via `attachDevice`).
     * @param subs Despacho de sub-rotinas.
     * @param cabinet Dispositivo de I/O do gabinete (fonte da INT3).
     */
    FrameRunner(MemoryBus& bus, ISubroutines& subs, io::CabinetIo& cabinet)
        : cabinet_(cabinet), vblank_(bus, subs), adc_(bus, subs) {}

    /**
     * @brief Executa um quadro.
     * @param cpu Registradores do código principal (interrompido).
     * @return Ok ou WatchdogReset (o chamador deve executar o RESET).
     */
    Result runFrame(Tms32031State& cpu);

    /// @return Bordas de subida do bit de watchdog escritas em 0x994000 até agora.
    uint64_t watchdogKicks() const { return cabinet_.watchdogKicks(); }
    /// @return Quantas INT3 foram atendidas no último quadro (esperado: 3).
    int adcInterruptsLastFrame() const { return adc_count_; }

private:
    /// @return true se a CPU aceitaria a interrupção @p ie_bit agora.
    static bool accepts(const Tms32031State& cpu, uint32_t ie_bit) {
        return (cpu.st & Tms32031State::kStGie) && (cpu.ie & ie_bit);
    }

    io::CabinetIo& cabinet_;  ///< Portas do gabinete (entradas, ADC, watchdog).
    irq::VblankHandler vblank_;  ///< Tratador da INT0 (VBLANK).
    irq::AdcHandler adc_;  ///< Tratador da INT3 (ADC).
    int adc_count_ = 0;  ///< INT3 atendidas no quadro atual.
    /// Limite de segurança para a cadeia de INT3 num quadro.
    static constexpr int kMaxAdcPerFrame = 8;
};

/**
 * @brief Relógio de espera ocupada que roda quadros do FrameRunner.
 *
 * @details Acumula as instruções informadas pelas rotinas. A cada
 * `vunit::kInstructionsPerFrame` (~431 600) executa um quadro: INT0/INT3,
 * respeitando GIE/IE. O watchdog de hardware é alimentado pela borda de subida do
 * bit 3 de `0x994000` (a `0x5029` da INT0 o inverte a cada quadro; o autoteste usa a
 * `0x503C`). Com um prazo ligado (`setHardwareWatchdog`), se nenhuma borda chegar nesse
 * número de quadros, pede o RESET.
 *
 * @note O MAME (`midvunit.cpp`) liga o bit 3 a `watchdog_timer_device::reset_line_w` sem
 * configurar o período; assim o dispositivo fica desligado (`m_enabled` = false) e nunca
 * reinicia a placa. A própria ROM confirma um prazo longo: o código de falha no LED
 * (`0x632F`) fica segundos sem alimentar o watchdog. Por isso o padrão é desligado (0).
 */
class FrameBusyWait : public IBusyWait {
public:
    /// Quadros seguidos sem interrupções até o watchdog de hardware agir (aproximação).
    /// Prazo do `watchdog_timer_device` do MAME quando ativo sem configuração: 3 s (~174 quadros).
    static constexpr uint32_t kMameDefaultWatchdogFrames = 174;

    /// @param runner Executor dos quadros (interrupções) chamado durante as esperas.
    explicit FrameBusyWait(FrameRunner& runner) : runner_(runner) {}

    bool spin(Tms32031State& cpu, uint32_t instructions) override;

    void feedWatchdog() override { starved_frames_ = 0; }

    /**
     * @brief Gancho chamado antes de cada quadro executado dentro de uma espera.
     * @details Com a CPU numa corrotina, o gancho devolve o controle à plataforma (que
     * apresenta o quadro anterior e lê as entradas) antes de a INT0 do quadro seguinte rodar.
     * Sem gancho, os quadros rodam em sequência (testes; partida antes da plataforma).
     */
    void setFrameHook(std::function<void()> hook) { hook_ = std::move(hook); }
    /// @brief Prazo do watchdog de hardware em quadros sem borda no bit 3 (0 = desligado, como no MAME).
    void setHardwareWatchdog(uint32_t frames) {
        hw_watchdog_frames_ = frames;
        starved_frames_ = 0;
    }

    /// @return true se um RESET foi pedido (watchdog de software ou de hardware).
    bool resetRequested() const { return reset_; }
    /// @return Quadros executados dentro de esperas desde o último takeFrames().
    uint64_t takeFrames() {
        const uint64_t n = frames_;
        frames_ = 0;
        return n;
    }
    /// @brief Limpa o pedido de RESET (depois de executá-lo).
    void clearReset() {
        reset_ = false;
        starved_frames_ = 0;
        acc_ = 0;
    }

private:
    FrameRunner& runner_;  ///< Executor dos quadros.
    std::function<void()> hook_;  ///< Gancho antes de cada quadro (ver setFrameHook()).
    uint64_t acc_ = 0;  ///< Instruções acumuladas desde o último quadro.
    uint64_t frames_ = 0;  ///< Quadros executados desde o último takeFrames().
    uint32_t starved_frames_ = 0;  ///< quadros seguidos sem borda no bit de watchdog
    uint64_t last_kicks_ = 0;  ///< Contagem de bordas do watchdog no último quadro.
    uint32_t hw_watchdog_frames_ = 0;  ///< Prazo do watchdog de hardware (0 = desligado).
    bool reset_ = false;  ///< RESET pedido.
};

} // namespace rea::core
