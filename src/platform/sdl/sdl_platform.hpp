#pragma once
/**
 * @file sdl_platform.hpp
 * @brief Implementações SDL2 das interfaces puras de `core/interfaces.hpp`.
 *
 * @details Única parte do projeto que inclui SDL2. Para migrar de biblioteca,
 * basta reescrever esta pasta.
 */

#include <SDL.h>

#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "core/game_loop.hpp"
#include "core/interfaces.hpp"

namespace rea::platform::sdl {

/// Resolução nativa da tela do V-Unit usada como espaço lógico de desenho.
inline constexpr int kScreenW = 512;
/// Altura nativa da tela do V-Unit (espaço lógico de desenho).
inline constexpr int kScreenH = 400;

/**
 * @brief Janela + renderer SDL2 com escala lógica fixa (512×400).
 */
class SdlRenderer final : public core::IRenderer {
public:
    /**
     * @brief Cria janela e renderer.
     * @param title Título da janela (UTF-8).
     * @param scale Fator inicial de escala da janela.
     */
    SdlRenderer(const char* title, int scale);
    ~SdlRenderer() override;
    SdlRenderer(const SdlRenderer&) = delete;
    SdlRenderer& operator=(const SdlRenderer&) = delete;

    /// @return true se janela e renderer foram criados.
    bool ok() const { return renderer_ != nullptr; }
    /// @brief Atualiza o título (ex.: texto localizado).
    void setTitle(const char* title);
    /// @brief Alterna entre janela e tela cheia (na resolução da área de trabalho; a proporção 512 × 400 é mantida).
    void toggleFullscreen();

    void beginFrame() override;
    void fillRect(int x, int y, int w, int h, uint32_t rgba) override;
    void blit(const uint32_t* rgba, int w, int h) override;
    void present() override;

private:
    SDL_Window* window_ = nullptr;  ///< Janela SDL.
    SDL_Renderer* renderer_ = nullptr;  ///< Renderizador SDL da janela.
    SDL_Texture* frame_ = nullptr;  ///< Textura de streaming para blit().
    int frame_w_ = 0;  ///< Largura atual de `frame_`.
    int frame_h_ = 0;  ///< Altura atual de `frame_`.
};

/**
 * @brief Mapeia teclado e game controller para os controles do gabinete.
 *
 * @details Teclado: ←/→ volante, ↑ acelerador, ↓ freio, 1–4 marchas (0 neutro),
 * 5–8 fichas 1–4, 9 serviço, T slam tilt (sensor de pancada), Enter start, F1/F3/F4 vistas 1–3,
 * R rádio, F2 teste ("ENTER"), −/= volume, Esc sair (no navegador o Esc não fecha o jogo).
 * Controle: analógico esquerdo = volante, RT = acelerador, LT = freio.
 */
class SdlInput final : public core::IInput {
public:
    SdlInput();
    ~SdlInput() override;
    SdlInput(const SdlInput&) = delete;
    SdlInput& operator=(const SdlInput&) = delete;
    core::ArcadeInputState poll() override;
    /// @return true uma vez a cada F11 ou Alt+Enter (pedido de alternar a tela cheia; só no PC).
    bool takeFullscreenToggle() {
        const bool t = fullscreen_toggle_;
        fullscreen_toggle_ = false;
        return t;
    }

private:
    /// Abre o controle @p index se ainda não há um aberto.
    void openPad(int index);
    /// Fecha o controle aberto.
    void closePad();

    SDL_GameController* pad_ = nullptr;  ///< Game controller aberto, se houver.
    SDL_JoystickID pad_id_ = -1;  ///< ID de instância do controle aberto (para a desconexão).
    uint8_t gear_ = 1;  ///< Marcha atual (1–4; 0 = neutro), trocada pelas teclas 0–4 ou pelo controle.
    bool fullscreen_toggle_ = false;  ///< F11/Alt+Enter apertado desde a última consulta.
};

/**
 * @brief Saída de áudio SDL2: toca o PCM que a placa DCS traduzida (`core::audio::AudioManager`) gera.
 */
class SdlAudio final : public core::IAudio {
public:
    SdlAudio();
    ~SdlAudio() override;
    /// @brief Enfileira amostras mono S16 a 31 250 Hz (saída da placa DCS).
    /// @details Mantém a fila entre ~40 e ~140 ms tirando ou repetindo 1 amostra em 32; acima de ~0,5 s
    ///          (travada longa) o quadro é descartado.
    void queue(const int16_t* samples, std::size_t count) override;

private:
    SDL_AudioDeviceID device_ = 0;  ///< Dispositivo de saída de áudio SDL.
    std::vector<int16_t> adjusted_;  ///< Bloco com a correção de fila aplicada (reaproveitado entre quadros).
};

/// @brief Relógio baseado em SDL_GetPerformanceCounter.
class SdlClock final : public core::IClock {
public:
    double now() override {
        return static_cast<double>(SDL_GetPerformanceCounter()) /
               static_cast<double>(SDL_GetPerformanceFrequency());
    }
    void sleep(double seconds) override {
#ifdef __EMSCRIPTEN__
        // Navegador: sempre devolve o controle à página (eventos, desenho, áudio), mesmo com 0 ms;
        // o ASYNCIFY retoma o laço de quadros depois.
        emscripten_sleep(seconds > 0.0 ? static_cast<unsigned>(seconds * 1000.0) : 0u);
#else
        if (seconds > 0.001) SDL_Delay(static_cast<Uint32>(seconds * 1000.0));
#endif
    }
};

} // namespace rea::platform::sdl
