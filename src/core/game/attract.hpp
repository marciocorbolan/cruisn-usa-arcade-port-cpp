#pragma once
/**
 * @file attract.hpp
 * @brief Controlador do modo de atração (processo `0x3859`, `[C703]`).
 *
 * @details Criado pela `0x9503` no caso −1 (RESET e fim de cada ciclo). Ele:
 * 1. carrega 5 paletas (`0x9F2F` com `[B182]`–`[B186]`) e as paletas por id de `[B187]` (`0x9DF6`);
 * 2. zera `[E5FE]`/`[E5FF]` e começa na cena AR5 = `[C8CF]`;
 * 3. **cena** (`0x3869`): cria o processo `[B188]` (= 0x3889, ID 0x560) com
 *    AR6 = AR5 (índice da cena) e espera até 211 quadros (`AR6 = 0xD2`, `DBU`);
 * 4. **a cada quadro** (`0x3872`): se `[E5FF]` mudou (botão/moeda?), grava em
 *    `[E5FE]` e avança já (com `[E5FF]` = −1, recomeça da cena 0);
 *    senão, ao fim da espera chama `0x6FEB` e avança.
 *
 * | Avanço | Efeito |
 * |---|---|
 * | por tempo | AR5 + 1; ≥ 15 ou par → `0x1E1C` (próxima tela do ciclo); ímpar → nova cena |
 * | por `[E5FF]` | AR5 + 1; ≥ 15 → `0x1E1C`; senão mata os processos `0x56x` (`0xA8CF`, máscara 0xFF0), `0x6FEB`, nova cena |
 *
 * Por tempo, a cena só continua se o novo AR5 for ímpar. Partindo de um AR5
 * par, cada passagem mostra 2 cenas antes de seguir para `0x1E1C`; partindo
 * de um ímpar, mostra 1.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// Variáveis do modo de atração.
namespace attract_vars {
inline constexpr uint32_t kLoadPtrs     = 0xB182; ///< 5 ponteiros para `0x9F2F`.
inline constexpr uint32_t kLoad9DF6Ptr  = 0xB187;  ///< Ponteiro para a carga de `0x9DF6`.
inline constexpr uint32_t kScenePcPtr   = 0xB188; ///< `[B188]` = 0x3889.
inline constexpr uint32_t kSceneId      = 0x560;  ///< ID do processo da cena.
inline constexpr uint32_t kSceneFrames  = 0xD2;   ///< `DBU` → 211 quadros.
inline constexpr uint32_t kSceneCount   = 15;  ///< Cenas da atração (pistas).
inline constexpr uint32_t kStartScene   = 0xC8CF;  ///< Cena inicial (pista atual).
inline constexpr uint32_t kInputLatch   = 0xE5FF;  ///< Última amostra das entradas.
inline constexpr uint32_t kInputSeen    = 0xE5FE;  ///< Amostra já vista (detecta mudança).
} // namespace attract_vars

/// @brief Registra `0x3859`, a cena `0x3869` e a espera `0x3872`.
void registerAttract(SubroutineDispatcher& d);

} // namespace rea::core::game
