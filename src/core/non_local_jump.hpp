#pragma once
/**
 * @file non_local_jump.hpp
 * @brief Desvio não local (`BR` para fora da rotina corrente, descartando a pilha).
 */

#include <cstdint>

namespace rea::core {

/**
 * @brief Lançada por uma rotina traduzida que, no original, desvia para @c pc e abandona a pilha.
 *
 * @details Casos:
 * - `0x4B28` (reentrada do RESET) a partir do menu de serviço;
 * - `0x0669` (auditorias) a partir da INT0, pela sequência secreta (`0x4FAE`).
 *
 * Quem executa o código principal (a corrotina da CPU) a captura no nível mais externo e segue
 * de @c pc, como faria o hardware.
 */
struct NonLocalJump {
    uint32_t pc;  ///< Endereço da ROM onde a execução continua.
};

} // namespace rea::core
