#pragma once
/**
 * @file palette_slots.hpp
 * @brief Gerenciador de slots de paleta (`0x9DF6`–`0x9FB5`).
 *
 * @details A paleta (`0x9E0000`, 32768 cores) é dividida em 128 slots de 256
 * cores. O campo `+0x10` de um objeto de cena guarda `slot << 8`, a base do
 * índice de cor.
 *
 * | Tabela | Endereço | Conteúdo |
 * |---|---|---|
 * | ocupação | `[9E7D]` = 0xC98D, 128 palavras | 0 = livre; `id | 0x8000` (por id) ou −1 (por ponteiro) |
 * | por ponteiro | `[9E7F]` = 0xCA0D, 128 | ponteiro da paleta na ROM (para `0x9EDB`) |
 * | por id | `[9E7E]` = 0xEA4C, 254 | `(slot << 16) | contagem de referências` |
 * | origem por id | `[9E7B]` = 0xA005 | ponteiro da paleta de cada id |
 * | outra | `[9E7C]` = 0xCA8D, 0x180 | zerada na iniciação |
 *
 * A cópia é feita depois, pela INT0 (`0x9E97`), a partir de uma fila de
 * pedidos: nós de 4 palavras {próximo, origem, destino, cabeçalho}, 128 nós em
 * 0xCC12, lista livre `[CC10]` e fila `[CC0F]` (LIFO).
 *
 * A INT0 copia até 13 pedidos por quadro. O 13º é copiado mas não sai da fila
 * (o teste `R7 > 12` vem antes da `0x9F8F`), então é copiado de novo no quadro
 * seguinte.
 *
 * Cabeçalho (1ª palavra da paleta na ROM):
 * - ≥ 0: N palavras copiadas como estão;
 * - < 0: N = (cabeçalho & 0x7FFFFFFF) / 2 palavras empacotadas, cada uma
 *   gerando 2 cores (16 bits baixos e depois altos).
 */

#include <cmath>
#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::video {

namespace palslot {
inline constexpr uint32_t kSourceTablePtr = 0x9E7B;  ///< `[0x9E7B]` = 0xA005: ponteiro da paleta de cada id.
inline constexpr uint32_t kOtherTablePtr  = 0x9E7C;  ///< `[0x9E7C]` = 0xCA8D: tabela de 0x180 palavras zerada na iniciação.
inline constexpr uint32_t kOwnerTablePtr  = 0x9E7D;  ///< `[0x9E7D]` = 0xC98D: ocupação dos 128 slots.
inline constexpr uint32_t kByIdTablePtr   = 0x9E7E;  ///< `[0x9E7E]` = 0xEA4C.
inline constexpr uint32_t kByPtrTablePtr  = 0x9E7F;  ///< `[0x9E7F]` = 0xCA0D: ponteiro na ROM da paleta de cada slot.
inline constexpr uint32_t kQueueHead      = 0xCC0F;  ///< Fila de pedidos de cópia (LIFO).
inline constexpr uint32_t kFreeHead       = 0xCC10;  ///< Lista livre dos nós de pedido.
inline constexpr uint32_t kPending        = 0xCC11;  ///< 1 = há pedidos de cópia; a INT0 zera quando a fila esvazia.
inline constexpr uint32_t kQueueCellPtr   = 0x9F6F;  ///< Palavra com o endereço da célula da fila.
inline constexpr uint32_t kFreeCellPtr    = 0x9F70;  ///< Palavra com o endereço da célula da lista livre.
inline constexpr uint32_t kNodesPtr       = 0x9F71;  ///< Palavra com o endereço dos 128 nós de pedido (0xCC12).
inline constexpr uint32_t kSlots          = 0x80;  ///< Slots de 256 cores.
inline constexpr uint32_t kMaxCopiesPerFrame = 12;  ///< A INT0 para depois deste número de cópias (o 13º pedido é copiado e fica na fila).
inline constexpr uint32_t kErrNoSlotById  = 0x77;  ///< Erro: sem slot livre ao alocar por id.
inline constexpr uint32_t kErrNoSlotByPtr = 0x78;  ///< Erro: sem slot livre ao alocar por ponteiro.
} // namespace palslot

/// @brief `0x9EFA`: garante a paleta do id AR2 (conta referências). R0 = slot << 8.
void acquireById(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x9F2F`: carrega a paleta apontada por AR2 num slot novo. R0 = slot << 8.
void loadByPointer(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x9E97` (INT0): executa até 13 pedidos da fila (o 13º fica); zera `[CC11]` quando a fila está vazia.
void servicePaletteQueue(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x95F5`: paletas básicas do jogo, chamada pela `0x9503`.
 * @details Reinicia as tabelas e a fila (`0x9E80`), porque a `0x9503` acabou de
 *          zerar a BSS (que contém a fila em `CC0F`). Depois carrega:
 *          - `[C710]`–`[C715]` por ponteiro (`0x9F2F`), zerando `paleta[0xFF]`;
 *          - `[C71E]`–`[C730]` (`0x96FF`);
 *          - a lista de ids `[C716]` (`0x9DF6`) e o id 5 (`0x9EFA`);
 *          - `[C717]`–`[C71B]` por ponteiro e a lista de ids `[C71C]`.
 */
void loadBasePalettes(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra as rotinas.
void registerPaletteSlots(SubroutineDispatcher& d);

/**
 * @brief Cor 15 bits (5:5:5) com cada canal = FIX(a + (b − a)·t), como a `0x5D00`. O bit 15 se perde.
 * @details Como no original, o resultado de cada canal não é limitado a 5 bits: com t fora de [0, 1]
 * ele pode passar de 31 ou ficar negativo e invadir os canais vizinhos (o OR é feito sem máscara).
 */
inline uint32_t lerpColor15(uint32_t a, uint32_t b, double t) {
    uint32_t out = 0;
    for (uint32_t sh : {0u, 5u, 10u}) {
        const double ca = static_cast<double>((a >> sh) & 0x1Fu), cb = static_cast<double>((b >> sh) & 0x1Fu);
        out |= static_cast<uint32_t>(static_cast<int32_t>(std::floor(ca + (cb - ca) * t))) << sh;
    }
    return out;
}

/// @brief Cor 15 bits com cada canal = FIX(c·f).
inline uint32_t scaleColor15(uint32_t c, double f) {
    uint32_t out = 0;
    for (uint32_t sh : {0u, 5u, 10u}) {
        const double v = static_cast<double>((c >> sh) & 0x1Fu) * f;
        out |= (static_cast<uint32_t>(static_cast<int32_t>(std::floor(v))) & 0x1Fu) << sh;
    }
    return out;
}

/**
 * @brief `0x9FB6`: copia a paleta AR0 (cabeçalho + 128 palavras = 256 cores) para AR1 escalando as
 *        cores por R0 (float) e a envia (`0x9EDB` + `0x9F5C`).
 * @details Preserva AR0–AR2, R0, R1, R6, R7; altera R2–R5, AR5.
 */
void scalePalette(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

} // namespace rea::core::video
