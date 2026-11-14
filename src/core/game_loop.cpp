/**
 * @file game_loop.cpp
 * @brief Implementação do loop com passo fixo.
 */
#include "core/game_loop.hpp"

namespace rea::core {

GameLoop::GameLoop(IClock& clock, IInput& input, double refresh_hz)
    : clock_(clock), input_(input), step_(1.0 / refresh_hz) {}

uint64_t GameLoop::run(uint64_t max_frames) {
    uint64_t frame = 0;
    double previous = clock_.now();
    double accumulator = 0.0;
    double window_start = previous;  // janela de medição da velocidade
    uint64_t window_frames = 0;
    bool warned = false;

    while (max_frames == 0 || frame < max_frames) {
        const double current = clock_.now();
        accumulator += current - previous;
        previous = current;

        // Limita o atraso acumulado (ex.: janela arrastada, breakpoint).
        if (accumulator > step_ * kMaxCatchUp) accumulator = step_ * kMaxCatchUp;

        bool presented = false;
        while (accumulator >= step_ && (max_frames == 0 || frame < max_frames)) {
            const ArcadeInputState in = input_.poll();
            if (in.quit) return frame;
            if (tick_) tick_(in, frame);
            ++frame;
            ++window_frames;
            accumulator -= step_;
            presented = true;
        }
        // VBLANK: apresenta uma vez por iteração, após a lógica, como a placa
        // que só troca o buffer de vídeo dentro do intervalo de retraço.
        if (presented && vblank_) vblank_(frame);

        // Velocidade: quadros emulados × duração do quadro ÷ tempo real da janela.
        if (const double elapsed = current - window_start; elapsed >= kSlowWindowS) {
            const double speed = static_cast<double>(window_frames) * step_ / elapsed;
            if (speed < kSlowLimit && slow_ && !warned) {
                warned = true;
                slow_(speed);
            }
            window_start = current;
            window_frames = 0;
        }

        if (accumulator < step_) clock_.sleep((step_ - accumulator) * 0.9);
    }
    return frame;
}

} // namespace rea::core
