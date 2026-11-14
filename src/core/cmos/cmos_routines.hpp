#pragma once
/**
 * @file cmos_routines.hpp
 * @brief Acesso à CMOS (NVRAM): ajustes do operador e auditorias.
 *
 * @details Tradução das rotinas `0x993F`–`0x9A1F` da ROM.
 *
 * **Formato:** a CMOS tem 8 bits úteis, ligados às linhas D24–D31 do
 * barramento. Cada valor de 32 bits ocupa 4 palavras consecutivas, com o
 * **byte mais significativo primeiro** (big-endian), ao contrário do resto do
 * jogo:
 *
 * | Palavra | Bits 24–31 contêm |
 * |---------|-------------------|
 * | +0      | bits 31–24 do valor |
 * | +1      | bits 23–16          |
 * | +2      | bits 15–8           |
 * | +3      | bits 7–0            |
 *
 * O endereço do item `n` é `[0x9936] + 4·n`, onde `[0x9936]` = `0x9C0000`
 * (início da CMOS). A escrita exige destravar a proteção (`0x995020` com bits
 * 0xC00) e usa wait states lentos no barramento (`0x808064 = 0x90`).
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::cmos {

/// Endereço RAM que guarda o início da tabela de ajustes/auditorias (= 0x9C0000).
inline constexpr uint32_t kTableBasePtr = 0x9936;
/// Variável com bits extras (0xF00) combinados no registrador de proteção.
inline constexpr uint32_t kProtectExtra = 0xE572;
inline constexpr uint32_t kCoinCount1   = 0xE573; ///< Pulsos pendentes do contador mecânico 1 (bit 0x100).
inline constexpr uint32_t kCoinCount2   = 0xE574; ///< Pulsos pendentes do contador mecânico 2 (bit 0x200).
inline constexpr uint32_t kCoinTick     = 0xE575; ///< Quadros até a próxima troca de fase (8).
inline constexpr uint32_t kCoinPhase    = 0xE576; ///< 1 = pulso ligado.
inline constexpr uint32_t kCmosProtectPort = 0x995020;  ///< Proteção de escrita da CMOS (libera só com bits 0xC00) e pulsos dos contadores mecânicos (bits 0x100/0x200).

/** @brief `0x993F`: wait states lentos para a CMOS (`0x808064 = 0x90`). Saída: R1 = 0x90. */
void slowBus(Tms32031State& cpu, MemoryBus& bus);
/** @brief `0x9945`: wait states normais (`0x808064 = 0x470`). Saída: R1 = 0x470. */
void normalBus(Tms32031State& cpu, MemoryBus& bus);
/** @brief `0x994B`: trava a CMOS (`0x995020 = [E572] & 0xF00`). Saída: R1. */
void lock(Tms32031State& cpu, MemoryBus& bus);
/** @brief `0x9953`: destrava a CMOS (`0x995020 = ([E572] & 0xF00) | 0xC00`). Saída: R1. */
void unlock(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x99F9`: lê um valor de 32 bits a partir de AR2.
 * @param cpu Entrada: AR2 = endereço. Saída: R0 = valor; AR2 += 4. R1 preservado.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void read32(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x9A0D`: grava R2 (32 bits) a partir de AR2, destravando a CMOS.
 * @param cpu Entrada: AR2 = endereço, R2 = valor. Saída: AR2 += 4. R1–R3 preservados.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void write32(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x99E9`: lê o item AR2 da tabela.
 * @param cpu Entrada: AR2 = índice; DP = 0. Saída: R0 = valor; AR2 = endereço do item.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void getItem(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x99F4`: grava R2 no item AR2.
 * @param cpu Entrada: AR2 = índice, R2 = valor; DP = 0. Saída: AR2 = endereço do item.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void setItem(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x99DA`: incrementa o item AR2 (contador de auditoria).
 * @param cpu Entrada: AR2 = índice; DP = 0. Saída: R0 = R2 = novo valor; AR2 = endereço + 4.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void incrementItem(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x99E2`: soma R2 ao item AR2.
 * @param cpu Entrada: AR2 = índice, R2 = parcela; DP = 0.
 *            Saída: R0 = valor antigo; R2 = novo valor; AR2 = endereço + 4.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void addItem(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x99CA`: checksum dos ajustes = soma dos itens 0x00–0x2D.
 * @param cpu Saída: R0 = soma. R1, AR2 e AR5 preservados.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void checksum(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x99EE`: grava R2 no item AR2 e atualiza o checksum no item 0x35.
 * @param cpu Entrada: AR2 = índice, R2 = valor. Saída: R2 = checksum; AR2 = endereço do item 0x35.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void setItemWithChecksum(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// Item da CMOS que guarda o checksum dos ajustes.
inline constexpr uint32_t kChecksumItem = 0x35;
/// Quantidade de itens somados no checksum (0x00–0x2D).
inline constexpr uint32_t kChecksumCount = 0x2E;

/// RAM que guarda o ponteiro da tabela (mín, máx, padrão) dos ajustes (= 0xC12437, ROM).
inline constexpr uint32_t kRangeTablePtr = 0xC733;
/// Item limitado a 0–10 por `0x995C` (significado ainda não identificado).
inline constexpr uint32_t kItemClamp10 = 0x70;
/// Item limitado a 0–30 por `0x995C`.
inline constexpr uint32_t kItemClamp30 = 0x71;

/**
 * @brief `0x9981`: confere cada ajuste 0x00–0x2D contra a tabela (mín, máx, padrão).
 *
 * @details Para cada item `n`, lê o trio `t[3n..3n+2]` a partir de `[0xC733]`.
 * Se o valor estiver fora de `[mín, máx]` (comparação COM sinal), grava o
 * padrão via `0x99EE` (que também atualiza o checksum).
 * @param cpu Todos os registradores usados (R0–R4, AR2, AR5, AR6) são preservados.
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void validateRanges(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x995C`: validação geral da CMOS chamada no fim do RESET.
 *
 * @details Limita o item 0x70 a 0–10 e o 0x71 a 0–30, valida a tabela de
 * recordes (`0x9AA9`) e os ajustes (`0x9981`). Termina com
 * `ANDN 1,ST`: o carry sai SEMPRE zerado, logo o `BLO $4FEF` do RESET nunca
 * é tomado nesta versão da ROM.
 */
void validateAll(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief `0x9972`: zera 0xC3 contadores de 32 bits a partir de `[9936]` + 0xDC (auditoria).
void clearAudits(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x99A3`: ajustes 0x00..0x2D = padrões de `[C734]`; ajuste 0 = registro de preço dos DIPs
///        (`0x73B7`, limitado pelo máximo de `[C735]`); grava o checksum (item 0x35).
void factoryAdjustments(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x0DCE`: restauração de fábrica: `0x9972`, `0x9A99`, `0x99A3`, itens 0x70/0x71 = 0, aviso
///        `[0DDA]` (`0x0E62`) e tela de configuração (`0x0CC0`, não traduzidas).
void factoryRestore(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief `0x9AD2`: AR2 = endereço da entrada R7 (0..9) da tabela de recordes R6 (0..15):
///        `[9936]` + 0x3F8 + R6·0x50 + R7·8.
void hiscoreAddress(Tms32031State& cpu, MemoryBus& bus);
/// @brief `0x9AEB`: grava em AR2 o tempo R0 (`0x9A0D`) e os bytes R1..R4 (iniciais, carro). AR2 += 8.
void hiscoreWrite(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x9ADC`: grava a entrada padrão AR1 (tempo; carro:iniciais em bytes) via `0x9AEB`.
void hiscoreWriteDefault(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x9B06`: lê a entrada AR2: R0 = tempo, R1..R3 = iniciais, R4 = carro. AR2 += 8.
void hiscoreRead(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x9A99`: recria as 15 tabelas com os padrões `[C736]` (pistas) e `[9A62]` (corrida inteira)
///        e chama `0x9A77` (tabela 15, entrada 0 = 15 × −1).
void hiscoreReset(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x9A77`: grava 15 valores −1 a partir da entrada 0 da tabela 15.
void hiscoreClearExtra(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
/// @brief `0x9AA9`: confere as tabelas 0..9 (iniciais ' ' ou '0'..'Z', carro 0..10, tempo 0..14400);
///        qualquer entrada inválida → `0x9A99`.
void hiscoreValidate(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x75F1`: contadores mecânicos de moedas (chamada a cada quadro).
 * @details A cada 8 quadros alterna a fase `[E576]`:
 *          - ligando: para `[E573]`/`[E574]` > 0, decrementa e liga o bit 0x100/0x200;
 *            grava em `0x995020` e em `[E572]`;
 *          - desligando: `[E572]` &= ~0xF00 e grava em `0x995020`.
 *          O GIE é desligado em volta da escrita e religado pela `0x4C33` (`RETIU`). DP = 0.
 */
void coinMeters(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra todas as rotinas deste módulo no despachante (inclui `0x75F1` e `0x4C33`).
void registerRoutines(SubroutineDispatcher& d);

} // namespace rea::core::cmos
