#pragma once
/**
 * @file input_routines.hpp
 * @brief Tradução da amostragem de entradas (`0x4CEF`) e das ações do operador.
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::io {

/// Variáveis RAM das entradas.
namespace input_vars {
inline constexpr uint32_t kHistoryPtr = 0x4CEE;  ///< [0x4CEE] = 0xC8C9 (estrutura de debounce).
inline constexpr uint32_t kSample1 = 0xC8C9;     ///< Amostra bruta anterior (IN1<<16 | IN0, ativa em 0).
inline constexpr uint32_t kSample2 = 0xC8CA;     ///< Amostra bruta de dois quadros atrás.
inline constexpr uint32_t kNewPresses = 0xC8CB;  ///< Bordas "recém-apertado" acumuladas (consumidas pelo jogo).
inline constexpr uint32_t kDipSwitches = 0xC8CC; ///< DIPs (0x992000 >> 16).
inline constexpr uint32_t kPressed = 0xC937;     ///< Nível atual invertido (1 = apertado).
inline constexpr uint32_t kTestRequest = 0xC939; ///< != 0 → `0x4E1B` abre o menu de teste.
inline constexpr uint32_t kAdcState = 0xC93D;    ///< Estado da INT3 (0 volante, 1 acelerador, 2 freio, 3 fim).
inline constexpr uint32_t kVolumeShown = 0xE56D; ///< Quadros de exibição do volume (0x3C).
inline constexpr uint32_t kGameMode = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.
inline constexpr uint32_t kControlShadow = 0xC8C7;  ///< Cópia do último valor escrito em `0x994000`: bits 0x40/0x20 selecionam a leitura do ADC; 0x80 = pipeline em execução.
} // namespace input_vars

/// Bits na palavra combinada IN1<<16 | IN0 (mesmos de `in0::`/`in1::` + deslocamento).
namespace input_bits {
inline constexpr uint32_t kTest = 0x00000010;  ///< Chave de teste.
inline constexpr uint32_t kVolumeDown = 0x00000100;  ///< Volume −.
inline constexpr uint32_t kVolumeUp = 0x00000200;  ///< Volume +.
} // namespace input_bits

/// Itens da CMOS usados pelo volume.
inline constexpr uint32_t kCmosVolume = 0x08;     ///< Volume atual (0–255).
inline constexpr uint32_t kCmosMinVolume = 0x20;  ///< "MIN VOLUME LEVEL".

/**
 * @brief `0x4CEF`: amostra as entradas digitais e dispara a leitura analógica.
 *
 * @details
 *  - DIPs → `[0xC8CC]`; palavra = `IN1 << 16 | IN0` (ativa em 0).
 *  - `[0xC937]` = palavra invertida (nível: 1 = apertado).
 *  - **Debounce:** bit novo em `[0xC8CB]` só quando estava solto nas DUAS
 *    amostras anteriores e está apertado agora:
 *    `novos |= amostra1 & amostra2 & ~agora`.
 *  - Inicia a conversão do ADC no canal 1 (volante); a INT3 encadeia os outros.
 *  - Borda na chave de teste → `0x4D61`. Fora dos modos 1/2, volume ± por
 *    **nível** (repete a cada quadro enquanto apertado).
 *  - Leituras "falsas" de RAM[0] após cada acesso de I/O (`LDI *AR0,R2`) são
 *    preservadas: provavelmente dão tempo ao barramento lento.
 */
void sampleInputs(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x4D61`: borda da chave de teste — alterna o pedido de menu de teste.
 * @details No modo 1 (boot) a borda fica guardada para depois.
 */
void handleTestSwitch(Tms32031State& cpu, MemoryBus& bus);

/** @brief `0x4D3D`: volume +1 (mínimo = item 0x20), grava na CMOS e envia ao som. */
void volumeUp(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/** @brief `0x4D28`: volume −1 (mínimo = item 0x20), grava na CMOS e envia ao som. */
void volumeDown(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra as rotinas deste módulo no despachante.
void registerRoutines(SubroutineDispatcher& d);

} // namespace rea::core::io
