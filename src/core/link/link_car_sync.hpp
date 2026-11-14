#pragma once
/**
 * @file link_car_sync.hpp
 * @brief Sincronização de carros pelo link (`0x7ACF`–`0x7B24` envio, `0x7B34`–`0x7BAF` recepção).
 *
 * @details Uma mensagem de 46 palavras (um byte cada no cabo) leva o estado de um carro:
 *
 * | Palavras | Conteúdo | Origem (envio) | Destino (recepção) |
 * |---|---|---|---|
 * | 0 | tipo: 7 (`0x7ADB`), 0xC (`0x7AD7`), 0x12 (`0x7ACF`) | | |
 * | 1 | identificação | `*+AR7(E)` (7/0xC) ou `*+AR5(59)` (0x12) | 0xC: índice na tabela `[0x508E]`; 0x12: procura em `[E404]` o carro com `+59` igual |
 * | 2–13 | 3 inteiros de 32 bits, LSB primeiro | `FIX` de `[AR4+1..3]` (posição) | `FLOAT` → `[AR4+1..3]` |
 * | 14–31 | 9 valores de 16 bits | `FIX([AR4+4..12] · 32000)` (matriz) | com sinal · `[7B32]` (≈ 1/32000) |
 * | 32–37 | 3 ângulos de 16 bits | `FIX(0x9308(+2C/+21/+2D) · 1000)` dos dados `[AR4+1B]` | com sinal · `[7B33]` (≈ 1/1000) |
 * | 38–39 | 16 bits | `FIX(+26)` | `FLOAT` com sinal → `+26` |
 * | 40, 41 | bytes | `+1F`, `+20` | `+1F`, `+20` |
 * | 42 | byte | `FIX(+2B · 100)` | · 0,009998 → `+2B` |
 * | 43–45 | 24 bits | `+57` | `+57` (a 3ª palavra somada inteira, como no original) |
 *
 * `0x7ACF` e `0x7AD7` antes copiam `*+AR7(13)` para `*+AR5(57)`. A recepção marca `+5A` = 1 nos dados do carro.
 *
 * **Batida (tipos 0x13/0x14, `0x2587`–`0x25D5`)**, enviadas pelo fim da batida (`0x2558`):
 * - 0x13 (`0x2587` → `0x258F`): só o carro (`*+AR5(59)`); quem recebe faz o processo do carro (`[AR0+17]`)
 *   retomar em `[2595]` = 0x2560.
 * - 0x14 (`0x2597` → `0x25AD`): carro, R4 e as 6 palavras `*+AR7(C..11)` do processo (4 bytes cada, 27 palavras).
 *   Quem recebe copia as 6 palavras e faz o processo retomar em `[2596]` = 0x255E se `+57` estiver fora da
 *   janela `[[E46E]+1E]`..`[[E470]+1E]`; dentro dela em 0x2478 (R4 = 0), 0x24CF (R4 = 1) ou 0x257B.
 *
 * **Tipo 0xD (`0x5278`)**: 1 byte = índice (0..7) na tabela `[0x508E]`. O carro volta a ser deste gabinete: o
 * processo `[AR4+17]` recebe `+F`/`+21`/`+22` = 1,0, `+23` = 0x4B, `+24` = 0x1B, `+25` = 1, `+13` = `+57` dos dados
 * (com `+58` = 0) e retoma em 0x5208 (`+57` ≥ `[[E470]+1E]`), 0x52A8 (abaixo de `[[[[E46E]+21]+21]+21]+1E`) ou
 * 0x5225.
 *
 * Tratadores: tipos 0x12 e 0x15 → `0x7B43`, 0xC → `0x7B49`, 7 → `0x7B58` (carro `[E592]`). Com `[E593]` ≠ 0,
 * ou sem carro de destino, o payload é pulado (`AR2 += 0x2D`).
 */

#include "core/subroutines.hpp"

namespace rea::core::link {

/**
 * @brief `0x5255` (@p behind = true) / `0x525A`: passagem do carro de tráfego para o outro gabinete.
 * @details Compara as voltas (`0x5268`: R1 − R0, focado − carro `[E592]`): `0x5255` segue se > 0, `0x525A` se < 0.
 *          Então, se `*+AR7(13)` estiver dentro de `[E8AD]`..`[E8AE]` (`0x525F`), envia o tipo 0xD com
 *          `*+AR7(E)` (`0x526F`) e o original faz `POP R0 / BU $A718`.
 * @return true quando o chamador deve seguir para `0xA718` (o carro passa a ser do outro gabinete).
 */
bool trafficHandOff(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, bool behind);

/// @brief Registra `0x7ACF`, `0x7AD7`, `0x7ADB`, `0x7B34`, `0x7B43`, `0x7B49`, `0x7B58`, `0x2587`, `0x258F`,
///        `0x2597`, `0x25AD` e `0x5278` (tipo 0xD).
void registerCarSync(SubroutineDispatcher& d);

} // namespace rea::core::link
