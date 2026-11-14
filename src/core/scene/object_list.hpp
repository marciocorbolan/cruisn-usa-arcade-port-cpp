#pragma once
/**
 * @file object_list.hpp
 * @brief Lista de objetos de desenho: ordenação e reagrupamento (`0x7187`–`0x7251`).
 *
 * @details O jogo mantém os objetos da cena numa lista simplesmente encadeada.
 * A célula da cabeça fica em `[[0x40]]` (`[0x40]` = `0xC982`). Campos usados
 * (deslocamentos em palavras):
 *
 * | Campo  | Uso |
 * |--------|-----|
 * | `+0x00` | Próximo objeto (0 = fim) |
 * | `+0x01` | Coordenada (float TMS) comparada com a do objeto `[C98C]` |
 * | `+0x0F` | Tipo/classe (`0x100`, `0x2xx`, `0x21x`, `0x300`, `0x6xx`…) |
 * | `+0x1B` | Ponteiro para uma estrutura com 5 vizinhos em `+5, +0xB, +0x11, +0x17, +0x1D` |
 * | `+0x1C` | Chave de ordenação (inteiro com sinal; provável profundidade) |
 *
 * A ordem é **decrescente** pela chave, como no algoritmo do pintor: o mais
 * distante vem primeiro.
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::scene {

/// Variáveis e campos da lista.
namespace objlist {
inline constexpr uint32_t kHeadCellPtr = 0x0040; ///< `[0x40]` = 0xC982: endereço da célula da cabeça.
inline constexpr uint32_t kFlipRequest = 0xC93F; ///< != 0: troca de página pedida (a INT0 executa e zera).
inline constexpr uint32_t kGameMode    = 0xC8C5; ///< Bit 0x20: pula o reagrupamento.
inline constexpr uint32_t kDetached    = 0xC98C; ///< Objeto tipo 0x100 retirado por `0x71B3`.
/// @name Campos de um objeto da lista (deslocamentos em palavras)
///@{
inline constexpr uint32_t kNext        = 0x00;  ///< Deslocamento de campo do objeto (ver a tabela do arquivo).
inline constexpr uint32_t kCoord       = 0x01;  ///< Coordenada (float TMS) comparada com a do objeto `[C98C]`.
inline constexpr uint32_t kType        = 0x0F;  ///< Tipo/classe do objeto.
inline constexpr uint32_t kNeighbors   = 0x1B;  ///< Ponteiro para a estrutura com 5 vizinhos.
inline constexpr uint32_t kKey         = 0x1C;  ///< Chave de ordenação (inteiro com sinal; maior = mais distante).
///@}
/// Limite de passos da ordenação no port. Equivale ao VBLANK chegar (ver sortObjects).
inline constexpr uint32_t kSortStepBudget = 1u << 22;
} // namespace objlist

/**
 * @brief `0x7187`: pede a troca de página e ordena a lista enquanto ela não ocorre.
 * @details
 * - Faz `[C93F] = 1` e repete passadas de bubble sort: troca vizinhos quando
 *   `a.chave < b.chave`.
 * - Antes de cada comparação lê `[C93F]`. No hardware, a única saída é a
 *   INT0 zerar essa variável. Toda passada termina com `BR $7189`, e o teste
 *   de R6 em `0x71A0` nunca é alcançado com R6 = 0.
 * - Depois (`0x71A9`), se `[C8C5] & 0x20 == 0`, chama `0x71B3`, `0x71E4`,
 *   `0xAF75`, `0x71BF`, `0x87B8` e `0xAE05`.
 *
 * @note **Aproximação do port:** a INT0 não roda dentro de rotinas. Por isso a
 * ordenação para quando uma passada inteira não troca nada. A partir daí
 * passadas extras não mudariam a lista, então o estado final é o mesmo de um
 * hardware com tempo de sobra. A espera da troca fica com o loop principal
 * (`0x4BD4`). Um orçamento de passos (`kSortStepBudget`) protege contra
 * listas corrompidas (cíclicas), como o VBLANK faria.
 */
