#pragma once
/**
 * @file lzw_loader.hpp
 * @brief Descompressor LZW cooperativo de texturas (`0xA2B0`–`0xA3D6`).
 *
 * @details O jogo guarda as texturas comprimidas em LZW nas EPROMs gráficas
 * (janela `0xC80000`–`0xE7FFFF`) e as descomprime para a RAM de texturas
 * (`0xA00000`). Formato verificado nos 5 fluxos que o RESET carrega:
 *
 * | Código   | Significado |
 * |----------|-------------|
 * | 0x00–0xFF | Literal (byte) |
 * | `0x100`  | Fim do fluxo |
 * | `0x101`  | Aumenta a largura dos códigos em 1 bit (início: 9 bits) |
 * | `0x102`  | Limpa o dicionário; ponto onde a descompressão pode ceder a vez |
 * | ≥ `0x103` | Entrada do dicionário (prefixo, sufixo) |
 *
 * - **Bits:** os códigos são lidos do bit mais significativo para o menos
 *   significativo de cada palavra de 32 bits, sem alinhamento.
 * - **Bytes de saída:** são empacotados 4 por palavra, o primeiro no LSB.
 *   Juntam-se 64 palavras num buffer (`0xC8EE`) que é copiado em meias
 *   palavras para a RAM de texturas (2 texels por escrita).
 * - **Dicionário:** prefixos em `[0x1976D + código]` e sufixos em
 *   `[0x1976D + 0x1145 + código]`. A pilha de inversão fica em `0x1B9F7`.
 *
 * **Modo cooperativo.** Com `[E5CB] = 0`, `0xA2F9` só prepara o contexto. Depois
 * o loop principal chama `0xA323` a cada iteração, e ela descomprime até ter
 * escrito ≥ 0x800 palavras (8 KB). No próximo `0x102` salva os registradores
 * em `0xE5CF`–`0xE5E3` e devolve o controle. Com `[E5CB] = 1` (usado no RESET),
 * a descompressão vai até o fim e chama `0xA3A5` (animação de "carregando")
 * a cada fatia.
 *
 * **Variáveis:**
 * - `[E5CA]` = descompressão ativa.
 * - `[E5CB]` = modo bloqueante.
 * - `[E5CD]` = palavras escritas na fatia atual.
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::gfx {

/// Variáveis RAM do descompressor.
namespace lzw_vars {
inline constexpr uint32_t kActive        = 0xE5CA; ///< != 0: há descompressão em andamento.
inline constexpr uint32_t kBlocking      = 0xE5CB; ///< != 0: `0xA2F9` descomprime tudo de uma vez.
inline constexpr uint32_t kE5CC          = 0xE5CC; ///< Zerada no início; uso ainda desconhecido.
inline constexpr uint32_t kSliceWords    = 0xE5CD; ///< Palavras de saída na fatia atual (+64 por flush).
inline constexpr uint32_t kMaxCode       = 0xE5CE; ///< Recebe 0x1FF a cada limpeza; não é lida aqui.
inline constexpr uint32_t kSavedAr0      = 0xE5CF; ///< AR0 (ponteiro de entrada) salvo.
inline constexpr uint32_t kContextPtr    = 0xA373; ///< `[A373]` = 0xE5D0: AR1–AR7, R1–R7, RC, RE, RS, IR0, IR1, BK.
inline constexpr uint32_t kDictPtr       = 0xA2AD; ///< `[A2AD]` = 0x1976D: base do dicionário.
inline constexpr uint32_t kStackPtr      = 0xA2AE; ///< `[A2AE]` = 0x1B9F7: pilha de inversão.
inline constexpr uint32_t kBufferPtr     = 0xA2AF; ///< `[A2AF]` = 0xC8EE: buffer de 64 palavras.
inline constexpr uint32_t kProgressFlag  = 0xA3A4; ///< != 0: chama `0xA3A5` (animação) no modo bloqueante.
inline constexpr uint32_t kQueueProcPtr  = 0xB12A; ///< `[B12A]` = 0xA3D7: processo que espera e enfileira.
inline constexpr uint32_t kSavedIeMask   = 0x8099DF; ///< Máscara IE usada durante a cópia do buffer.
} // namespace lzw_vars

/// Constantes do formato.
namespace lzw {
inline constexpr uint32_t kEnd         = 0x100;  ///< Fim do fluxo.
inline constexpr uint32_t kGrow        = 0x101;  ///< Largura do código + 1.
inline constexpr uint32_t kClear       = 0x102;  ///< Limpa o dicionário (ponto de pausa).
inline constexpr uint32_t kFirstCode   = 0x103;  ///< Primeira entrada livre do dicionário.
inline constexpr uint32_t kInitialBits = 9;      ///< Largura inicial.
inline constexpr uint32_t kSuffixOffset = 0x1145; ///< IR0: distância prefixos → sufixos.
inline constexpr uint32_t kBufferWords = 0x40;   ///< Palavras por flush.
inline constexpr uint32_t kSliceLimit  = 0x800;  ///< Palavras por fatia antes de ceder a vez.
} // namespace lzw

/**
 * @brief `0xA2B0`: lê o próximo código de AR6 bits.
 * @details Entrada: AR0 = palavra atual, AR7 = bits já consumidos nela,
 *          AR6 = largura. Saída: R0 = código, AR7 e AR0 avançados.
 *          Se o código atravessa a palavra (AR7 + AR6 > 31), junta o fim de
 *          `*AR0` com o início de `*(AR0+1)`. Com soma exatamente 32 o
 *          `LSH` por −32 do TMS dá 0, o que mantém o resultado correto.
 *          Altera R1 e R2.
 */
