#pragma once
/**
 * @file link_packet.hpp
 * @brief Montagem do pacote de envio do link (`0x7989`, `0x798F`, `0x7998`, `0x79AF`, `0x7A41`).
 *
 * @details A cada iteração o loop principal:
 * 1. **Começa** um pacote novo (`0x7989`: tamanho 0 + mensagem de estado tipo 1).
 * 2. **Anexa** mensagens ao longo do quadro (`0x7998` bloco, `0x79AF` palavra).
 * 3. **Termina** com uma palavra 0 (`0x798F`).
 *
 * A troca de pacotes (`0x7F5D`, no início da iteração seguinte) envia `[C8D3]`
 * palavras de `[0x7938]` = 0x1F000. Só o byte baixo de cada palavra vai para o
 * cabo, por isso valores maiores são quebrados em bytes (`LSH −8`) pela
 * mensagem.
 *
 * As anexações só ocorrem com a DIP "Linking" ligada (`[C8CC] & 4 == 0`) e
 * enquanto o pacote couber em 0x258 palavras. Senão, a mensagem é descartada
 * em silêncio.
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::link {

/// Constantes do pacote.
namespace packet {
inline constexpr uint32_t kScratchPtr = 0x7939;  ///< `[0x7939]` = 0x809A1B: rascunho das mensagens (RAM interna).
inline constexpr uint32_t kMaxWords   = 0x258;   ///< Tamanho máximo do pacote.
inline constexpr uint32_t kStatusMode = 0xC8C5;  ///< Modo do jogo (4 bytes na mensagem 1).
inline constexpr uint32_t kVarC8DA    = 0xC8DA;  ///< 2 bytes na mensagem 1.
inline constexpr uint32_t kVarC8E4    = 0xC8E4;  ///< 1 enquanto a tela de espera do link está montada (`0x16E6`; a `0x16FF` zera).
inline constexpr uint32_t kVarE607    = 0xE607;  ///< posição do focado (1 = líder)
inline constexpr uint32_t kVarC8E8    = 0xC8E8;  ///< Caso da atração/partida (−1 a −7).
inline constexpr uint32_t kFloatE639  = 0xE639;  ///< Float TMS; enviado como FIX(valor × 100).
} // namespace packet

/**
 * @brief `0x79AF`: anexa a palavra AR2 ao pacote.
 * @details `n = [C8D3] + 1`. Se `n ≤ 0x258`: `[C8D3] = n` e `[0x7938 + n − 1] = AR2`.
 *          Preserva R0 e AR0.
 */
void appendWord(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x7998`: anexa RC + 1 palavras a partir de AR2.
 * @details Se couber, copia o bloco e deixa RC = −1 (fim do `RPTB`; RC não é
 *          salvo). Senão, ou com o link desligado, RC fica intacto.
 *          Preserva AR0, AR2, AR3 e R0.
 */
void appendBlock(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x7A41`: mensagem de estado (tipo 1, 11 palavras) no rascunho, anexada com `0x7998`.
 * @details Conteúdo:
 *
 * | Palavra | Valor |
 * |---|---|
 * | 0 | `1` (tipo) |
 * | 1–4 | `[C8C5]` em bytes, LSB primeiro |
 * | 5–6 | `[C8DA]` em bytes |
 * | 7 | `[C8E4]` |
 * | 8 | `[E607]` |
 * | 9 | `[C8E8]` |
 * | 10 | `FIX([E639] × 100.0)` |
 *
 * Saída: AR2 = início do rascunho; RC = 10 (ou −1 se anexou).
 */
void buildStatusMessage(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x7989`: começa o pacote do quadro (`[C8D3] = 0`) com a mensagem de estado.
 * @details Preserva R0.
 */
void beginPacket(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x798F`: termina o pacote anexando a palavra 0. Preserva R0, AR0 e AR2.
 */
void endPacket(Tms32031State& cpu, MemoryBus& bus);

/// @brief Registra as rotinas deste módulo no despachante.
void registerPacketRoutines(SubroutineDispatcher& d);

} // namespace rea::core::link
