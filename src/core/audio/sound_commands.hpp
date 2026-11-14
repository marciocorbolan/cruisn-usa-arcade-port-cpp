#pragma once
/**
 * @file sound_commands.hpp
 * @brief Driver de comandos da placa de som (`0x90E6`–`0x92C7`).
 *
 * @details A ROM fala com a placa de som DCS por palavras de 16 bits (`0x9211`):
 * byte alto e depois byte baixo. Cada byte vai como `0xFF00 | b`, estrobo `0xFD00 | b`
 * e `0xFF00 | b` para a porta `0x995000`. O `0xFF00 | b` também vai para `[[C6BB]]` = 0x9A0000.
 * O GIE é desligado durante o envio e religado (`0x4C33`) só com `[E917]` = 0; assim, as
 * sequências de 2 palavras saem sem interrupção. Com `[809A79]` ≠ 0 (placa reiniciando),
 * nada é enviado. O original espera 31 ciclos entre as escritas; o port só as reproduz.
 *
 * **Canais**: 4 registros de 9 palavras em `[9175]` = 0xE8F0:
 *
 * | Palavra | Uso |
 * |---|---|
 * | +0 | palavra de prioridade do som atual (bits 0–7 prioridade, 0x1000 travado, 0x2000 não interrompe igual) |
 * | +2 | quadros até o próximo passo |
 * | +3 / +4 | início / passo atual da sequência |
 * | +5 | volume do canal (0xFF = padrão) |
 * | +6 | número do som |
 * | +7 | 1 = tocando |
 * | +8 | variante (comando + 1); canal − 1 na `0x9186` |
 *
 * **Sons** (`[90A6]` = 0x8974 + número): sequência de pares (prioridade, `comando | quadros << 16`);
 * a prioridade 0 ou os quadros 0 encerram. O canal vem dos bits 8–11 da 1ª prioridade.
 *
 * | Rotina | Efeito |
 * |---|---|
 * | `0x9211` | envia AR2 (16 bits). Preserva R0, R1 e AR3; AR2 &= 0xFF; DP = 0 |
 * | `0x910F` | volume geral R1: `0x55AA`, `(v << 8) \| (~v & 0xFF)` |
 * | `0x9114` | volume R1 do canal R0 (`+5`): `0x55AB + canal`, `(v << 8) \| (~v & 0xFF)` |
 * | `0x9131` | reinicia a placa (bit 2 de `[C8C7]`/`0x994000`; latch 0 com 0x3C, espera, 0x1C) e limpa os canais |
 * | `0x9166` | limpa os canais e envia 0, 0x4E8, 0x55CC, 0 |
 * | `0x9176` | zera os 4 canais (36 palavras). Preserva AR0 e R0 |
 * | `0x917F` | R0 = prioridade atual do canal do som AR0 |
 * | `0x9186` / `0x9188` | toca AR2 no canal 1 ou 2 (o de menor prioridade) com o volume R0 & 0xFF / 0xFF; carry = 1 se tocou. No modo 2 não toca (carry = 1) |
 * | `0x91C1` | se AR2 está no canal 1 → toca 0x264; no canal 2 → toca 0x267 (carry = 1); senão carry = 0 |
 * | `0x91D0` | envia 0x55CC e `R1 \| R0 << 8` |
 * | `0x91DA` | envia os 16 bits baixos de `[[90A6] + AR2 + 1]` |
 * | `0x91E0` | toca o som AR2 se a prioridade vencer a do canal (volume volta a 0xFF) |
 * | `0x9108` | toca a música `[[90A7] + [E916]]` (`[E916]` > 8 → 0) |
 * | `0x90E6` / `0x90EB` / `0x90F0` | música AR2 (recebida pelo link / local / a atual); cria o processo `[C6BA]` (ID 0x5C6) se não existir; `[E914]` = 0x28; com `[C8E3]` avisa o outro gabinete (`0x7C07`) |
 * | `0x92AD` | pede o reinício da placa (`[809A79]` = `[809A7A]` = 1) |
 * | `0x92B3` | INT0: reinício em etapas (2: latch; 4: palavra 0; 6: volume do item 8 da CMOS) ou avança os canais |
 * | `0x92C7` | toca o passo atual do canal AR0. Preserva AR1 |
 */

#include "core/subroutines.hpp"

namespace rea::core::audio {

/// Endereços do driver de som.
namespace snd_vars {
inline constexpr uint32_t kOutputPort  = 0x995000;  ///< Latch serial das saídas (comandos em 2 fases).
inline constexpr uint32_t kControlPort = 0x994000;  ///< Controle: bit7 LED, bit3 watchdog, bit1 reset do som DCS.
inline constexpr uint32_t kControl     = 0xC8C7;   ///< Cópia de `0x994000`; bit 1 = placa de som fora de reset.
inline constexpr uint32_t kDataPortPtr = 0xC6BB;   ///< `[C6BB]` = 0x9A0000.
inline constexpr uint32_t kDataPort2Ptr = 0xC6BC;  ///< `[C6BC]` = 0x9A0000.
inline constexpr uint32_t kMusicProc   = 0xC6BA;   ///< `[C6BA]` = 0x90B9.
inline constexpr uint32_t kChannelsPtr = 0x9175;   ///< `[9175]` = 0xE8F0.
inline constexpr uint32_t kSoundTable  = 0x90A6;   ///< `[90A6]` = 0x8974.
inline constexpr uint32_t kMusicTable  = 0x90A7;   ///< `[90A7]` = 0x90A8.
inline constexpr uint32_t kChannel1    = 0xE8F9;   ///< Canal 1, palavra 0.
inline constexpr uint32_t kChannel2    = 0xE902;   ///< Canal 2, palavra 0.
inline constexpr uint32_t kChannel1Id  = 0xE8FF;  ///< Som tocando no canal 1.
inline constexpr uint32_t kChannel2Id  = 0xE908;  ///< Som tocando no canal 2.
inline constexpr uint32_t kMusic       = 0xE916;  ///< Música atual.
inline constexpr uint32_t kMusicTimer  = 0xE914;  ///< Quadros até a música mudar de passo (a `0x90F3` começa com 0x28).
inline constexpr uint32_t kMusicFlag   = 0xE915;  ///< Zerada ao começar a música.
inline constexpr uint32_t kHoldGie     = 0xE917;   ///< != 0: a `0x9211` não religa o GIE.
inline constexpr uint32_t kResetting   = 0x809A79;  ///< != 0: reinício da placa de som em andamento.
inline constexpr uint32_t kResetStep   = 0x809A7A;  ///< Etapa do reinício da placa (`0x9247`, uma por quadro).
inline constexpr uint32_t kChannelWords = 9;  ///< Palavras por canal.
inline constexpr uint32_t kChannels    = 4;  ///< Canais de som.
} // namespace snd_vars

/// @brief `0x9211`: envia a palavra AR2 à placa de som.
void sendWord(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x91E0`: toca o som AR2.
void playSound(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x92C7`: toca o passo atual do canal AR0.
void channelStep(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x92B3`: atualização por quadro (INT0).
void soundFrame(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra as rotinas deste arquivo.
void registerSoundCommands(SubroutineDispatcher& d);

} // namespace rea::core::audio
