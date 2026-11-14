#pragma once
/**
 * @file scene_object.hpp
 * @brief Objetos de cena: alocar, iniciar, profundidade e encadeamento (`0x7025`–`0x70C9`, `0x715F`, `0x9ECD`).
 *
 * @details Cada objeto tem 0x22 palavras (pool da `0x6FEB`). Campos usados aqui:
 *
 * | Campo | Uso |
 * |---|---|
 * | `+0` | próximo |
 * | `+1..+3` | posição x, y, z (float) |
 * | `+4..+0xC` | matriz 3×3 (identidade ao alocar) |
 * | `+0xD` | ponteiro do modelo |
 * | `+0xE` | flags: lista (`0x1000` visível, `0x2000` distante, `0x3000` terceira), `0x40`, `0x400` |
 * | `+0xF` | tipo |
 * | `+0x10` | textura/paleta (da `0x9ECD`) |
 * | `+0x1C` | profundidade (inteiro) |
 * | `+0x1D` | raio (palavra 0 do modelo) |
 *
 * Listas:
 * - visível `[[0x40]]`, ordenada por profundidade decrescente;
 * - distante `[[0x41]]`;
 * - terceira `[[0x42]]`, ordenada (decrescente);
 * - `[C986]` e `[C987]`, pilhas simples.
 *
 * Erros vão para `0xAB1A`: 0x110 (pool vazio) e 0x104 (lista vazia na `0x715F`).
 */

#include "core/subroutines.hpp"

namespace rea::core::scene {

namespace sobj {
inline constexpr uint32_t kFreeHead   = 0xC983;  ///< Cabeça da lista de objetos livres.
inline constexpr uint32_t kFreeCount  = 0xC989;  ///< Objetos livres.
inline constexpr uint32_t kThirdHeadPtr = 0x0042;  ///< `[0x42]`: célula da cabeça da terceira lista.
inline constexpr uint32_t kStackA     = 0xC986;  ///< Pilha simples A.
inline constexpr uint32_t kStackB     = 0xC987;  ///< Pilha simples B.
/// @name Campos do objeto de cena (deslocamentos em palavras)
///@{
inline constexpr uint32_t kFlags      = 0x0E;  ///< Deslocamento de campo do objeto (ver a tabela do arquivo).
inline constexpr uint32_t kListMask   = 0x3000;  ///< Bits de lista em `+0xE` (0x1000 visível, 0x2000 distante, 0x3000 terceira).
inline constexpr uint32_t kModel      = 0x0D;  ///< Ponteiro do modelo.
inline constexpr uint32_t kTexture    = 0x10;  ///< Textura/paleta (da `0x9ECD`).
inline constexpr uint32_t kDepth      = 0x1C;  ///< Profundidade (inteiro).
inline constexpr uint32_t kRadius     = 0x1D;  ///< Raio (palavra 0 do modelo).
///@}
inline constexpr uint32_t kTexTablePtr = 0x9E7E; ///< `[0x9E7E]` = 0xEA4C.
inline constexpr uint32_t kErrPoolEmpty = 0x110;  ///< Erro: pool de objetos vazio.
inline constexpr uint32_t kErrNotInList = 0x104;  ///< Erro: objeto não está na lista (`0x715F`).
} // namespace sobj

/// @brief `0x7025`: AR0 = novo objeto zerado (matriz identidade). Pool vazio: erro 0x110, carry = 1, AR0 = 0. Preserva R0.
void allocObject(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/**
 * @brief `0x700A`: aloca e inicia com o modelo AR2.
 * @details `+0xD` = AR2, `+0x1D` = `[AR2]`. A flag do modelo é `[AR2 − 1]`:
 *          - bit 0x1000 → `+0xE |= 0x40`;
 *          - bit 0x2000 → `+0xE |= 0x400` e `+0x10` = `0x9ECD(flag & 0xFFF)`.
 *
 *          Preserva R0 e R1. Carry = 1 se o pool estiver vazio.
 * @note Se a `0x9ECD` falhar, o original trava (`BLO $701F` para si mesmo)
 *       até o watchdog. O port devolve true nesse caso e segue.
 * @return true se o original travaria.
 */
bool allocFromModel(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x7058`: `+0x1C` = R0 = FIX((pos − câmera) · eixo de profundidade). Preserva R1 e R2.
void computeDepth(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0x7309`: aloca (`0x7025`) com o modelo AR2 em (FLOAT R2, FLOAT R3, FLOAT RC); carry = 0. Pool vazio: carry = 1.
/// @brief `0x7070`: `+0xE |= 0x3000` e insere AR2 na terceira lista, antes do 1º com profundidade < a dele.
void linkThird(Tms32031State& cpu, MemoryBus& bus);
/**
 * @brief `0x7093`: calcula a profundidade e encadeia AR2.
 * @details - profundidade < −5000 ou ≥ `[0x7253]`: vai para o início da lista
 *            distante (`+0xE |= 0x2000`);
 *          - senão vai para a lista visível (`|= 0x1000`), antes do 1º com
 *            profundidade ≤ a dele.
 *
 *          Preserva R0, R1, AR0 e AR1.
 */
void linkObject(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0x70BB` (visível) / `0x70B8` (terceira): procura `+0xF == AR2`. Achou: carry = 1 e AR0 = objeto.
void findByType(Tms32031State& cpu, MemoryBus& bus, bool third);
/// @brief `0x715F`: retira AR2 da lista indicada por `+0xE & 0x3000` e zera esses bits. Preserva R0, R1, AR1 e AR2.
void unlinkObject(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x9ECD`: R0 = (`[[0x9E7E] + AR2]` >> 16) << 8. Entrada 0: carry = 1. Preserva AR2; DP = 0.
void textureSlot(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x440C`: cria os objetos de uma lista da ROM (AR2).
 * @details Formato: `[AR2+1]` = quantidade; depois, por objeto, 6 palavras:
 *          modelo, x, y, z (inteiros → float), ângulo Y (float) e tipo
 *          (só os 12 bits baixos vão para `+0xF`).
 *          Para cada objeto chama `0x700A`, monta a matriz com `0x939D` (em
 *          `+4`, ângulo em `+0x15`) e encadeia com `0x7093`. Os objetos criados
 *          formam uma cadeia por `+0x1F`, que começa em `[E47D]`
 *          (`[0xB0D1]` − 0x1F + 0x1F) e termina em 0.
 *          Se o pool esgotar, para ali. R0 = `[E47D]`; preserva R4, R5, AR0, AR2 e AR4–AR7.
 */
void spawnObjectList(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra as rotinas. `hangs` conta as travas do `0x701F` (precisa viver tanto quanto `d`).
void registerSceneObject(SubroutineDispatcher& d, uint64_t& hangs);

} // namespace rea::core::scene
