#pragma once
/**
 * @file serial_printer.hpp
 * @brief UART do controlador da plataforma de movimento (Galil) no latch de saídas `0x995000`
 *        (`0x4834`, `0x48D2`, `0x4A4E`).
 *
 * @note O nome do arquivo vem de uma leitura antiga: os textos enviados ("XA=", "YA=", "ZA=") e as mensagens
 * ("MOTION SETUP TESTING...", "X ENCODER FAILURE") mostram que o dispositivo é o controlador de movimento de
 * 3 eixos, não uma impressora/modem. O menu de movimento e a instalação estão em `service/service_motion.cpp`.
 *
 * @details Só funciona com o ajuste 0x34 da CMOS = 1 e o bit 0x40 de `[0x992000]` desligado
 * (`0x4693`/`0x469D`). Cada acesso usa o protocolo de 2 fases do latch (ver `cabinet_outputs.hpp`)
 * com estes endereços:
 *
 * | Endereço | Uso |
 * |---|---|
 * | 8 | recepção: dado nos bits 8–15 da leitura |
 * | 9 | transmissão: um caractere por escrita (`0xFF00 \| c`, `0xFB00 \| c`, `0xFF00 \| c`) |
 * | 10 | estado: bit 15 da leitura = sem dado/ocupado (carry do `LSH 17`) |
 *
 * Entre as escritas, `0x4689` espera 101 ciclos e, no modo 1, alimenta o watchdog (`0x5029`).
 *
 * **`0x4834`** (laço principal, fora do modo 4): envia as posições dos 3 eixos em 3 linhas `[C7B7..C7CB]`, cada uma com
 * `0xA762` (número 0 em decimal), `0x77D1` (copia o prefixo) e duas `0x77E7` (concatena); envia
 * (`0x48D2`) e trata a resposta (`0x4A4E`).
 *
 * **`0x4A4E`**: até 1000 tentativas; com dado recebido (`0x49EA`) interpreta comandos de 1 letra:
 * `G` (ajuste 0x80 = 1 e desliga a impressora), `V $hh` (avisos `[EB52]` ≥ 2 se faltar o bit 1 ou 2),
 * `W`/`S`/`Q` (mostra o texto `[C7D4]`/`[C7D5]`/`[C7D6]` e desliga), `X`/`Y`/`Z` `$hh` → `[EBB8]`..`[EBBA]`.
 * Sem resposta em 1000 tentativas: desliga a impressora (ajuste 0x34 = 0).
 *
 * `0x4954`/`0x48C4` (configuração), que esperam a INT0 em laço ativo (`0x4EC7`), estão em
 * `service/service_motion.cpp`.
 */

#include "core/subroutines.hpp"

namespace rea::core::io {

/// @brief Registra `0x4689`, `0x4834`, `0x48D2`, `0x49B8`, `0x49CE`, `0x49EA`, `0x4A2E`, `0x4A41`,
///        `0x4A4E`, `0x77D1` (cópia de texto empacotado) e o início do modem: `0x4530` (RESET: item 0x34 = 1,
///        item 0x80 = 0; sem o DIP 0x40 de `[0x992000]` >> 16 volta, com ele segue em `0x4539`, a instalação em `service/service_motion.cpp`),
///        `0x4693` (Z = DIP do modem ligado), `0x469D` (Z = item 0x34 = 1) e `0x49A7` (`[EBB7]` = 0).
void registerSerialPrinter(SubroutineDispatcher& d);

} // namespace rea::core::io
