#pragma once
/**
 * @file boot_sequence.hpp
 * @brief RESET + telas de recuperação da CMOS (`0x4FEF`, `0x5006`) até o loop principal.
 */

#include <cstdint>
#include <string>
#include <vector>

#include "core/memory_bus.hpp"
#include "core/subroutines.hpp"
#include "core/tms32031_state.hpp"

namespace rea::core::boot {

/**
 * @brief Mensagem de recuperação exibida pela ROM (via `0xA7F4`, ainda pendente).
 * @details `i18n_key` aponta para `assets/lang/<idioma>.lang`; `rom_text` é o texto
 * original lido da ROM (inglês), mantido para conferência.
 */
struct BootMessage {
    const char* i18n_key;  ///< Chave de tradução.
    uint32_t rom_ptr;      ///< Palavra RAM com o ponteiro da string (ex.: 0xB0A6).
    uint32_t x;            ///< Coluna passada em R2.
    uint32_t y;            ///< Linha passada em R3.
};

/// Telas de recuperação: duas linhas cada.
inline constexpr BootMessage kCmosErrorScreen[2] = {
    {"BOOT_CMOS_ERROR", 0xB0A6, 0xD8, 0xBE},                 // "CMOS ERROR"
    {"BOOT_FACTORY_ADJUSTMENTS_RESTORED", 0xB0A7, 0x90, 0xC8} // "FACTORY ADJUSTMENTS RESTORED"
};
/// Mensagens da tela de versão atualizada (duas linhas).
inline constexpr BootMessage kVersionUpdatedScreen[2] = {
    {"BOOT_VERSION_UPDATED", 0xB0A8, 0xC4, 0xBE},            // "VERSION UPDATED"
    {"BOOT_FACTORY_SETTINGS_RESTORED", 0xB0A9, 0x9C, 0xC8}   // "FACTORY SETTINGS RESTORED"
};

/// Assinatura de versão gravada no item 0xFA da CMOS.
inline constexpr uint32_t kVersionItem = 0xFA;
/// Valor da assinatura de versão esperado no item 0xFA.
inline constexpr uint32_t kVersionSignature = 0x28;

/**
 * @brief Sequência de partida: RESET → (recuperação → reentrada 0x4B28)* → `0x4BAF`.
 *
 * @details
 * - `0x5006` ("VERSION UPDATED"): a CMOS não tem a assinatura 0x28 no item
 *   0xFA (placa nova ou ROM trocada). Mostra a tela, chama `0x5020` e `0x0DCE`
 *   (restaura os ajustes de fábrica, pendente), grava 0xFA = 0x28 e reentra.
 * - `0x4FEF` ("CMOS ERROR"): mesmo esquema com `0x0CC0`; só é alcançado se
 *   `0x995C` devolver carry, o que esta versão nunca faz (ver cmos::validateAll).
 *
 * Cada tela: 9 × (`0x8E79`, `0x8E4E`, `[CE13] = 0x900000`,
 * 101 × (duas linhas via `0xA7F4`), `0x5029`). No hardware isso dura alguns
 * segundos; aqui roda de uma vez (as interrupções não são atendidas no meio).
 */
class BootSequence {
public:
    /// Limite de reentradas (o original repetiria indefinidamente se a CMOS não gravasse).
    static constexpr int kMaxPasses = 3;

    /**
     * @param bus Barramento (ROM carregada).
     * @param subs Despacho de sub-rotinas.
     */
    BootSequence(MemoryBus& bus, ISubroutines& subs) : bus_(bus), subs_(subs) {}

    /**
     * @brief Executa a partida completa a partir do vetor de RESET.
     * @param cpu Registradores.
     * @param hardware_reset true = também simula o pino RESET (cópia ROM→RAM).
     * @return 0x4BAF se chegou ao loop principal, ou o último destino se esgotou kMaxPasses.
     */
    uint32_t run(Tms32031State& cpu, bool hardware_reset = true);

    /**
     * @brief Reentrada em `0x4B28` (desvio do menu de serviço, "EXIT TO GAME OVER", ou do fim
     *        das auditorias `0x0669`): mesmo caminho do RESET sem o pino de hardware nem a `0x6381`.
     * @return Como run().
     */
    uint32_t reenter(Tms32031State& cpu);

    /// @return Destinos de cada passagem pelo RESET (ex.: {0x5006, 0x4BAF}).
    const std::vector<uint32_t>& exits() const { return exits_; }
    /// @return Chaves i18n das telas exibidas, em ordem.
    const std::vector<std::string>& messagesShown() const { return messages_; }

private:
    /// Passagens pelas telas de recuperação a partir do destino @p next do RESET.
    uint32_t recover(Tms32031State& cpu, uint32_t next);
    /// Laço comum das telas (`0x4FEF`–`0x5003` / `0x5006`–`0x501A`).
    void showScreen(Tms32031State& cpu, const BootMessage (&lines)[2]);

    MemoryBus& bus_;  ///< Barramento de memória simulado.
    ISubroutines& subs_;  ///< Despachante das sub-rotinas traduzidas.
    std::vector<uint32_t> exits_;  ///< Destinos de cada passagem pelas telas de recuperação.
    std::vector<std::string> messages_;  ///< Chaves das mensagens mostradas, em ordem.
};

} // namespace rea::core::boot
