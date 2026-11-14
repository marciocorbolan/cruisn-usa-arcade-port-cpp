#pragma once
/**
 * @file link_messages.hpp
 * @brief Mensagens do link: leitura do pacote recebido (`0x794C`), tratadores e emissores.
 *
 * @details O pacote recebido (`[0x7F16]` = 0x1F258) tem um byte por palavra, em
 * `bits 16–23`. A `0x794C` lê o tipo (1 byte) e chama `tabela[tipo]`
 * (`[0x7966]` = 0x7967, 0x21 entradas). O tratador consome o próprio payload
 * avançando AR2. Tipo ≥ 0x21 encerra a leitura. O tipo 0 (`0x79BF` = `RETS`)
 * é a palavra final que a `0x798F` anexa.
 *
 * | Tipo | Tratador | Payload → efeito | Emissor |
 * |---|---|---|---|
 * | 1 | `0x79E7` | estado do outro gabinete (10 bytes) | `0x7A41` |
 * | 2 | `0x7ABE` | `[E604]` (2 bytes), `[C93E]` = byte << 16 | `0x7A8D` |
 * | 3 | `0x7AA3` | `[C8DF]` (com sinal), `[C8E0]`, `[C8D7]` | `0x7A98` |
 * | 4 | `0x7AB2` | `[C8D8]` | `0x7AB7` |
 * | 5 | `0x7A6B` | início de corrida (ver handleType5) | `0x7A61` |
 * | 6, 8 | `0x7A8A` | `[C8E3] = 0` | `0x7A88` (tipo 8) |
 * | 9 / 0xA | `0x79DD` / `0x79E2` | `[C8DE] = 1 / 2` | `0x79D3` / `0x79D8` |
 * | 0xB | `0x7A5C` | `[C8DD] = 0` | `0x7A5F` |
 * | 0xE | `0x7BC3` | 1 byte (consulta a tabela `[0x508E]`) | — |
 * | 0xF | `0x7BB0` | 3 bytes → processo `0x9761` (`0xA844`) | — |
 * | 0x16–0x18 | `0x79C8`/`0x79CB`/`0x79CE` | `[E63B] = 1/2/3` (0x18 zera `[C93E]`) | `0x79C0`/`0x79C2`/`0x79C4` |
 * | 0x19 | `0x7BD1` | `[E8AD]`, `[E8AE]` (24 bits cada) | `0x7BE0` |
 * | 0x1A–0x1D | `0x7BFA` | `[C8E1] = 3` | `0x7BF6` (tipo 0x1D) |
 * | 0x1E | `0x7BFD` | 1 byte → `CALL $90E6` se `[C8E3]` | `0x7C07` |
 * | 0x1F | `0x7C0F` | `[E597] = 1` | `0x7C12` |
 * | 0x20 | `0x7C14` | 1 byte → `[9A6B]`, `[9A6C]`; responde (`0x7C1C`) | `0x7C1C` |
 *
 * Pendentes (outros subsistemas):
 * - 7, 0xC, 0x12, 0x15 (estado de carro): traduzidos em `link_car_sync.cpp`;
 * - 0xD (`0x5278`), 0x10 (`0x992B`), 0x11 (`0x98F2`), 0x13 (`0x258F`), 0x14 (`0x25AD`).
 */

#include <cstdint>
#include <functional>

#include "core/subroutines.hpp"

