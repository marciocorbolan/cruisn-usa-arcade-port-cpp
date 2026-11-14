#pragma once
/**
 * @file cabinet_outputs.hpp
 * @brief Saídas do gabinete chamadas pela INT0: lâmpadas (`0x4E4F`), motor do volante
 *        (`0xAFC2`, `0xB001`) e a sequência secreta de botões (`0x4FAE`).
 *
 * @details **Porta `0x995000`** (latch serial das saídas). Cada escrita é um comando de 2 fases:
 * 1. endereço: `0xFFFFFF00 | n`, `0xFFFFF700 | n`, `0xFFFFFF00 | n` (`LDI` com sinal);
 * 2. dado: `0xFF00 | d`, `0xFB00 | d`, `0xFF00 | d` (`OR` com imediato sem sinal).
 *
 * O original espera 21 ciclos (`RPTS 0x14` de `NOP`) entre as escritas; o port só as reproduz.
 *
 * | Endereço | Uso |
 * |---|---|
 * | 4 | força do motor do volante (`0xB001`) |
 * | 5 | lâmpadas (`0x4E4F`): bit 0 = START; bits 6–7 = `0xC0` fora do jogo |
 *
 * **Lâmpadas (`0x4E4F`)**:
 * - modo 1: START pisca pelo contador `[0x8099E2]` (aceso 0–29, apagado 30–49);
 * - modo 2 sem jogo grátis e sem créditos: a cada 60 quadros (`[C97D]`) avança `[C97C]`
 *   (0–2) e acende o padrão `[[4FD7] + i]` = 2, 4, 8;
 * - demais: o bit 0 de `[C8CD]` (START) pisca (aceso 0–29, apagado 30–59). Só escreve se mudou
 *   (`[C943]`) ou com `[C8E3]` ≠ 0. Os bits `0xC0` ficam ligados, exceto com
 *   `[C8E3]` ≠ 0 e `[E867]` = 0.
 *
 * **Motor do volante (`0xAFC2`)**, por modo (`[C8C5]` & 0xF):
 *
 * | Modo | Ganho `[C8EB]` | Limite `[EA4A]` | Amortecimento `[EA4B]` |
 * |---|---|---|---|
 * | 4 (corrida) | `[C8EB]` (mantém) | 126 | `[C8EB]` |
 * | 5 | 2,0 | 126 | 2,5 |
 * | 3 | 0,5 | 126 | 2,5 |
 * | 6 | 0,75 | 126 | 2,5 |
 * | 1 | 0 | 126 | 0 |
 * | demais (0, 2, 7…) | 0 | 126 | 2,5 |
 *
 * Força = clamp(ganho · (`[EA48]` − volante) − 2,2002 · amort. · vel, ±limite), com
 * vel = 0,55 · vel + 0,45 · clamp(Δvolante, ±22) (`[C8EC]`). Vai para `[EA49]` e,
 * como `FIX & 0xFF`, para o endereço 4 da porta.
 *
 * **Sequência secreta (`0x4FAE`)**, no modo 2: `[C937]` & `[4FAD]` deve seguir a tabela
 * `[4FAC]`. Cada acerto avança `[C97A]` e dá 120 quadros (`[C97B]`) para o próximo. No
 * 7º acerto, `BR $0669` (auditorias). Esse salto sai da INT0 sem retornar: o port lança
 * NonLocalJump{0x0669}, que a corrotina da CPU captura no nível mais externo.
 */

#include "core/subroutines.hpp"

