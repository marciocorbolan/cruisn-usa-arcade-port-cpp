#pragma once
/**
 * @file game_loop.hpp
 * @brief Loop principal com passo de tempo fixo e VBLANK simulado.
 */

#include <cstdint>
#include <functional>

#include "core/interfaces.hpp"

namespace rea::core {

/**
 * @brief Relógio monotônico abstrato (permite testes determinísticos).
 */
class IClock {
public:
    virtual ~IClock() = default;
    /// @return Tempo atual em segundos.
    virtual double now() = 0;
    /// @brief Cede a CPU por aproximadamente @p seconds.
    virtual void sleep(double seconds) = 0;
};

/**
 * @brief Executa a lógica na taxa original do arcade, independentemente do monitor.
 *
 * @details Modelo *fixed timestep* com acumulador: o tempo real decorrido é
 * acumulado e consumido em fatias exatas de `1 / refresh_hz`. Cada fatia
 * equivale a um quadro do hardware original e termina com o sinal de VBLANK,
 * que é o único momento em que o vídeo é apresentado — o mesmo contrato da
 * interrupção de VBLANK da placa: INT0 do TMS320C31 (vetor 0x4C34), disparada
 * na scanline 399, a última visível. Taxa original: ≈57,928 Hz
 * (`vunit::kRefreshHz`, de htotal 666 × vtotal 432 lidos da ROM).
 */
class GameLoop {
public:
    /// Callback de um quadro de lógica (equivale ao corpo do loop principal da ROM).
    using TickFn = std::function<void(const ArcadeInputState&, uint64_t frame)>;
    /// Callback do VBLANK (equivale ao tratador da interrupção de vídeo).
    using VblankFn = std::function<void(uint64_t frame)>;
    /// Aviso de lentidão: @p speed = tempo emulado / tempo real numa janela de `kSlowWindowS` s (< `kSlowLimit`).
    using SlowFn = std::function<void(double speed)>;

    static constexpr double kSlowWindowS = 5.0;  ///< Janela de medição da velocidade, em segundos reais.
    static constexpr double kSlowLimit = 0.95;   ///< Abaixo disso (95 % do tempo real) o jogo está lento.

    /**
     * @brief Constrói o loop.
     * @param clock Fonte de tempo.
     * @param input Fonte de entrada.
     * @param refresh_hz Taxa de quadros original do arcade.
     */
    GameLoop(IClock& clock, IInput& input, double refresh_hz);

    /// @brief Define a lógica executada a cada quadro.
    void onTick(TickFn fn) { tick_ = std::move(fn); }
    /// @brief Define o tratador do VBLANK simulado.
    void onVblank(VblankFn fn) { vblank_ = std::move(fn); }
    /// @brief Chamado uma vez, na primeira janela em que o host não acompanha o tempo real do arcade.
    /// @details Quando o host é lento, o atraso acima de `kMaxCatchUp` quadros é descartado e o jogo anda em
    ///          câmera lenta; sem este aviso isso passava despercebido.
    void onSlow(SlowFn fn) { slow_ = std::move(fn); }

    /**
     * @brief Roda até `quit` ou até @p max_frames quadros (0 = sem limite).
     * @param max_frames Limite de quadros (útil para testes headless).
     * @return Número de quadros de lógica executados.
     */
    uint64_t run(uint64_t max_frames = 0);

private:
    IClock& clock_;  ///< Relógio do host.
    IInput& input_;  ///< Fonte das entradas do gabinete.
    double step_;  ///< Duração de um quadro do arcade, em segundos (1 / 57,928 Hz).
    TickFn tick_;  ///< Passo da lógica, chamado uma vez por quadro do arcade.
    VblankFn vblank_;  ///< Chamado depois de cada quadro de lógica (apresentação).
    SlowFn slow_;  ///< Aviso de lentidão (uma vez por execução).
    /// Máximo de quadros recuperados por iteração (evita "espiral da morte").
    static constexpr int kMaxCatchUp = 5;
};

} // namespace rea::core