namespace rea::core::link {

/// Variáveis das mensagens.
namespace msg_vars {
inline constexpr uint32_t kHandlerTablePtr = 0x7966; ///< `[0x7966]` = 0x7967.
inline constexpr uint32_t kHandlerCount    = 0x21;  ///< Entradas da tabela de tratadores (tipos 0–0x20).
inline constexpr uint32_t kPeerMode        = 0xC8D6; ///< `[C8C5]` do outro gabinete.
inline constexpr uint32_t kPeerC8DA        = 0xC8D9; ///< `[C8DA]` do outro.
inline constexpr uint32_t kLocalC8DA       = 0xC8DA;  ///< 2 bytes na mensagem 1.
inline constexpr uint32_t kPeerC8E4        = 0xC8E5;  ///< `[C8E4]` do outro gabinete (mensagem 1).
inline constexpr uint32_t kPeerE607        = 0xC8E2;  ///< `[E607]` (posição) do outro gabinete (mensagem 1).
inline constexpr uint32_t kPeerC8E8        = 0xC8DB; ///< Byte com sinal; anterior em `[C8DC]`.
inline constexpr uint32_t kPeerC8E8Prev    = 0xC8DC;  ///< Valor anterior de `[C8DB]`.
inline constexpr uint32_t kPeerE639        = 0xC8D5; ///< Float: byte × 0,009998.
inline constexpr uint32_t kGameMode        = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.
inline constexpr uint32_t kRaceFlag        = 0xC8E3;  ///< 1 = corrida com link em andamento (tipo 5); 0 nos tipos 6 e 8.
inline constexpr uint32_t kVarC8D7         = 0xC8D7;  ///< Percurso escolhido pelo outro gabinete (−1 = nenhum).
inline constexpr uint32_t kVarC8D8         = 0xC8D8;  ///< Recebido no tipo 4; −1 = nenhum.
inline constexpr uint32_t kVarC8DD         = 0xC8DD;  ///< 1 no tipo 5, 0 no tipo 0xB.
inline constexpr uint32_t kVarC8DE         = 0xC8DE;  ///< Resposta do outro gabinete: 1 (tipo 9) ou 2 (tipo 0xA).
inline constexpr uint32_t kVarC8DF         = 0xC8DF;  ///< Recebido no tipo 3 (byte com sinal).
inline constexpr uint32_t kVarC8E0         = 0xC8E0;  ///< Recebido no tipo 3.
inline constexpr uint32_t kVarC8E1         = 0xC8E1;  ///< 3 quando chega um dos tipos 0x1A–0x1D (a largada espera por isso).
inline constexpr uint32_t kVarC93E         = 0xC93E;  ///< Conta 0–56: divide 57 quadros em ~1 segundo.
inline constexpr uint32_t kVarE592         = 0xE592;  ///< Carro de tráfego que está com o outro gabinete.
inline constexpr uint32_t kVarE593         = 0xE593;  ///< != 0: ignora as mensagens de estado de carro.
inline constexpr uint32_t kVarE594         = 0xE594;  ///< Zerado ao iniciar e no tipo 5.
inline constexpr uint32_t kVarE595         = 0xE595;  ///< 3 depois de enviar o tipo 0x1D (`0x7BF6`).
inline constexpr uint32_t kVarE596         = 0xE596;  ///< Cópia de `[E592]` feita na `0x793A`.
inline constexpr uint32_t kVarE597         = 0xE597;  ///< 1 quando chega o tipo 0x1F.
inline constexpr uint32_t kVarE604         = 0xE604;  ///< Contagem regressiva por segundo, só com `[C8C5] & 0x200`.
inline constexpr uint32_t kVarE63A         = 0xE63A;  ///< Bit de `[C8DA]` ligado pela `0x7A36`.
inline constexpr uint32_t kVarE63B         = 0xE63B;  ///< Passo da largada do mestre: 1, 2 ou 3 (tipos 0x16–0x18).
inline constexpr uint32_t kVarE404         = 0xE404;  ///< Lista de carros (`0x6762`).
inline constexpr uint32_t kVarE8AD         = 0xE8AD;  ///< Início da faixa de passagem de carros (tipo 0x19, 24 bits).
inline constexpr uint32_t kVarE8AE         = 0xE8AE;  ///< Fim da faixa de passagem de carros (tipo 0x19, 24 bits).
inline constexpr uint32_t kSlotTablePtr    = 0x508E; ///< `[0x508E]` = 0xE8A3: 10 palavras.
inline constexpr uint32_t kProcessPtr      = 0xB129; ///< `[0xB129]` = 0x9761: processo do tipo 0xF.
inline constexpr uint32_t kScratch3Ptr     = 0xB128; ///< `[0xB128]` = 0x809A1B (rascunho da mensagem 3).
inline constexpr uint32_t kVar9A6B         = 0x9A6B; ///< Endereços diretos com o DP do chamador (DP = 0).
inline constexpr uint32_t kVar9A6C         = 0x9A6C;  ///< Contador do tipo 0x20 (incrementado no envio).
} // namespace msg_vars

/// Diz se um tratador já está traduzido (para não dessincronizar a leitura).
using HandlerQuery = std::function<bool(uint32_t addr)>;

/**
 * @brief `0x794C`: lê o pacote recebido e despacha cada mensagem.
 * @details Só age com Linking ligada, `[C8D1] != 0` e `[C8D4] != 0`. Como
 *          `[C8D4]` = pares − 1, um pacote de 1 par é ignorado, como no original.
 *          Lê de AR2 até `AR2 ≥ base + 2·([C8D4] + 1)` ou até um tipo ≥ 0x21.
 *          Ao final, `[C8D4] = 0`.
 * @param translated Se fornecido e o tratador não estiver traduzido, a leitura
 *        para ali (desvio do port: o payload não consumido seria lido como tipos).
 * @param cpu Registradores do TMS320C31 (entrada e saída da rotina).
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param subs Despachante das sub-rotinas traduzidas (destino dos `CALL`).
 */
void parseReceived(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, const HandlerQuery& translated = {});

/**
 * @brief `0x793A`: zera o estado das mensagens do link.
 * @details Faz `[E596] = [E592]` e zera `C8DA`, `C8D9`, `C8D6`, `C8E3`, `E594`,
 *          `C8DD`, `E592`, `E404` e as 10 palavras de `[0x508E]`. Depois faz
 *          `[C8D7] = [C8D8] = −1`. É chamada no boot pela `0x4E20`.
 */
void resetLinkState(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x79E7`: mensagem 1 (estado do outro gabinete).
 * @details Reconstrói `[C8D6]`, `[C8D9]`, `[C8E5]`, `[C8E2]`, `[C8DB]`
 *          (o anterior vai para `[C8DC]`) e `[C8D5]`. Depois:
 *          - se o outro está no modo 2: `[C8E3] = 0` e `[C8D7] = [C8D8] = −1`;
 *          - senão, se o bit 0x800 do `[C8DA]` do outro está ligado e o nosso não:
 *            `[E604] = 0`;
 *          - senão, se os dois estão no modo 4 e `nosso & 0xF00 < dele & 0xF00`:
 *            `CALL $AD00`.
 *
 *          Preserva R0–R2.
 */
void handleStatus(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x7A6B`: mensagem 5.
 * @details Sempre faz `[C8DD] = 1`.
 *
 *          **Sem `[E594]`:** envia a mensagem 0xA e faz `[C8D7] = [C8D8] = −1`.
 *
 *          **Com `[E594]`:**
 *          1. `[C8E3] = 1`.
 *          2. Incrementa a auditoria 0xF8 (`0x99DA`).
 *          3. Zera `[C8DD]` e `[E594]`.
 *          4. Envia a mensagem 9 e faz `[C8D7] = [C8D8] = −1`.
 *          5. `CALL $91E0` com AR2 = 0xF3.
 *          6. Se `[E636] == 1`, envia a mensagem 3.
 *
 * @note Como no original, AR2 (cursor do pacote) não é salvo: vale 0xF8 para
 *       o `0x99DA` e, se a mensagem 3 for enviada, termina no rascunho
 *       `[0xB128]`. A leitura do pacote continua desse endereço.
 */
void handleType5(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra a leitura, os tratadores e os emissores traduzidos.
void registerMessageRoutines(SubroutineDispatcher& d);

} // namespace rea::core::link