namespace rea::core::io {

/// Variáveis das saídas.
namespace out_vars {
inline constexpr uint32_t kOutputPort   = 0x995000;  ///< Latch serial das saídas (comandos de 2 fases, ver acima).
inline constexpr uint32_t kGameMode     = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.
inline constexpr uint32_t kStartLamp    = 0xC8CD;   ///< Bit 0: lâmpada START.
inline constexpr uint32_t kBlinkCounter = 0x8099E2; ///< `@($99E2)` com DP = 0x80.
inline constexpr uint32_t kLastLamps    = 0xC943;  ///< Último valor das lâmpadas escrito na porta.
inline constexpr uint32_t kForceLamps   = 0xC8E3;  ///< != 0: força a escrita das lâmpadas.
inline constexpr uint32_t kLampMask     = 0xE867;  ///< Com `[C8E3]` != 0 e este 0, desliga os bits 0xC0 das lâmpadas.
inline constexpr uint32_t kIdleTimer    = 0xC97D;  ///< Quadros até avançar o padrão das lâmpadas na atração (60).
inline constexpr uint32_t kIdleStep     = 0xC97C;  ///< Padrão atual das lâmpadas na atração (0–2).
inline constexpr uint32_t kIdlePatterns = 0x4FD7;   ///< `[4FD7]` = 0x4FD8: 2, 4, 8.
inline constexpr uint32_t kWheelGain    = 0xC8EB;  ///< Ganho da força do motor do volante.
inline constexpr uint32_t kWheelPrev    = 0xC8EA;  ///< Volante no quadro anterior.
inline constexpr uint32_t kWheelVel     = 0xC8EC;  ///< Velocidade filtrada do volante.
inline constexpr uint32_t kWheelPos     = 0xC8C8;   ///< Volante (inteiro, da INT3).
inline constexpr uint32_t kWheelCenter  = 0xEA48;   ///< FLOAT(item 5 da CMOS).
inline constexpr uint32_t kWheelForce   = 0xEA49;  ///< Força calculada do motor do volante.
inline constexpr uint32_t kWheelLimit   = 0xEA4A;  ///< Limite da força (126).
inline constexpr uint32_t kWheelDamp    = 0xEA4B;  ///< Amortecimento da força.
inline constexpr uint32_t kButtons      = 0xC937;  ///< Nível atual invertido (1 = apertado).
inline constexpr uint32_t kSecretStep   = 0xC97A;  ///< Acertos da sequência secreta (no 7º abre as auditorias).
inline constexpr uint32_t kSecretTimer  = 0xC97B;  ///< Quadros restantes para o próximo acerto (120).
inline constexpr uint32_t kSecretTable  = 0x4FAC;  ///< `[4FAC]`: tabela da sequência secreta.
inline constexpr uint32_t kSecretMask   = 0x4FAD;  ///< `[4FAD]`: máscara dos botões comparados.
inline constexpr uint32_t kDipPort      = 0x992000; ///< Bit 0x40 da metade alta: sem placa de movimento.
inline constexpr uint32_t kMotionPort   = 0x991030; ///< Entrada lida pela `0x4996`.
inline constexpr uint32_t kMotionMask   = 0x4995;   ///< Máscara da `0x4996`.
inline constexpr uint32_t kMotionPending = 0xEBB7;  ///< 1 = o controlador de movimento pediu atenção.
} // namespace out_vars

/// @brief Escrita de 2 fases na porta `0x995000`: endereço @p addr e dado @p data (8 bits).
void writeOutput(MemoryBus& bus, uint32_t addr, uint32_t data);

/// @brief `0x4E4F`: lâmpadas (ver o arquivo). DP = 0 na saída.
void updateLamps(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0xAFC2`: força do motor do volante (ver o arquivo).
void updateWheelMotor(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x4FAE`: sequência secreta de botões no atrato.
void secretSequence(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief `0x4996`: true se `[0x991030] & [4995]` != 0 (o original devolve no carry).
bool motionRequest(MemoryBus& bus);
/**
 * @brief `0x49B1` (INT0): com a placa de movimento presente (bit 0x40 de `[0x992000] >> 16`
 *        desligado) e o pedido da `0x4996`, `[EBB7]` = 1. A serial do controlador (`0x49B8`…)
 *        é tratada pela `0x4834` no loop principal.
 */
void pollMotion(Tms32031State& cpu, MemoryBus& bus);

/// @brief Registra `0x4E4F`, `0xAFC2`, `0xB001`, `0x4FAE`, `0x49B1`, `0x4996` e `0x49A7`.
void registerCabinetOutputs(SubroutineDispatcher& d);

} // namespace rea::core::io
