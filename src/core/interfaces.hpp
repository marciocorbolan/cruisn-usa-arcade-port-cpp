#pragma once
/**
 * @file interfaces.hpp
 * @brief Interfaces puras (abstratas) entre a lógica do jogo e a plataforma.
 *
 * @details Nada em `src/core`, `src/physics`, `src/gameplay` ou `src/ui`
 * inclui SDL2. A plataforma (hoje `src/platform/sdl`) implementa estas
 * interfaces, permitindo trocar de biblioteca sem tocar na lógica traduzida.
 */

#include <cstddef>
#include <cstdint>
#include <string>

namespace rea::core {

/**
 * @brief Estado dos controles do gabinete, no formato que a lógica do jogo espera.
 *
 * @details Espelha as portas de I/O do gabinete original (volante analógico,
 * pedais analógicos, câmbio H de 4 marchas, botões de vista/rádio, moedeiros,
 * chaves do operador). Os valores analógicos usam 8 bits sem sinal, como o
 * ADC0844 da placa. A codificação para as portas (ativas em 0) fica no
 * `InputManager` (`src/core/io/input_manager.hpp`).
 */
struct ArcadeInputState {
    uint8_t wheel = 0x80;   ///< Volante: 0x00 = esquerda total, 0x80 = centro, 0xFF = direita total.
    uint8_t gas = 0x00;     ///< Acelerador: 0x00 solto … 0xFF fundo.
    uint8_t brake = 0x00;   ///< Freio: 0x00 solto … 0xFF fundo.
    uint8_t gear = 1;       ///< Marcha selecionada (1–4; 0 = neutro).
    bool coin1 = false;     ///< Moedeiro 1 (pulso).
    bool coin2 = false;     ///< Moedeiro 2 (pulso).
    bool coin3 = false;     ///< Moedeiro 3.
    bool coin4 = false;     ///< Moedeiro 4.
    bool service = false;   ///< Crédito de serviço (operador).
    bool tilt = false;      ///< Slam switch (sensor de pancada).
    bool volume_up = false;   ///< Volume + (dentro da porta do moedeiro).
    bool volume_down = false; ///< Volume − (dentro da porta do moedeiro).
    bool start = false;     ///< Botão START.
    bool view1 = false;     ///< Botão de vista 1.
    bool view2 = false;     ///< Botão de vista 2.
    bool view3 = false;     ///< Botão de vista 3.
    bool radio = false;     ///< Botão de rádio (troca de música).
    bool test = false;      ///< Chave de teste/serviço do operador.
    bool quit = false;      ///< Pedido de encerramento (somente PC).
};

/// @brief Fonte de entrada (teclado, joystick, volante USB...).
class IInput {
public:
    virtual ~IInput() = default;
    /**
     * @brief Processa eventos pendentes e devolve o estado atual.
     * @return Estado dos controles do gabinete.
     */
    virtual ArcadeInputState poll() = 0;
};

/// @brief Saída de vídeo.
class IRenderer {
public:
    virtual ~IRenderer() = default;
    /// @brief Inicia um quadro (limpa o back buffer).
    virtual void beginFrame() = 0;
    /**
     * @brief Desenha um retângulo sólido (primitiva mínima de depuração).
     * @param x,y,w,h Retângulo em coordenadas da tela original (512×400).
     * @param rgba Cor 0xRRGGBBAA.
     */
    virtual void fillRect(int x, int y, int w, int h, uint32_t rgba) = 0;
    /**
     * @brief Copia uma imagem inteira (ex.: a página exibida da VRAM) para o back buffer.
     * @param rgba Pixels 0xRRGGBBAA, linha a linha.
     * @param w,h Dimensões da imagem (512×400 no V-Unit).
     */
    virtual void blit(const uint32_t* rgba, int w, int h) = 0;
    /**
     * @brief Apresenta o quadro — chamado apenas no VBLANK simulado.
     */
    virtual void present() = 0;
};

/// @brief Saída de áudio da plataforma: recebe o PCM gerado pela placa DCS (programa traduzido, hardware simulado; ver `audio::AudioManager`).
class IAudio {
public:
    virtual ~IAudio() = default;
    /**
     * @brief Enfileira amostras para tocar.
     * @param samples PCM mono, 16 bits com sinal, a 31 250 Hz (`audio::AudioManager::kSampleRate`).
     * @param count Número de amostras.
     */
    virtual void queue(const int16_t* samples, std::size_t count) = 0;
};

} // namespace rea::core