void readCode(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0xA2C9`: grava a palavra montada (IR1) no buffer e, a cada 64, copia.
 * @details `buffer[R7++] = IR1`; IR1 = R3 = 0. Quando R7 chega a 64:
 *          - `[E5CD] += 64`;
 *          - wait states `0x808064 = 0x1000`;
 *          - copia cada palavra como duas escritas de 16 bits em `*AR1++`
 *            (primeiro a metade baixa, depois `>> 16`), totalizando 128 escritas;
 *          - volta os wait states para `0x470`.
 *
 *          Durante a cópia o IE fica com a máscara de `[0x8099DF]`.
 *          Termina com `BUD $4C33` (`RETIU`), portanto **liga o GIE**.
 */
void flushOutput(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0xA374`: salva o contexto do descompressor (`0xE5CF`–`0xE5E3`).
 * @details `[E5CF] = AR0` e depois `AR1…AR7, R1…R7, RC, RE, RS, IR0, IR1, BK`
 *          a partir de `[A373]`. Saída: DP = 0, AR0 = 0xE5E4.
 */
void saveContext(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0xA38C`: restaura o contexto salvo por `0xA374`. Saída: DP = 0.
 */
void restoreContext(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0xA2F9`: inicia uma descompressão (AR4 = origem, AR5 = destino).
 * @details Salva AR0–AR7/R0–R7 (`0x9042`), espera o DMA e chama `0x4EE3`.
 *          Depois faz `[E5CA] = 1`, `[E5CC] = 0` e monta o contexto inicial:
 *          - AR0 = AR4, AR1 = AR5;
 *          - R7 = AR7 = IR1 = R3 = 0;
 *          - AR3 = dicionário, IR0 = 0x1145.
 *
 *          Salva esse contexto. No modo bloqueante, chama `0xA323` até o fim.
 *          Restaura os registradores (`0x905C`).
 * @note Vazam para o chamador DP, IR0, IR1, RC, RS, RE e BK, como no original.
 */
void startDecompression(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0xA323`: continua a descompressão em andamento (uma fatia).
 * @details Chamada pelo loop principal (`0x4BCC`) a cada iteração. Retorna
 *          na hora se `[E5CA] = 0`. No fim do fluxo zera `[E5CA]` e `[E5CB]`.
 */
void resumeDecompression(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0xA3C2`: pede o carregamento descrito por `{origem, destino}` em AR2.
 * @details Se já há uma descompressão ativa, cria um processo (`0xA844`,
 *          entrada `[B12A] = 0xA3D7`, AR4 = descritor). Esse processo espera
 *          o fim da descompressão atual e então chama `0xA3C2` de novo. Senão,
 *          lê o descritor (AR2 avança 2) e chama `0xA2F9`. Preserva AR4/AR5.
 */
void loadAsset(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief Registra as rotinas deste módulo no despachante.
void registerRoutines(SubroutineDispatcher& d);

} // namespace rea::core::gfx
