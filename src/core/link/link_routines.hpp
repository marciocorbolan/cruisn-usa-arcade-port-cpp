#pragma once
/**
 * @file link_routines.hpp
 * @brief Troca de pacotes entre gabinetes ligados (`0x7F5D`–`0x81D8`).
 *
 * @details Chamada no início do corpo do loop principal (`0x4BD6`). Só age com
 * a DIP "Linking" (SW2:6) ligada (`[C8CC] & 4 == 0`); de fábrica vem desligada
 * e a rotina retorna na hora. Ligada, espera a cadeia de INT3 terminar
 * (`[C93D] == 3`) e executa um lado do protocolo:
 *
 * **Master (SW2:8 = 0):**
 * 1. **Handshake** com timeout em quadros: o Slave sobe 0x100; o Master sobe
 *    o strobe 0x400 e espera 0x100 baixar.
 * 2. **Desliga o GIE.** Espera o Slave subir 0x200, sem timeout.
 * 3. **Envia o pacote** de `[C8D3]` bytes de `[0x7938]` = 0x1F000, um byte por
 *    palavra, com COMDOE ligado:
 *    - cabeçalho `n, ~n` (n = pares de bytes, alto primeiro);
 *    - dados, 2 bytes por par;
 *    - checksum de 16 bits.
 *    Cada byte vai nas linhas 0–7, alternando o strobe (`0xC400`/`0xC000`), com
 *    atrasos `RPTS` e sem confirmação por byte.
 * 4. **Inverte o sentido:** espera 0x200 baixar (≤ 31 leituras) e sobe 0x400.
 * 5. **Recebe** o pacote do Slave em `[0x7F16]` = 0x1F258, com cada byte
 *    em `bits 16–23` de uma palavra. Cada borda tem timeout de 300 leituras.
 *    Confere o cabeçalho e o checksum. `[C8D4]` = pares − 1.
 * 6. **Contadores:** `[0x809A17]` +1 em sucesso, `[0x809A16]` +1 em erro
 *    (com `[C8D4] = 0`). Termina com `RETIU` (`CALL $4C33`), que religa o GIE.
 *
 * **Slave (SW2:8 = 1):** espelho do Master, com as linhas 0x200/0x100 e o
 * strobe 0x200 (`0x3200`/`0x3000`). Recebe primeiro e depois envia. Contadores
 * `[0x809A19]` (sucesso) e `[0x809A18]` (erro).
 *
 * **Variáveis:**
 * - `[C8D0]` != 0 pula a troca.
 * - `[C8D1]` = conectado.
 * - `[C8D2]` = a última tentativa estourou o tempo. Os timeouts em quadros
 *   caem de 120 para `[C930] + 1` (máx. 4).
 * - `[E591]` = 1 durante a troca.
 *
 * **No port:** as esperas chamam `IBusyWait::spin()` com uma estimativa das
 * instruções gastas. É assim que a INT0 avança `[C930]` nos timeouts em
 * quadros e que o watchdog de hardware age na espera sem timeout com GIE
 * desligado.
 */

#include <cstdint>

#include "core/busy_wait.hpp"
#include "core/subroutines.hpp"