void sortObjects(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x71B3`: retira o primeiro objeto do tipo `0x100` e o guarda em `[C98C]`.
 * @details `[C98C]` = 0 se não houver. Saída: AR0 = predecessor e AR1 = objeto.
 */
void detachType100(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x71E4`: reinsere cada objeto `0x2xx` junto ao vizinho de menor chave.
 * @details
 * 1. Retira da lista todos os objetos com `tipo & 0xF00 == 0x200` e os
 *    empilha, o que inverte a ordem.
 * 2. Para cada um, escolhe entre os 5 vizinhos de `[+0x1B]` o de menor chave
 *    (nulos valem 0x7FFF0000; no empate fica o último). Procura esse vizinho
 *    na lista; se não achar, usa a cabeça.
 * 3. A partir dali, insere o objeto antes do 1º elemento com chave ≤ a dele.
 *    Se o 1º vizinho for do tipo `0x300`, só contam elementos com tipo < 0x310
 *    ou `tipo & 0xF00 == 0x600`.
 */
void regroupType200(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x71BF`: devolve `[C98C]` ao fim da lista com os objetos `0x21x` próximos.
 * @details Um objeto entra no grupo se `tipo & 0xFF0 == 0x210`, a chave dele é ≤ a
 *          de `[C98C]` e `FIX(|coord − coord_C98C|)` ≤ 2000. Ele sai da
 *          lista e é anexado ao fim da cadeia de `[C98C]`. Ao final, a cadeia
 *          inteira vai para o fim da lista: esses objetos são desenhados por último.
 * @note A subtração usa double. O TMS usa precisão estendida (mantissa de
 *       32 bits); a diferença só apareceria muito longe do limiar de 2000.
 */
void reattachDetached(Tms32031State& cpu, MemoryBus& bus);

/// Listas de visibilidade (`0x7254`/`0x726F`).
namespace cull {
inline constexpr uint32_t kFarHeadPtr   = 0x0041; ///< `[0x41]` = 0xC984: célula da cabeça da lista "distante".
inline constexpr uint32_t kCamPosPtr    = 0x0045; ///< `[0x45]` = 0x809800: posição da câmera (x, y, z).
inline constexpr uint32_t kCamMatrixPtr = 0x0047; ///< `[0x47]` = 0x809809: matriz da câmera; a linha em +6..+8 é o eixo de profundidade.
inline constexpr uint32_t kNearRangePtr = 0x7252; ///< 75000: entra na lista visível se profundidade − raio ≤ isto.
inline constexpr uint32_t kFarRangePtr  = 0x7253; ///< 80000: sai da lista visível se profundidade − raio > isto.
inline constexpr int32_t  kBehind       = -5000;  ///< Profundidade ≤ −5000: atrás da câmera.
inline constexpr uint32_t kPos          = 0x01;   ///< x, y, z (float) em +1..+3.
inline constexpr uint32_t kListFlags    = 0x0E;   ///< XOR 0x3000 a cada troca de lista.
inline constexpr uint32_t kRadius       = 0x1D;   ///< Raio (inteiro).
} // namespace cull

/**
 * @brief `0x726F`: promove objetos da lista distante (`[[0x41]]`) para a visível (`[[0x40]]`).
 * @details Para cada objeto da lista distante:
 * - profundidade = (pos − câmera) · `[[0x47]+6..+8]` (float);
 * - grava `FIX(profundidade)` em `+0x1C` (sempre: o `FIX`/`STI` está nos slots do `BLED`);
 * - se profundidade > −5000,0 e `FIX − [0x7252] − raio ≤ 0`, o objeto sai da
 *   lista distante, vai para o **início** da visível e tem `+0xE ^= 0x3000`.
 *
 * Saída: R3 = −5000,0, R4 = `[0x7252]`, R5/R6/R7 = diferenças do último objeto,
 * R0 = 0, AR1 = 0, AR6 = último nó visitado.
 * @note As diferenças e o produto escalar usam double. O TMS usa precisão
 *       estendida de 32 bits de mantissa. A diferença só altera o resultado
 *       quando a profundidade cai exatamente num limiar.
 */
void promoteNear(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x7254`: rebaixa objetos da lista visível para a distante.
 * @details Usa a profundidade inteira já gravada em `+0x1C`. O objeto vai para
 *          o início da lista distante (`+0xE ^= 0x3000`) se profundidade ≤ −5000
 *          ou se `profundidade − [0x7253] − raio > 0`.
 */
void demoteFar(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x72D2`: bubble sort completo da lista visível (decrescente por `+0x1C`).
 * @details Ao contrário da `0x7187`, não depende da INT0: repete passadas até
 *          nenhuma trocar. Orçamento de passos como em sortObjects.
 */
void sortVisibleFull(Tms32031State& cpu, MemoryBus& bus);
/**
 * @brief `0x7298`: refaz as listas de visibilidade.
 * @details Passa a lista visível inteira para o início da distante (`+0xE ^= 0x3000`
 *          em cada objeto), depois chama `0x726F` e `0x72D2`. Preserva AR3–AR6.
 */
void rebuildVisibility(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// Pool de objetos de cena (`0x6FEB`).
namespace pool {
inline constexpr uint32_t kFreeCellPtr = 0x6FE9; ///< `[0x6FE9]` = 0xC983: célula da lista livre.
inline constexpr uint32_t kFirstPtr    = 0x6FEA; ///< `[0x6FEA]` = 0x10555: primeiro objeto.
inline constexpr uint32_t kCount       = 0x44C;  ///< 1100 objetos.
inline constexpr uint32_t kStride      = 0x22;   ///< Palavras por objeto.
inline constexpr uint32_t kFreeCount   = 0xC989; ///< Objetos livres.
} // namespace pool

/**
 * @brief `0x6FEB`: esvazia as listas de cena e refaz a lista livre do pool.
 * @details `[C989]` = 1100; zera `C982`, `C984`–`C988`, `C98A`, `C98B` e
 *          `E403`–`E406`; encadeia os 1100 objetos (0x22 palavras cada, a partir
 *          de `[0x6FEA]`) a partir da célula `[0x6FE9]`; o último aponta para 0.
 *          Preserva R0, AR0 e AR1; RC = −1.
 */
void resetObjectPool(Tms32031State& cpu, MemoryBus& bus);

/// @brief Registra as rotinas deste módulo no despachante.
void registerRoutines(SubroutineDispatcher& d);

} // namespace rea::core::scene
