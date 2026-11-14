#pragma once
/**
 * @file title_screen.hpp
 * @brief Tela de título do atrato (caso −2 da `0x9503`): processo `0xA93B`.
 *
 * @details O caso −2 (`0x9587`) põe o modo 2 em `[C8C5]`, carrega os recursos da
 * pista `[C8CF]` (`0x59E3`) e cria os processos `[C705]` e `[C706]` (ID 0x500).
 * Um deles é a `0xA93B`:
 * 1. com `[C8E8]` = −2, `[C8CF]` = 0 e o item 0x1E da CMOS ligado, toca a
 *    música do atrato (`0x910F`/`0x91E0`, som pendente) e fica 890 quadros
 *    (AR6 = 0x37A); senão AR6 = 0 e a tela dura só 4 quadros;
 * 2. cria um objeto que gira em Y (`0x1DCF`, processo `0x1DC3`);
 * 3. põe a câmera em y = −2780 e cria o logotipo `[B0BF]` em (0, −910, 5500),
 *    com ângulos (0, 4,189 + π/2, 0,0873) → matriz (`0x9343`);
 * 4. a cada quadro: ângulo Y += 0,017456 × quadros (`[C92F]`), menos o yaw da
 *    câmera; dorme 1 quadro;
 * 5. `0xA986`: AR6 −= quadros. Com `[E5CA]` = 0 por mais de 3 quadros e AR6 ≤ 0,
 *    zera `[C8E9]` e salta para `0x1E1C` (próxima tela do ciclo).
 *
 * | Rotina | Efeito |
 * |---|---|
 * | `0x59E3` | `CALLU [[[C8CF] + [B0AD] + 1] + [8C13]]`: carregador de recursos da pista |
 * | `0x8C14` | pista 0: texturas LZW `[C650]` e `[C651]` (`0xA3C2`) |
 * | `0x1DCF` | objeto (modelo 0xC2F984, flags \|= 0xA3) em (0, 50, 368), terceira lista, processo `[B226]` = 0x1DC3 |
 * | `0x1DC3` | gira o objeto AR4 em Y: R6 −= 0,034912 × quadros (`0x938A`) |
 * | `0xA3D7` | processo: espera `[E5CA]` = 0, carrega a textura LZW AR4 (`0xA3C2`) e morre |
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// Variáveis da tela de título.
namespace title_vars {
inline constexpr uint32_t kAttractCase  = 0xC8E8;  ///< Caso da atração/partida (−1 a −7).
inline constexpr uint32_t kTrack        = 0xC8CF;  ///< Pista atual (cena da atração e da corrida).
inline constexpr uint32_t kFrames       = 0xC92F;   ///< Quadros desta iteração.
inline constexpr uint32_t kBackdrop     = 0xC938;  ///< Copiada para a paleta[0] a cada quadro.
inline constexpr uint32_t kInputIdle    = 0xE5CA;  ///< != 0: há descompressão em andamento.
inline constexpr uint32_t kExitFlag     = 0xC8E9;  ///< Zerado ao sair da tela de título.
inline constexpr uint32_t kCamY         = 0x809801;  ///< y da câmera.
inline constexpr uint32_t kCamYaw       = 0x809807;  ///< Rumo da câmera (página 0x80 do DP).
inline constexpr uint32_t kLogoModelPtr = 0xB0BF;  ///< Ponteiro do modelo do logotipo.
inline constexpr uint32_t kSpinProcPtr  = 0xB226;   ///< `[B226]` = 0x1DC3.
inline constexpr uint32_t kTrackTable   = 0xB0AD;   ///< `[B0AD]` = 0xC10361.
inline constexpr uint32_t kLoaderTable  = 0x8C13;   ///< `[8C13]` = 0xC11065.
inline constexpr uint32_t kMusicFrames  = 0x37A;  ///< Quadros da música do título.
inline constexpr uint32_t kCounterSlot  = 0xD;      ///< `*+AR7(D)`: quadros com `[E5CA]` = 0.
inline constexpr uint32_t kSignModels   = 0xB227;   ///< `[B227]`..`[B22C]`: modelo e paleta das 3 placas da `0x1DE9`.
inline constexpr uint32_t kFrameModel   = 0xC2F8F3; ///< Moldura do caso −3 (`0xA9C8`).
inline constexpr uint32_t kTrackLogoTable = 0xA9C7; ///< `[A9C7]` = 0xC10371: logotipo por par de pistas.
inline constexpr uint32_t kTrackLogoFrames = 0x226; ///< Duração mínima do caso −3 (AR6).
} // namespace title_vars

/**
 * @brief Caso −3 (`0x95A4`): processo `[C70B]` = `0xA9C8`:
 * 1. moldura 0xC2F8F3 (flags \|= 0xA3) em (−150, −110, 436);
 * 2. `0x1DE9`: sem `[C8CC] & 4`, três placas (`0x7309`) em x = 230, y = −250 / −130 / −190,
 *    z = 926 / 926 / 924, flags \|= 0x4A3, paletas `[B228]`/`[B22A]`/`[B22C]` (`0x9EDB` → `+0x10`);
 * 3. logotipo `[[A9C7] + ((([C8CF] >> 1) − 1) & 3)]` em z = 1368, ângulos (−0,196, 3,534, 0);
 * 4. espera 16 quadros; depois, a cada quadro, ângulo Y += 0,017998 × quadros e `0xA986`
 *    com AR6 = 0x226 (o fim normal vem do cronômetro `0x1E26`, 900 quadros).
 */

/// @brief Registra `0x59E3`, `0x8C14`, `0x1DCF`, `0x1DC3`/`0x1DCE`, `0x1DE9`, `0xA3D7`/`0xA3D9`, `0xA93B`/`0xA984` e `0xA9C8`/`0xA9F5`/`0xAA05`.
void registerTitleScreen(SubroutineDispatcher& d);

} // namespace rea::core::game