namespace rea::core::link {

/// Variáveis RAM do link.
namespace link_vars {
inline constexpr uint32_t kDipShadow    = 0xC8CC; ///< Cópia das DIPs: bit 0 Slave, bit 2 Linking desligado.
inline constexpr uint32_t kSkip         = 0xC8D0; ///< != 0: não troca pacotes.
inline constexpr uint32_t kConnected    = 0xC8D1; ///< 1 após o handshake.
inline constexpr uint32_t kTimedOut     = 0xC8D2; ///< 1 se a última tentativa estourou o tempo.
inline constexpr uint32_t kTxLength     = 0xC8D3; ///< Bytes a enviar (Master: 1…0x23F).
inline constexpr uint32_t kRxPairs      = 0xC8D4; ///< Pares recebidos − 1; 0 em erro.
inline constexpr uint32_t kFrameCounter = 0xC930; ///< Quadros (INT0) — base dos timeouts.
inline constexpr uint32_t kAdcState     = 0xC93D; ///< 3 = cadeia de INT3 terminada.
inline constexpr uint32_t kBusy         = 0xE591; ///< 1 durante a troca.
inline constexpr uint32_t kRxBufferPtr  = 0x7F16; ///< `[0x7F16]` = 0x1F258.
inline constexpr uint32_t kPortPtr      = 0x7F17; ///< `[0x7F17]` = 0x997000.
inline constexpr uint32_t kTxBufferPtr  = 0x7938; ///< `[0x7938]` = 0x1F000.
inline constexpr uint32_t kMasterErrors = 0x809A16;  ///< Trocas com erro no Master.
inline constexpr uint32_t kMasterOk     = 0x809A17;  ///< Trocas sem erro no Master.
inline constexpr uint32_t kSlaveErrors  = 0x809A18;  ///< Trocas com erro no Slave.
inline constexpr uint32_t kSlaveOk      = 0x809A19;  ///< Trocas sem erro no Slave.
} // namespace link_vars

/**
 * @brief `0x7F25`: chamada a cada quadro. Fora de uma troca (`[E591]` = 0) e no Master,
 *        desliga o GIE e o INT2 e pulsa a porta: `(0xC800 | b) << 16` e `(0xC000 | b) << 16`,
 *        com `b` = bit 0x400 da metade alta lida; limpa o IF do INT2.
 * @note O GIE fica desligado na saída, como no original.
 */
void linkIdle(Tms32031State& cpu, MemoryBus& bus);

/// @brief Troca de pacotes pelo link (`0x7F5D`), com o relógio das esperas ocupadas injetado.
class LinkRoutines {
public:
    /// @param wait Relógio das esperas (ex.: FrameBusyWait; nos testes, uma co-simulação).
    explicit LinkRoutines(IBusyWait& wait) : wait_(wait) {}

    /**
     * @brief `0x7F5D`: troca de pacotes do quadro (Master ou Slave conforme a DIP).
     * @details Sem efeito com a DIP Linking desligada (padrão de fábrica).
     *          Retorna na hora se o relógio pedir RESET.
     */
    void exchange(Tms32031State& cpu, MemoryBus& bus);

    /// @brief Registra `0x7F5D` no despachante (a instância precisa viver mais que ele).
    void registerIn(SubroutineDispatcher& d);

private:
    struct Abort {};  ///< Lançada quando spin() pede RESET; capturada em exchange().

    /// `0x7F6F`: lado Master da troca (envia primeiro, depois recebe).
    void master();
    /// `0x80C1`: lado Slave da troca (recebe primeiro, depois responde).
    void slave();
    /// `0x7FB6` / `0x8168`: envia cabeçalho, dados e checksum. @p hi / @p lo = strobe alto/baixo.
    void sendPacket(uint32_t hi, uint32_t lo);
    /// `0x800D` / `0x80F7`: recebe o pacote. @return false em erro (timeout, cabeçalho, checksum).
    bool receivePacket(bool master_side);
    /// Laço `TSTB src,R5 / DBcond AR3`. @return true se AR3 estourou (`TSTB -8000h,AR3`).
    bool pollTimedOut(uint32_t addr, bool wait_while_set);
    /// `PUSH IE; IE = 0; NOP; NOP; AND 0xDFFF,ST; POP IE`: desliga o GIE.
    void disableInterrupts();
    void masterFinish();  ///< `0x806F`
    void masterFail();    ///< `0x8088`
    void slaveFinish();   ///< `0x81B9`
    void slaveFail();     ///< `0x81D1`
    /// Soma 1 à palavra em @p addr (contadores de erro/sucesso).
    void increment(uint32_t addr);

    /// Lê a palavra no endereço @p addr.
    uint32_t rd(uint32_t addr) { return bus_->readWord(addr); }
    /// Grava a palavra @p v no endereço @p addr.
    void wr(uint32_t addr, uint32_t v) { bus_->writeWord(addr, v); }
    /// @brief Passa tempo; lança Abort se o relógio pedir RESET.
    void spin(uint32_t instructions) {
        if (!wait_.spin(*cpu_, instructions)) throw Abort{};
    }

    IBusyWait& wait_;  ///< Espera ativa usada nos laços de polling do hardware.
    Tms32031State* cpu_ = nullptr;  ///< CPU da troca em andamento.
    MemoryBus* bus_ = nullptr;  ///< Barramento da troca em andamento.
};

} // namespace rea::core::link
