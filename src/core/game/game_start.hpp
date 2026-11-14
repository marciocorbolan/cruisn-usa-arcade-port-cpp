#pragma once
/**
 * @file game_start.hpp
 * @brief (Re)início do jogo (`0x9503`) e seus casos (`0x9563`–`0x95C9`).
 *
 * @details A `0x9503` é chamada pelo RESET (`0x4BA1`, com AR2 = −1), pela
 * `0x4FAE` e pela ressincronização do Slave no loop principal. Ela:
 * 1. tira o próprio retorno da pilha (`POP AR7`), porque vai zerar a RAM
 *    interna (0x809800–0x809FFF, onde fica a pilha), e o devolve depois;
 * 2. chama `0x9166` e `0x7F18`; limpa a BSS (`0x4E35`), preservando
 *    `E573`–`E576`; copia 0x10001 palavras da RAM principal para a RAM rápida
 *    (0x400000);
 * 3. chama `0x7F4D` e `0x95CB` (reinício dos processos); zera a câmera em
 *    0x8098xx e grava 0,70703 em `[809812]`/`[809813]`; chama `0x7409` e
 *    `0x93D7`;
 * 4. faz `[E3FD]` = 35,0, `[C93F]` = 1 (pede troca de página),
 *    `[EA48]` = FLOAT(item 5 da CMOS) e zera `C8EB`, `C8EC`, `E541` e `E8E3`;
 * 5. chama `0x95F5` e cria o processo de sistema `[C701]` (= 0x903B, ID 0x500);
 * 6. escolhe o caso:
 *    - AR2 == 1: `0x95C4`;
 *    - `~[0x991030] & 0x70 == 0x60` (VIEW2+VIEW3 apertados, VIEW1 solto): `0x95B6` (créditos finais);
 *    - senão: `CALLU [0x9563 + AR2]`, com AR2 negativo, numa tabela que fica
 *      logo antes de `0x9563`.
 *
 * | AR2 | Caso | Efeito |
 * |---|---|---|
 * | −1 | `0x9579` | atrato: modo 0x22, `[C943]` = −1, processos `[C703]` (0x3859) e `[C704]` (0x9563) |
 * | −2 | `0x9587` | modo 2, `CALL $59E3`, processos `[C705]` e `[C706]` (AR6 = `[C8CF]`) |
 * | −3 | `0x95A4` | modo 2, `[C8E6]` = 0, processo `[C70A]` (ID 0x202), `[E84F]` = 900, processos `[C70B]` e `[C70C]` |
 * | −4 | `0x9596` | carrega `[C707]` (LZW), modo 2, processos `[C708]` e `[C709]` |
 * | −5 | `0x95B4` | `0x95BC` (carrega `[C70E]`, `[C8E8]` = −1), depois o caso −1 |
 * | −6 | `0x95B6` | processo `[C70D]`, `[C8E8]` = −1 |
 * | −7 | `0x9572` | processo `[C702]` |
 * | −8 | `0x9563` | espera o outro gabinete (ver abaixo) |
 *
 * Todos os casos −1 a −7, exceto −6, começam com `[C8CD] &= ~0xE`.
 *
 * **Reinício (`0x1E17`/`0x1E1C`)**: descartam a pilha (`SP = [0x63]`), chamam
 * a `0x9503` com AR2 = `[C8E8]` e saltam para `0x4C1B` no loop principal. A
 * `0x1E1C` antes avança `[C8E8]` no ciclo −1 → −2 → −3 → −4 → −1 (as telas do
 * modo de atração). No port, o salto vira `cpu.jump = 0x4C1B`.
 * O processo `0x1E26` (`[C70B]`) espera `[E84F]` quadros e chama a `0x1E1C`.
 *
 * **Processo `0x903B`** (`[C701]`, sempre criado): a cada quadro alterna
 * `0x726F` (promove objetos próximos para a lista visível) e `0x7254` (rebaixa
 * os distantes).
 *
 * **Processo `0x9563`:** adormece 1 quadro por vez até o outro gabinete estar
 * no modo 5, 6 ou 7, ou até `[C8DD] != 0`. Então faz `[C8E8]` = −7 e salta
 * para `0x1E17`.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief `0x9503`. Entrada: AR2 = caso. Saída: AR7 = `cpu.ret` (o retorno tirado da pilha).
void startGame(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra `0x9503`, os casos e o processo `0x9563`/`0x9565`.
void registerRoutines(SubroutineDispatcher& d);

} // namespace rea::core::game
