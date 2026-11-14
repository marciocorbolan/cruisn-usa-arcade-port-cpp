#pragma once
/**
 * @file input_manager.hpp
 * @brief Ponte entre a entrada da plataforma (SDL2...) e as portas do gabinete.
 */

#include <cstdint>

#include "core/interfaces.hpp"
#include "core/io/cabinet_io.hpp"

namespace rea::core::io {

/// Bits de IN0 (`0x991060`), ativos em 0. Fonte: MAME `midvunit` + rótulos do teste de chaves da ROM.
namespace in0 {
inline constexpr uint16_t kCoin1 = 0x0001;  ///< Ficha 1.
inline constexpr uint16_t kCoin2 = 0x0002;  ///< Ficha 2.
inline constexpr uint16_t kStart = 0x0004;  ///< START.
inline constexpr uint16_t kTilt = 0x0008;       ///< Slam switch.
inline constexpr uint16_t kTest = 0x0010;       ///< "ENTER" / chave de teste.
inline constexpr uint16_t kService = 0x0040;  ///< Serviço (crédito sem ficha).
inline constexpr uint16_t kCoin3 = 0x0080;  ///< Ficha 3.
inline constexpr uint16_t kVolumeDown = 0x0100;  ///< Volume −.
inline constexpr uint16_t kVolumeUp = 0x0200;  ///< Volume +.
inline constexpr uint16_t kGear4 = 0x0400;  ///< Marcha 4.
inline constexpr uint16_t kGear3 = 0x0800;  ///< Marcha 3.
inline constexpr uint16_t kGear2 = 0x1000;  ///< Marcha 2.
inline constexpr uint16_t kGear1 = 0x2000;  ///< Marcha 1.
inline constexpr uint16_t kCoin4 = 0x4000;  ///< Ficha 4.
} // namespace in0

/// Bits de IN1 (`0x991030`), ativos em 0.
namespace in1 {
inline constexpr uint16_t kRadio = 0x0002;  ///< Rádio (troca a música).
inline constexpr uint16_t kView1 = 0x0010;  ///< Vista 1.
inline constexpr uint16_t kView2 = 0x0020;  ///< Vista 2.
inline constexpr uint16_t kView3 = 0x0040;  ///< Vista 3.
} // namespace in1

/**
 * @brief DIP switches padrão (valores default do MAME para `crusnusa`).
 * @details SW2: Master, link desligado, Freeze off, Upright, Motion off,
 *          Test off. SW3: 1 contador de fichas, preço "USA-8" (0xF800).
 */
inline constexpr uint16_t kDefaultDsw = 0xF9FE;

/// Faixa útil do potenciômetro do volante (MAME: PORT_MINMAX 0x10–0xF0).
inline constexpr uint8_t kWheelMin = 0x10;
/// Fim da faixa útil do potenciômetro do volante.
inline constexpr uint8_t kWheelMax = 0xF0;

/**
 * @brief Lê a plataforma uma vez por quadro e atualiza as portas do gabinete.
 */
class InputManager {
public:
    /**
     * @brief Constrói o gerenciador.
     * @param input Fonte de entrada da plataforma.
     * @param cabinet Dispositivo de I/O a alimentar.
     * @param dsw DIP switches.
     */
    InputManager(IInput& input, CabinetIo& cabinet, uint16_t dsw = kDefaultDsw);

    /**
     * @brief Lê a plataforma e atualiza IN0, IN1, DIPs e canais do ADC.
     * @return Estado lido (útil para HUD de depuração e para `quit`).
     */
    ArcadeInputState update();

    /**
     * @brief Aplica um estado já lido (testes, replays).
     * @param s Estado dos controles.
     */
    void apply(const ArcadeInputState& s);

    /// @brief Codifica IN0 (ativa em 0); marcha 0 = neutro (nenhum bit de marcha em 0).
    static uint16_t encodeIn0(const ArcadeInputState& s);
    /// @brief Codifica IN1 (ativa em 0).
    static uint16_t encodeIn1(const ArcadeInputState& s);
    /// @brief Limita o volante à faixa física do potenciômetro.
    static uint8_t clampWheel(uint8_t v) { return v < kWheelMin ? kWheelMin : (v > kWheelMax ? kWheelMax : v); }

    /// @brief Altera as DIP switches.
    void setDsw(uint16_t dsw) { dsw_ = dsw; }

private:
    IInput& input_;  ///< Fonte das entradas do gabinete.
    CabinetIo& cabinet_;  ///< Portas que recebem as entradas codificadas.
    uint16_t dsw_;  ///< DIP switches atuais.
};

} // namespace rea::core::io
