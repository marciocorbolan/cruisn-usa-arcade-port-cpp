#pragma once
/**
 * @file attract_demo.hpp
 * @brief Demonstração do atrato (caso −3): processo `0x559E`, carga da pista `0x596F`,
 *        câmera `0x5943` e a moldura `0x55DA`.
 *
 * @details **`0x559E`** (processo `[C70A]`, ID 0x202):
 * 1. `[AR7+0x36]` = `[C8CF]` >> 1 (par de pistas); carrega a pista (`0x596F`);
 *    `[C941]` = 4; cria a moldura `[B0AE]` = `0x55DA`; `[C8C5]` |= 0x200; `[C931]` =
 *    `[C946]` = `[E5EB]` = 1; dorme 1 quadro;
 * 2. `0x55B4`: segue o carro: `[E876]` = `[[[E876]+0xA]+9]` … (o PCB do carro 4 → o objeto);
 *    `[E604]` = 0x5A; AR6 = roteiro `[[B0AC] + [AR7+0x36]]` (`0x5620`);
 * 3. laço do roteiro: cada passo é `início, (argumentos lidos pelo início), duração, por quadro`.
 *    O início roda uma vez, AR5 = duração, e a função por quadro roda até AR5 ≤ 0 (ela mesma
 *    desconta `[C92F]`). O terminador 0 volta 3 palavras (repete o último passo). Antes de cada
 *    início, o objeto pendente `[AR7+0x38]` é encadeado (`0x7093`). A cada quadro, `0x5943`
 *    copia a câmera do processo (`AR7+0x17..0x1D`) para a câmera global e `[C941]` é descontado.
 *
 * **`0x596F`**: música 0x3F (pista 0 com o item 0x1E); modo 0x42; paleta `[B0B0]` (`0x9DFE`);
 * `[E480]`/`[E481]` = `[[B0AD] + [C8CF]]` / `+1`; carregadores por pista `[[8C12] + [E481]]` e
 * `[[3965] + [E481]]`; `0x6FEB`, `0x6754`, `0x8FF0`, `0x9019`, `0xAF55`, `0x3F8D`; o processo
 * 0x5AA recebe `+4` = 3; `[C938]` = `[AR7+0xC]`; `[C940]` = 1; processo `[B0B1]` (ID 0x5C0);
 * `0x59AF` cria os processos fixos (`[B0B2]`..`[B0B5]`) e os carros `[B0B6]`..`[B0BD]` (R4 = 0..7)
 * e `[B0BE]` (R4 = 9), ID 0x200; `[E876]` = PCB do carro 4. No fim, `[C8CF]` += 2 (volta a 0 depois de 0xE).
 *
 * **Câmera do processo**: posição `AR7+0x17..0x19`, ângulos `AR7+0x1A..0x1C`, matriz
 * `AR7+0x1D` (9 palavras). Com `[AR7+0xD]` ≠ 0, a matriz vai direto para `[0x47]`; senão,
 * `[0x47]` = 0,2·`[0x47]` + 0,8·nova. `0x5968`: `[E3FD]` = sen(ângulo X)·512 + `[AR7+0x2B]`.
 *
 * **Moldura `0x55DA`**: objeto 0xC2F8F3 (tipo 0x550) que vem de z = 10000 até 368 e desliza
 * para (−120, −85) com velocidade crescente (R7 += 0,03), girando em X de 18,85 a 0, por
 * 125 quadros; depois morre.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// Variáveis da demonstração.
namespace demo_vars {
inline constexpr uint32_t kScriptTable = 0xB0AC; ///< `[B0AC]` = 0xC1030B: roteiro por par de pistas.
inline constexpr uint32_t kTrackTable  = 0xB0AD; ///< `[B0AD]` = 0xC10361.
inline constexpr uint32_t kFrameProc   = 0xB0AE; ///< `[B0AE]` = 0x55DA.
inline constexpr uint32_t kCamPosPtr   = 0xB0AB; ///< `[B0AB]` = 0x809800.
inline constexpr uint32_t kCamAngles   = 0x809806;  ///< Ângulos da câmera (página 0x80 do DP).
inline constexpr uint32_t kCamMatPtr   = 0x0047;  ///< `[0x47]`: matriz da câmera.
inline constexpr uint32_t kFocusCar    = 0xE876;  ///< objeto do carro focado (jogador/câmera)
inline constexpr uint32_t kFocusData   = 0xE878;  ///< dados D do carro focado
inline constexpr uint32_t kTrack       = 0xC8CF;  ///< Pista atual (cena da atração e da corrida).
inline constexpr uint32_t kFrames      = 0xC92F;  ///< Quadros (VBLANKs) gastos na última iteração do loop principal.
inline constexpr uint32_t kCountdown   = 0xC941;  ///< Contagem regressiva decrementada a cada passo da demonstração; com 0, a INT0 alterna as páginas de vídeo (`0x8E4E`/`0x8E59`).
inline constexpr uint32_t kSkyOffset   = 0xE3FD;  ///< Deslocamento horizontal do céu.
inline constexpr uint32_t kTwoPi       = 0x9304;  ///< `[9304]` = 2π.
// Campos do PCB (AR7 + n)
inline constexpr uint32_t kSnap        = 0x0D; ///< != 0: a matriz da câmera não é suavizada.
inline constexpr uint32_t kCamPos      = 0x17;  ///< Posição da câmera (3 floats).
inline constexpr uint32_t kCamAng      = 0x1A;  ///< Ângulos da câmera (3 floats).
inline constexpr uint32_t kCamMat      = 0x1D;  ///< Matriz da câmera (9 floats).
inline constexpr uint32_t kSkyBase     = 0x2B;  ///< Deslocamento do céu no início do passo.
inline constexpr uint32_t kTrackPair   = 0x36;  ///< Índice do par de pistas no roteiro.
inline constexpr uint32_t kPendingObj  = 0x38;  ///< Objeto pendente do passo atual.
} // namespace demo_vars

/// @brief `0x58E8`: R0 em (−π, π] por uma volta (±6,279). R1 = a correção.
void wrapPi(Tms32031State& cpu);
/// @brief `0x5943`: aplica a câmera do processo AR7 (ver o arquivo); `[AR7+0xD]` = 0.
void applyDemoCamera(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x596F`: carrega a pista `[C8CF]` e cria os carros (ver o arquivo).
void loadDemoTrack(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra `0x559E`/`0x55B4`/`0x55C5`, `0x55DA`/`0x55F0`, `0x5620`, `0x58DD`, `0x58E8`,
///        `0x5943`, `0x5968`, `0x596F` e `0x59AF`.
void registerAttractDemo(SubroutineDispatcher& d);

} // namespace rea::core::game
