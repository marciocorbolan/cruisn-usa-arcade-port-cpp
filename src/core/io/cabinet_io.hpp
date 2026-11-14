#pragma once
/**
 * @file cabinet_io.hpp
 * @brief Portas de entrada do gabinete e conversor A/D ADC0844 (V-Unit).
 *
 * @details Mapa conforme `williams/midvunit.cpp` (MAME 0.289), conferido com a
 * rotina de leitura `0x4CEF` e a INT3 da ROM:
 *
 * | Endereço   | Leitura                                   |
 * |------------|-------------------------------------------|
 * | `0x991030` | IN1 (rádio, vistas)                        |
 * | `0x991060` | IN0 (fichas, start, teste, volume, marchas)|
 * | `0x992000` | DIP switches SW2 (bits 0–7) e SW3 (8–15)   |
 * | `0x993000` | ADC0844: amostra nos bits 24–31            |
 * | `0x995000` | 0 (latch só de escrita; sem placa de movimento) |
 *
 * As portas de 16 bits aparecem nas duas metades da palavra (o jogo usa a
 * baixa para IN0/IN1 e a alta para as DIPs). O ADC só aceita comando com o
 * bit 5 de `0x994000` em 0 e só devolve dado com o bit 6 em 0.
 */

#include <array>
#include <cstdint>

#include "core/memory_bus.hpp"

namespace rea::core::io {

/// Endereços de I/O do gabinete.
inline constexpr uint32_t kPortIn1 = 0x991030;
/// Latch serial `0x995000` (saídas e UART do movimento): leitura = 0, sem placa de movimento.
inline constexpr uint32_t kPortSerial = 0x995000;
inline constexpr uint32_t kPortIn0 = 0x991060;  ///< IN0: fichas, START, teste, serviço, volume e marchas (ativos em 0).
inline constexpr uint32_t kPortDsw = 0x992000;  ///< Bit 0x40 da metade alta: sem placa de movimento.
inline constexpr uint32_t kPortAdc = 0x993000;  ///< Conversor A/D (volante/pedais); dado nos bits 24–31.
inline constexpr uint32_t kPortControl = 0x994000;  ///< Controle: bit7 LED, bit3 watchdog, bit1 reset do som DCS.

/**
 * @brief Conversor A/D de 4 canais ADC0844 (modo single-ended).
 * @details Canais do Cruis'n USA: 1 = volante, 2 = acelerador, 3 = freio.
 *          Comando de multiplexador (MA3–MA0): 4–7 = canais 1–4 single-ended.
 */
class Adc0844 {
public:
    /// @brief Define a tensão (0–255) de um canal (1–4).
    void setChannel(int ch, uint8_t value) { if (ch >= 1 && ch <= 4) inputs_[ch - 1] = value; }
    /**
     * @brief Inicia uma conversão.
     * @param mux Endereço do multiplexador (bits MA3–MA0).
     * @note Modos diferenciais (mux 0–3, 8–11) não são usados pelo jogo e devolvem 0.
     */
    void write(uint8_t mux) {
        const uint8_t m = mux & 0x0F;
        result_ = (m & 0x04) ? inputs_[m & 0x03] : 0;
    }
    /// @return Resultado da última conversão.
    uint8_t read() const { return result_; }

private:
    std::array<uint8_t, 4> inputs_{0x80, 0x00, 0x00, 0x00};  ///< Tensão de cada canal (0–255); o volante começa no centro.
    uint8_t result_ = 0;  ///< Resultado da última conversão.
};

/**
 * @brief Dispositivo de I/O com as portas digitais, DIP switches e o ADC.
 */
class CabinetIo : public IIoDevice {
public:
    /// @brief Define IN0 (ativa em 0).
    void setIn0(uint16_t v) { in0_ = v; }
    /// @brief Define IN1 (ativa em 0).
    void setIn1(uint16_t v) { in1_ = v; }
    /// @brief Define as DIP switches (SW2 nos bits 0–7, SW3 nos bits 8–15).
    void setDsw(uint16_t v) { dsw_ = v; }
    /// @brief Acesso ao ADC (para definir volante/pedais).
    Adc0844& adc() { return adc_; }

    /// @return true se uma conversão terminou e a INT3 está pendente.
    bool int3Pending() const { return int3_pending_; }
    /// @brief Bordas de subida do bit 3 de 0x994000 (alimentação do watchdog de hardware, `reset_line_w` do MAME).
    uint64_t watchdogKicks() const { return watchdog_kicks_; }
    /// @brief Reconhece a INT3 (a CPU começou a atendê-la).
    void ackInt3() { int3_pending_ = false; }

    bool ioRead(uint32_t addr, uint32_t& value) override;
    void ioWrite(uint32_t addr, uint32_t value) override;

private:
    uint16_t in0_ = 0xFFFF;  ///< Último valor de IN0 (ativo em 0).
    uint16_t in1_ = 0xFFFF;  ///< Último valor de IN1 (ativo em 0).
    uint16_t dsw_ = 0xFFFF;  ///< DIP switches (SW2 nos bits 0–7, SW3 nos bits 8–15).
    uint32_t control_ = 0xFFFF;  ///< Último valor escrito em 0x994000.
    uint64_t watchdog_kicks_ = 0;  ///< Bordas de subida do bit 3 (watchdog) de 0x994000.
    Adc0844 adc_;  ///< Conversor A/D do volante e dos pedais.
    bool int3_pending_ = false;  ///< Conversão terminada, INT3 pendente.
};

} // namespace rea::core::io
