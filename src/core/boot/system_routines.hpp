#pragma once
/**
 * @file system_routines.hpp
 * @brief Sub-rotinas de sistema chamadas pelo RESET (vídeo, watchdog, link, BSS).
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::boot {

/// Bits das DIP switches (cópia em RAM `0xC8CC`, lida de `0x992000 >> 16`).
namespace dip {
inline constexpr uint32_t kLinkSlave = 1u << 0;   ///< SW2:8 "Link Status": 0 = Master, 1 = Slave.
inline constexpr uint32_t kLinkingOff = 1u << 2;  ///< SW2:6 "Linking": 1 = desligado (ativo em 0).
} // namespace dip

/// Variável RAM com a cópia das DIP switches.
inline constexpr uint32_t kDipSwitchesVar = 0xC8CC;

/**
 * @brief `0x4ECC`: copia a tabela de timing de vídeo (12 palavras) para `0x980020`.
 * @details A tabela fica no endereço guardado em `[0x4ECB]` (= `0xC102DA`, ROM):
 *          htotal 666, vtotal 432, INT0 na scanline 399. Saída: RC = −1.
 */
void loadVideoTiming(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x4E1B`: se `[0xC939] != 0`, desvia para `0x0699` (BR, não retorna aqui).
 * @details `LDI @($C939),R0 / RETSEQ / BR $0699`. Saída: R0 = `[0xC939]`.
 */
void checkPendingC939(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x4E20`: zera as variáveis `0xC8C4–0xC8EC` e chama `0x793A`.
 */
void clearSystemVars(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x4E35`: zera a área de variáveis `0xC8EE–0x1EFFE` (BSS do jogo).
 * @details Limites lidos de `[0x4E33]`/`[0x4E34]`. Revela o layout da RAM:
 *          código abaixo de `0xC8C4`, variáveis acima.
 */
void clearBss(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x5029`: alimenta o watchdog (inverte o bit 3 de `0x994000`).
 * @details Mantém a cópia em `[0xC8C7]`. Usa wait states 0x470 durante a escrita.
 */
void kickWatchdog(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x4EE3`: pulsos de controle com interrupções desligadas.
 * @details Escreve `0x994000` com o bit 0 em 0 e depois o valor da cópia
 *          (pulso em bit 0), religa as interrupções e pulsa o bit 1 de
 *          `0x980080` (0xA → 0x8). Saída: R0 = 8, R1 = cópia & ~1, DP = 0.
 */
void pulseControl(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x4EF9`: liga o timer 0 (`0x808020 |= GO | HLD` = 0xC0).
 */
void startTimer0(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x7F06`: configura a placa de comunicação (link) em `0x997000/1`.
 * @details Master → `0xC0000000`, Slave → `0x30000000`; `0x997001 = 0x20000000`.
 */
void setupLinkBoard(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x7F18`: habilita a INT2 (sincronismo de quadro) se Slave com link ligado.
 * @details Grava 4 (EINT2) na máscara salva `[0x8099DF]` e faz `IE |= máscara`.
 */
void enableLinkInterrupt(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x5020`: prepara a volta ao atrato após uma tela de recuperação.
 * @details `CALL $95CB`, `CALL $9E80`, `AR2 = [B0AA]`, `CALL $9F2F` (pendentes);
 * depois `[C938] = 0x4210` (cor de fundo RGB555: R=G=B=16, cinza médio) e
 * `[C8C5] = 1` (modo 1).
 */
void prepareAttract(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra todas as rotinas deste módulo no despachante.
void registerRoutines(SubroutineDispatcher& d);

} // namespace rea::core::boot
