#pragma once
/**
 * @file credits_display.hpp
 * @brief Textos de créditos do rodapé (`0x745E`–`0x7591`, `0x762E`, `0x73A0`–`0x73B5`,
 *        `0x77E7`, `0x4693`, `0x469D` e os leitores de campos `0xC10042`–`0xC1009E`).
 *
 * @details **`0x745E`** é chamada a cada iteração do loop principal:
 * 1. `0x7594` (volume) e `0x762E` (aviso: sem o bit 0x40 da porta `0x992000` e com o
 *    item 0x34 da CMOS ≠ 1, mostra `[762D]` = "MOTION OFF" em (256, 305));
 * 2. com `[E541]` ≠ 0, para aqui;
 * 3. jogo grátis (item 0x19 = 1): "PRESS START" piscando (`0x74B7`, y = 360);
 * 4. no atrato com `[C8E8]` ímpar e sem créditos nem moedas (itens 0x71 e 0x70), ou no
 *    modo 7: só "INSERT COINS" piscando (`0x749F`, y = 360);
 * 5. senão monta um bloco centrado em x = 256 a partir de
 *    `y = 345 − (17·linhas + 44)/2`:
 *    - "PRESS START" (créditos ≥ o preço de início) ou "n CREDITS TO START/CONTINUE"
 *      (`0x7544`), y += 22;
 *    - "INSERT COINS" (sem créditos nem moedas) ou o saldo "c m/n CREDITS" (`0x7510`), y += 22;
 *    - até 3 linhas da tabela de preços (`0x74D1`, estilo `0x76E2`, paleta `[B121]`), 17 em 17.
 *
 * Os textos são criados com vida 1 (somem no próximo quadro) e a flag 0x100 (centralizado).
 * O pisca usa `[E56C]`: conta de −1 para baixo até −N (aceso) e de 1 para cima até N (apagado).
 *
 * **Tabela de preços** (`0x73A0`): com o item 0x17 da CMOS = 1, o registro
 * `[B080] + (item 0 & 0x7F) · 11` (limitado a `[B081]`); senão o registro personalizado
 * `[742D]` = 0xE542 (montado pela `0x7409`). Campos lidos pelas rotinas em `0xC1xxxx`:
 *
 * | Rotina | Campo | Uso |
 * |---|---|---|
 * | `0xC10042` | `+1` bits 0–7 | denominador das moedas parciais |
 * | `0xC1004E` | `+1` bits 8–15 | moedas para o bônus (`0x7386`) |
 * | `0xC1005B` | `+1` bits 16–23 | — |
 * | `0xC10068` | `+1` bits 24–31 | créditos para começar |
 * | `0xC10075` | `+2` bits 0–7 | créditos para continuar |
 * | `0xC10081` | `+2` bits 8–15 | modo de exibição do saldo |
 * | `0xC1000F`…`0xC10035` | `+0` bytes 0–3 | unidades por moeda das entradas 1–4 (em R2) |
 * | `0xC1008E`…`0xC100A8` | `+0xA` bytes 0–3 | pulsos do contador mecânico das entradas 1–4 (em R0) |
 *
 * **Flags de condição:** `0x4693` (TSTB) e `0x469D` (CMPI) devolvem o resultado no ST.
 * O port não modela o bit Z, então os chamadores traduzidos usam `serviceBit()` e
 * `item34IsOne()` direto; as entradas registradas deixam R0 como no original.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// Variáveis dos textos de créditos.
namespace credit_vars {
inline constexpr uint32_t kBlink        = 0xE56C; ///< Contador do pisca.
inline constexpr uint32_t kHide         = 0xE541; ///< != 0: sem textos de créditos.
inline constexpr uint32_t kStartFlag    = 0xC8CD; ///< 1 quando "PRESS START" está à vista.
inline constexpr uint32_t kAttractCase  = 0xC8E8;  ///< Caso da atração/partida (−1 a −7).
inline constexpr uint32_t kGameMode     = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.
inline constexpr uint32_t kFrames       = 0xC92F;  ///< Quadros (VBLANKs) gastos na última iteração do loop principal.
inline constexpr uint32_t kInputPort    = 0x992000; ///< Bits 16–31: entradas; bit 0x40 testado pela `0x4693`.
inline constexpr uint32_t kNoticePtr    = 0x762D;   ///< `[762D]` = 0xC106B7: "MOTION OFF".
inline constexpr uint32_t kInsertCoins  = 0x7455;   ///< `[7455]` → "INSERT COINS".
inline constexpr uint32_t kPressStart   = 0x745A;   ///< `[745A]` → "PRESS START".
inline constexpr uint32_t kNumBuf1      = 0x7439;   ///< `[7439]` = 0xE559.
inline constexpr uint32_t kNumBuf2      = 0x743A;   ///< `[743A]` = 0xE55A.
inline constexpr uint32_t kBalanceStr   = 0x743B;   ///< `[743B]` = 0xE55B: "c m/n CREDITS".
inline constexpr uint32_t kPriceStr     = 0x743C;   ///< `[743C]` = 0xE563: "n CREDITS TO START".
inline constexpr uint32_t kSlash        = 0x743D;   ///< `[743D]` → "@" (a barra da fonte).
inline constexpr uint32_t kSpace        = 0x743F;   ///< `[743F]` → " ".
inline constexpr uint32_t kTwoSpaces    = 0x7441;   ///< `[7441]` → "  ".
inline constexpr uint32_t kCredits      = 0x7443;   ///< `[7443]` → "CREDITS".
inline constexpr uint32_t kCredit       = 0x7446;   ///< `[7446]` → "CREDIT".
inline constexpr uint32_t kDenomBuf     = 0x7449;   ///< `[7449]` = 0xE56B.
inline constexpr uint32_t kToStart      = 0x7430;   ///< `[7430]` → " TO START".
inline constexpr uint32_t kToContinue   = 0x7434;   ///< `[7434]` → " TO CONTINUE".
inline constexpr uint32_t kCustomPrices = 0x742D;   ///< `[742D]` = 0xE542.
inline constexpr uint32_t kPriceTable   = 0xB080;  ///< Início da tabela de preços (11 palavras cada).
inline constexpr uint32_t kPriceTableEnd = 0xB081;  ///< Fim da tabela de preços.
inline constexpr uint32_t kPricePalette = 0xB121;  ///< Paleta dos preços.
inline constexpr uint32_t kTextPalette  = 0xB120;  ///< Paleta do texto.
inline constexpr uint32_t kItemCoinage  = 0x00;  ///< Item da CMOS: tabela de moedas.
inline constexpr uint32_t kItemCustom   = 0x17;  ///< Item da CMOS: preços personalizados.
inline constexpr uint32_t kItemFreePlay = 0x19;  ///< Item da CMOS: jogo grátis.
inline constexpr uint32_t kItemNotice   = 0x34;  ///< Item da CMOS: aviso de preço.
inline constexpr uint32_t kItemCoins    = 0x70;   ///< Moedas parciais.
inline constexpr uint32_t kItemCredits  = 0x71;  ///< Item da CMOS: créditos.
} // namespace credit_vars

/// @brief `0x4693`: true se o bit 0x40 de `[0x992000] >> 16` está ligado (o original devolve o Z do TSTB).
bool serviceBit(MemoryBus& bus);
/// @brief `0x469D`: true se o item 0x34 da CMOS é 1 (o original devolve o Z do CMPI).
bool item34IsOne(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief `0x73A0`: AR0 = registro de preços (ver tabela no arquivo).
void priceRecord(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x73B7`: registro de preço pelos DIPs de moeda/país (`~[0x992000] >> 24`, bits invertidos):
 *        AR2 = `[[B083] + (d & 0x1F)·4 + (d & 0x60) >> 5]` (limitado a `[B085]`); R0 = R2 = índice
 *        desse registro na tabela `[B080]` (11 palavras cada, `0xA531`). DP = 0.
 */
void coinDipPrice(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x77E7`: concatena o texto AR1 ao fim do texto AR0 (4 caracteres por palavra, LSB primeiro).
 * @details Faz OR byte a byte (supõe zeros depois do terminador) e zera cada palavra nova.
 *          Preserva todos os registradores que usa.
 */
void appendString(Tms32031State& cpu, MemoryBus& bus);

/// @brief `0x745E`: textos de créditos do quadro (ver o arquivo).
void creditsDisplay(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra as rotinas deste arquivo.
void registerCreditsDisplay(SubroutineDispatcher& d);

} // namespace rea::core::game
