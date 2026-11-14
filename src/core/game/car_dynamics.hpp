#pragma once
/**
 * @file car_dynamics.hpp
 * @brief Dinâmica de direção do carro (`0x2C18`): marcha, velocidade, tração, rotação do
 *        motor, esterço/rodopio, batida no muro e integração da posição.
 *
 * @details AR4 = objeto, AR5 = dados D do carro (`[obj+0x1B]`). Campos de D usados:
 * - `+0x21` esterço aplicado, `+0x22` fator de tração, `+0x23` potência, `+0x24` acelerador;
 * - `+0x25` derrapagem (0..1), `+0x26` velocidade, `+0x27` velocidade efetiva;
 * - `+0x2B` freio, `+0x2C` rumo do carro, `+0x2D` rumo desejado, `+0x2E` giro por quadro;
 * - `+0x31` rodopio (0 = não; 1 = gira até `[D+0x32]`; ≥ 2 = contador), `+0x32` ângulo a girar;
 * - `+0x38` marcha (1..4), `+0x39` rotação do motor, `+0x44`/`+0x45` arrasto no asfalto/fora.
 *
 * **`0x2C18`** (R2 = esterço pedido):
 * 1. marcha `0x2E73` → `[D+0x38]`;
 * 2. velocidade `0x2D83`, derrapagem `0x2C9C`, rotação `0x2C92`;
 * 3. esterço `0x2CEB` (devolve o giro) e batida no muro `0x2F6B`;
 * 4. matriz do objeto = rotY(giro) × matriz; `[0x5B]` = rotY(`[D+0x2D]` − `[D+0x2C]`) × matriz;
 * 5. no chão (`[D+0x20]` = 0): velocidade `[obj+0x11..0x13]` = `[0x5B]`·(0, 0, `[D+0x27]`);
 * 6. posição += velocidade (`0x9081`).
 *
 * **`0x2D83`**: força = acelerador × potência × curva de torque `[2D68]` (interpolada pela rotação/3)
 * × marcha `[2D62]` × (1 − derrapagem/4) + inclinação·2. Arrasto = média do tipo de chão dos 5
 * pontos × 0,2 + derrapagem·`[2916]` + freio·`[2915]` + marcha `[2D7D]`. Integra
 * v ← v − arrasto·v + força por quadro (`[C92F]`, até 6).
 *
 * **`0x2F6B`**: com algum ponto 1..4 fora do chão e velocidade < 0x7D00·2, toca o som da batida,
 * empurra o carro de volta para a pista (perpendicular ao rumo da pista) e alinha o rumo, ou
 * inicia um rodopio.
 *
 * **`0x2F63`** (só o carro do jogador): alinhamento automático `0x2F43` e bordas — ponto sem chão
 * → impacto `0x2F75`; ponto fora da estrada → `0x2FE8`, que puxa o rumo de volta para a pista
 * e, com desvio ≥ 0,6, reaproveita a cauda da batida em `0x2FB0`.
 */

#include "core/subroutines.hpp"

namespace rea::core::game {

/// @brief Registra `0x2C18`, `0x2C92`, `0x2C9C`, `0x2CEB`, `0x2D83`, `0x2E73`, `0x2F2C`, `0x2F43`, `0x2F63`, `0x2F6B`,
///        `0x2F75`, `0x2FE8`,
///        `0x3031`, `0x3035`, `0x3037`, `0x303A`, `0x3159`, `0x3168`, `0x9081`, `0x908B` e `0x9099`.
void registerCarDynamics(SubroutineDispatcher& d);

} // namespace rea::core::game
