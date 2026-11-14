#pragma once
/**
 * @file coin_start.hpp
 * @brief Processos de moeda (`0x7314`–`0x739F`) e do botão START (`0x1D7F`–`0x1DC2`).
 *
 * @details A tabela de portas do gabinete (`[4DFA]` → 0x4DFB) cria um processo a cada borda de
 * subida: moeda 1 → `0x7314`, moeda 2 → `0x7321`, START → `0x1D92` (moedas 3/4 e serviço usam
 * `0x7338`, `0x7344`, `0x7350`).
 *
 * **Moedas** (`0x7314`, `0x7321`, `0x7338`, `0x7344`): som 0x102 (`0x91E0`), auditoria
 * 0x64/0x65/0x65/0x67 (`0x99DA`), aviso ao link (`0x7C12`, só entradas 1 e 2), soma os pulsos do
 * contador mecânico (`0xC1008E`…`0xC100A8`) em `[E573]` — a entrada 2 vai para `[E574]` quando o
 * bit 0x100 de `[C8CC]` está limpo — e segue para `0x7360` com R2 = unidades da moeda
 * (`0xC1000F`…`0xC10035`).
 *
 * **Crédito de serviço** (`0x7350`): auditoria 0x68 e item 0x71 (créditos) + 1, limitado ao
 * item 0x2D (máximo de créditos).
 *
 * **Conversão** (`0x7360`): com R2 ≠ 0, soma R2 às unidades parciais (item 0x70) e ao contador de
 * bônus (item 0xF7). Enquanto as unidades ≥ `0xC1005B` e ≥ `0xC10042` (unidades por crédito),
 * desconta-as, dá 1 crédito (limitado) e conta a auditoria 0x69. Depois, se o bônus
 * (`0xC1004E`) for ≠ 0 e o contador 0xF7 o alcançar, zera 0xF7 e as unidades e dá mais 1 crédito.
 *
 * **START** (`0x1D92`), pelo modo `[C8C5] & 0xF`:
 * - 3, 5, 6, 7, 8 (jogo em andamento ou telas finais): `[E631]` = 1 (pedido de continuação);
 * - 4: nada;
 * - senão (atração): `0x1D7F` testa os créditos (jogo grátis — item 0x19 = 1 — sempre basta).
 *   Sem créditos, toca o som 0x105 uma única vez (`[C8E6]`). Com créditos, desconta o preço
 *   (`0xC10068`, sem ficar negativo), zera o bônus 0xF7, entra no modo 3, zera `[E876]`, refaz
 *   a pilha (`SP = [0x63]`), chama `0x9503` com AR2 = 1 e volta ao loop principal `0x4C1B`.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// Variáveis e itens da CMOS usados pelas moedas e pelo START.
namespace coin_vars {
inline constexpr uint32_t kMeter1       = 0xE573; ///< Pulsos pendentes do contador mecânico 1.
inline constexpr uint32_t kMeter2       = 0xE574; ///< Pulsos pendentes do contador mecânico 2.
inline constexpr uint32_t kMeterFlags   = 0xC8CC; ///< Bit 0x100: entrada 2 usa o contador 1.
inline constexpr uint32_t kGameMode     = 0xC8C5; ///< Bits 0–3: modo do jogo.
inline constexpr uint32_t kContinueReq  = 0xE631; ///< 1: START apertado durante o jogo.
inline constexpr uint32_t kNoCreditBeep = 0xC8E6; ///< > 0: o som de "sem crédito" já tocou.
inline constexpr uint32_t kE876         = 0xE876;  ///< objeto do carro focado (jogador/câmera)
inline constexpr uint32_t kStackTop     = 0x0063; ///< `[0x63]`: topo da pilha do loop principal.
inline constexpr uint32_t kItemCoins    = 0x70;   ///< Unidades de moeda parciais.
inline constexpr uint32_t kItemCredits  = 0x71;  ///< Item da CMOS: créditos.
inline constexpr uint32_t kItemBonus    = 0xF7;   ///< Unidades acumuladas para o crédito bônus.
inline constexpr uint32_t kItemMaxCred  = 0x2D;  ///< Item da CMOS: máximo de créditos.
inline constexpr uint32_t kItemFreePlay = 0x19;  ///< Item da CMOS: jogo grátis.
inline constexpr uint32_t kSndCoin      = 0x102;  ///< Som da moeda.
inline constexpr uint32_t kSndNoCredit  = 0x105;  ///< Som de crédito insuficiente.
} // namespace coin_vars

/**
 * @brief `0x1D7F`: há créditos para começar?
 * @param cpu Saída: carry = 1 se sim (ou jogo grátis); R0 = item 0x71 (ou 0x19), R1 = preço.
 *            Preserva R2 e AR2.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void hasStartCredits(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra `0x1D7F`, os processos `0x1D92`, `0x7314`, `0x7321`, `0x7338`, `0x7344`,
///        `0x7350` e o trecho `0x7360` (também alcançável como processo).
void registerCoinStart(SubroutineDispatcher& d);

} // namespace rea::core::game
