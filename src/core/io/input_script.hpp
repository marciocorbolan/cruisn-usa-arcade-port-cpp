#pragma once
/**
 * @file input_script.hpp
 * @brief Entrada roteirizada: sobrepõe ações em faixas de quadros à entrada real (testes sem tela).
 *
 * @details Formato (`--press`): itens separados por vírgula, `ação@início-fim[:valor]`, com quadros
 * contados a cada `poll()`. Ações: `coin1`..`coin4`, `start`, `service`, `view1`..`view3`, `radio`,
 * `test`, `volup`, `voldown` (botões), `gas`, `brake`, `wheel` (0..255, padrão 255) e `gear` (1..4).
 * Exemplo: `coin1@1200-1205,start@1300-1305,gas@1400-99999:255`.
 */

#include <cstdint>
#include <string>
#include <vector>

#include "core/interfaces.hpp"

namespace rea::core::io {

/// @brief Uma ação aplicada nos quadros [first, last].
struct ScriptedAction {
    std::string name;  ///< Nome da ação (`coin1`, `gas`, `wheel`…).
    uint64_t first = 0;  ///< Primeiro quadro em que a ação vale.
    uint64_t last = 0;  ///< Último quadro em que a ação vale.
    uint32_t value = 255;  ///< Valor dos analógicos (0–255) ou marcha (1–4).
};

/**
 * @brief Decorador de IInput que aplica um roteiro de ações por quadro.
 */
class ScriptedInput : public IInput {
public:
    /**
     * @param base Entrada real (continua valendo fora das ações do roteiro).
     * @param spec Roteiro no formato descrito no arquivo.
     */
    ScriptedInput(IInput& base, const std::string& spec);

    /// @brief Lê a entrada real e sobrepõe as ações ativas neste quadro.
    ArcadeInputState poll() override;

    /// @return Ações reconhecidas (itens inválidos são ignorados).
    const std::vector<ScriptedAction>& actions() const { return actions_; }

    /// @brief Interpreta um roteiro (função pura, usada também pelos testes).
    static std::vector<ScriptedAction> parse(const std::string& spec);

    /// @brief Primeiro item inválido de @p spec (sem `@`, faixa invertida ou ação desconhecida).
    /// @return O item, ou vazio se o roteiro todo é válido.
    static std::string firstInvalid(const std::string& spec);

    /// @return true se @p name é uma ação conhecida (`coin1`, `gas`, ...).
    static bool knownAction(const std::string& name);

    /// @brief Aplica as ações ativas no quadro @p frame sobre @p s.
    static void apply(const std::vector<ScriptedAction>& actions, uint64_t frame, ArcadeInputState& s);

private:
    IInput& base_;  ///< Entrada real.
    std::vector<ScriptedAction> actions_;  ///< Roteiro interpretado.
    uint64_t frame_ = 0;  ///< Quadros lidos até agora.
};

} // namespace rea::core::io
