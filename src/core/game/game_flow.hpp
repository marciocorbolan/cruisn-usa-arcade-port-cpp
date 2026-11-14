#pragma once
/**
 * @file game_flow.hpp
 * @brief Processo de fluxo da partida (`0x1775`, criado pela `0x95C4` a partir de `[C70F]`) e
 *        as rotinas que ele usa diretamente.
 *
 * @details **Chamada local.** Os processos do jogo têm uma pilha própria dentro do PCB: `[PCB+1]`
 * aponta para o topo (começa em PCB + 0x2F). Uma "chamada local" empilha o endereço de retorno
 * (lido de uma tabela, ex. `[B1EA]`) e salta (`BU`) para a sub-rotina (`[B1E9]`); a sub-rotina pode
 * adormecer quantas vezes quiser e volta desempilhando o endereço e saltando para ele (`BU R0`).
 * No port, os dois saltos viram despachos de cauda (`subs.call(alvo)` seguido de `return`), e o PC
 * do PCB é ajustado para o alvo antes do salto — assim, se o alvo ainda não estiver traduzido, o
 * escalonador o reexecuta (e o lista como pendente) em vez de repetir o chamador.
 *
 * **Fluxo** (modo 3, depois do START):
 * 1. `0x1775`: `[E636]` = 2, modo `[C8C5]` = 0x203, `[E637]` = 1; chamada local `0x1722`;
 * 2. `0x1722` (link): sem link (`[C8D1]` = 0), avisa o link (`0x7A5F`), espera 1 quadro, zera o estado
 *    do link (`0x793A`), `[E63D]` = 1 e programa o modo mestre (`0x7F42`); com link, pode mostrar a
 *    tela de espera `0x169C` e aguardar até 30 quadros pela resposta `[C8DE]`;
 * 3. `0x1783`: limpa recordes extras (`0x9A77`), item 0x33 = −1, auditorias 0x6F/0xD2/0x75, item 0x21
 *    − 1 (`0x9937`), confere os itens 0x8F/0x90 (erro 0x700), espera `[0x809A79]` = 0, som 0xF3,
 *    zera o estado da corrida, carrega os ajustes (`0x317C`) e faz a chamada local `0x6C8F`
 *    (seleção do percurso);
 * 4. `0x17C5`: chamada local `0x59EC` (seleção do carro, `[E63E]` = 0) e `0x18A6` (`[E63E]` = 1);
 * 5. `0x17DA`: espera `[E63F]` ≠ 0 e prepara a corrida (`0x17E0`): tempo `[E604]` = item 0x1C · 5 +
 *    60, cria `[B1F1]` (ID 0x5C0), mata 0x5C1, auditoria 0x72, item 0xDF = 0 e morre; no atrato
 *    (`[E637]` = 0) segue para `0x29EE`.
 *
 * `0x181F` é o ramo do atrato (percurso `[E634]`: tabelas `[8C12]`, `[18A5]`, `[3965]`); `0x180C`
 * encerra o jogo de atrato e volta a `0x17A7`.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// Variáveis do fluxo da partida.
namespace flow_vars {
inline constexpr uint32_t kGameMode    = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.
inline constexpr uint32_t kLinkOn      = 0xC8D1; ///< ≠ 0: gabinetes ligados em rede.
inline constexpr uint32_t kLinkMaster  = 0xC8D0;  ///< != 0: não troca pacotes.
inline constexpr uint32_t kCabFlags    = 0xC8CC; ///< Bit 0: gabinete da direita; bit 2: atrato.
inline constexpr uint32_t kPlaying     = 0xE637; ///< 1: jogo com jogador; 0: atrato.
inline constexpr uint32_t kRaceReady   = 0xE63F; ///< ≠ 0: corrida pronta para começar.
inline constexpr uint32_t kSelectStep  = 0xE63E; ///< 0: seleção do carro; 1: transmissão.
inline constexpr uint32_t kTimer       = 0xE604; ///< Tempo da corrida (segundos).
inline constexpr uint32_t kCourse      = 0xE634; ///< Percurso escolhido.
inline constexpr uint32_t kLinkWaitObj = 0xEA33; ///< Objeto da tela de espera do link.
inline constexpr uint32_t kGasSetup    = 0xE89A; ///< Floats dos itens 1..7 da CMOS (`0x317C`).
inline constexpr uint32_t kLinkModeReg = 0x997000;  ///< Placa de link: +0 dados (bits 16–31), +1 flags (bits 29–31).
} // namespace flow_vars

/**
 * @brief Empilha @p value na pilha local do processo atual (`[AR7+1]`).
 */
void localPush(Tms32031State& cpu, MemoryBus& bus, uint32_t value);

/// @brief Desempilha da pilha local do processo atual.
uint32_t localPop(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief Salto de processo (`BU`): grava @p pc como PC do PCB e despacha.
 * @details O chamador deve retornar logo depois (o salto não volta).
 */
void processJump(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t pc);

/// @brief Chamada local: empilha `[retPtr]` e salta para `[targetPtr]`.
void localCall(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t targetPtr, uint32_t retPtr);

/// @brief Retorno local (`LDI *--AR0,R0 / BU R0`).
void localReturn(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra o processo `0x1775` e seus pontos de retomada, `0x168C`, `0x169C`, `0x1720`,
///        `0x1722`, `0x180C`, `0x181F`, e as rotinas `0x1C5B`, `0x317C`, `0x6DFB`, `0x6FD4`,
///        `0x7F42`, `0x7F4D`, `0x9937`, `0x9EEC`, `0xB06F`.
void registerGameFlow(SubroutineDispatcher& d);

} // namespace rea::core::game
