/**
 * @file service_calibration.cpp
 * @brief CALIBRATE CONTROLS (`0x0CC0`–`0x0D1E`): telas que pedem cada posição do volante e dos pedais e
 *        gravam as leituras filtradas pela INT3 nos itens 1–7 da CMOS.
 *
 * @details Cada tela limpa as páginas, escreve duas (ou três) linhas de instrução em cor 0xF, espera a
 * chave de teste soltar (`0x05FA`) e apertar (`0x05F6`), toca o bipe (`0x0600`) e grava:
 * | tela | textos | itens gravados |
 * |---|---|---|
 * | 1 | `[C855]` (0x96,0x0A), `[C856]`, `[C857]` | 1 = acelerador `[C93B]`, 6 = freio `[C93C]`, 5 = volante `[C8C8]` (repouso) |
 * | 2 | `[C858]`, `[C859]` | 3 = volante |
 * | 3 | `[C85A]`, `[C85B]` | 4 = volante |
 * | 4 | `[C85C]`, `[C85D]` | 2 = acelerador |
 * | 5 (só com a DIP `[C8CC]` & 0x20 = 0) | `[C85E]`, `[C85F]` | 7 = freio; espera a chave soltar |
 */
#include "core/service/service_internal.hpp"

namespace rea::core::service {

namespace {

constexpr uint32_t kGas = 0xC93B;    ///< acelerador filtrado (INT3)
constexpr uint32_t kBrake = 0xC93C;  ///< freio filtrado
constexpr uint32_t kWheel = 0xC8C8;  ///< volante filtrado

/// Título em (0x96, 0x0A) e instrução em (0x32, 0x28); `0x0CC6`–`0x0CC9` acrescenta (0x32, 0x32).
void instructions(Ctx& c, uint32_t title, uint32_t line, uint32_t extra) {
    c.cpu.rc = 0xF;
    c.text(0x96, 0x0A, c.rd(title));
    c.text(0x32, 0x28, c.rd(line));
    if (extra) c.text(0x32, 0x32, c.rd(extra));
}

/// `0x05FA`, `0x05F6`, `0x0600`: chave de teste solta, apertada, bipe.
void confirm(Ctx& c) {
    c.call(0x05FA);
    c.call(0x05F6);
    c.call(0x0600);
}

/// `0x99EE` com R2 = `[var]` e AR2 = item.
void store(Ctx& c, uint32_t var, uint32_t item) {
    c.cpu.r[2] = c.rd(var);
    c.cpu.ar[2] = item;
    c.call(0x99EE);
}

/// @brief `0x0CC0`: calibração dos controles.
void calibrate(Ctx& c) {
    c.call(0x0AE9);                         // 00CC0  limpa a tela
    instructions(c, 0xC855, 0xC856, 0xC857);
    confirm(c);
    store(c, kGas, 1);                      // 00CD1  repouso: acelerador, freio e volante
    store(c, kBrake, 6);
    store(c, kWheel, 5);
    c.call(0x8E79);                         // 00CDA
    instructions(c, 0xC858, 0xC859, 0);
    confirm(c);
    store(c, kWheel, 3);
    c.call(0x8E79);                         // 00CEA
    instructions(c, 0xC85A, 0xC85B, 0);
    confirm(c);
    store(c, kWheel, 4);
    c.call(0x8E79);                         // 00CFA
    instructions(c, 0xC85C, 0xC85D, 0);
    confirm(c);
    store(c, kGas, 2);
    if (c.rd(vars::kDips) & 0x20u) return;  // 00D0A  TSTB $20,[C8CC]; BNE → RETSU
    c.call(0x8E79);                         // 00D0D
    instructions(c, 0xC85E, 0xC85F, 0);
    confirm(c);
    store(c, kBrake, 7);
    c.call(0x05FA);                         // 00D1D
}

} // namespace

/// Registra `0x0CC0` (CALIBRATE CONTROLS).
void registerCalibration(SubroutineDispatcher& d, IBusyWait& wait) {
    addSvc(d, wait, 0x0CC0, calibrate);
}

} // namespace rea::core::service
