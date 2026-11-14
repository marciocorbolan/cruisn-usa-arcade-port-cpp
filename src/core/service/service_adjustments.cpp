/**
 * @file service_adjustments.cpp
 * @brief Ajustes do menu de serviço (`0x0B9B`, `0x0DFB`–`0x0F59`), preços padrão e personalizados
 *        (`0x0BDC`–`0x0C9D`, `0x0FC8`–`0x1054`, `0x73B0`, `0x73D6`) e utilidades (`0x0DC9`–`0x0E60`).
 */
#include <array>

#include "core/service/service_internal.hpp"
#include "core/tms_float.hpp"

namespace rea::core::service {

namespace {

/// `0x9042`/`0x905C`: guarda e restaura R0–R7 e AR0–AR7.
struct SavedRegs {
    /// Guarda R0–R7 e AR0–AR7 de @p cpu.
    explicit SavedRegs(Tms32031State& cpu) : cpu_(cpu), r_(cpu.r), ar_(cpu.ar) {}
    /// Restaura os registradores guardados.
    void restore() {
        cpu_.r = r_;
        cpu_.ar = ar_;
    }
    Tms32031State& cpu_;  ///< CPU cujos registradores foram guardados.
    std::array<uint32_t, 8> r_;   ///< R0–R7 guardados.
    std::array<uint32_t, 8> ar_;  ///< AR0–AR7 guardados.
};

/// Itens de sim/não e de duas opções: R5 = item; caixa `0x0E7A` (ou `0x0E7D` com AR4/AR5) e grava R5.
struct Toggle {
    uint32_t addr;    ///< Endereço da rotina na ROM.
    uint32_t item;    ///< Item da CMOS (R5).
    uint32_t line1;   ///< Ponteiro da 1ª linha do título.
    uint32_t line2;   ///< Ponteiro da 2ª linha do título.
    uint32_t first;   ///< Texto da 1ª opção; 0 = `[0E50]` (caixa `0x0E7A`).
    uint32_t second;  ///< Texto da 2ª opção; 0 = `[0E51]`.
};

/// Itens de sim/não do menu de ajustes.
constexpr Toggle kToggles[] = {
    {0x0B9B, 0x1E, 0x0B9A, 0x0E52, 0, 0},       {0x0DFB, 0x19, 0x0DFA, 0x0E52, 0, 0},
    {0x0E07, 0x1A, 0x0E05, 0x0E06, 0, 0},       {0x0E13, 0x1F, 0x0E11, 0x0E12, 0, 0},
    {0x0E1E, 0x25, 0x0E1D, 0x0E52, 0, 0},       {0x0E29, 0x24, 0x0E28, 0x0E52, 0, 0},
    {0x0E34, 0x23, 0x0E33, 0x0E52, 0, 0},       {0x0E41, 0x22, 0x0E3E, 0x0E52, 0x0E3F, 0x0E40},
};

/// Ajustes numéricos: R5 = item; caixa `0x0F5B` (R6..R7, passo R4, formatador AR4) e grava R5.
struct Numeric {
    uint32_t addr;       ///< Endereço da rotina na ROM.
    uint32_t item;       ///< Item da CMOS (R5).
    uint32_t line1;      ///< Ponteiro da 1ª linha do título.
    uint32_t line2;      ///< Ponteiro da 2ª linha do título.
    uint32_t low;        ///< Valor mínimo (R6).
    uint32_t high;       ///< Valor máximo (R7).
    uint32_t step;       ///< Passo (R4); 0 = caixa `0x0F5A` (passo 1).
    uint32_t formatter;  ///< Rotina que formata o valor (AR4).
};

/// Ajustes numéricos do menu.
constexpr Numeric kNumerics[] = {
    {0x0ECB, 0x26, 0x0EC9, 0x0ECA, 0, 5, 1, 0x0EF9},
    {0x0EDB, 0x27, 0x0ED9, 0x0EDA, 1000, 25000, 1000, 0x0EF9},
    {0x0EEB, 0x1B, 0x0EE9, 0x0EEA, 0, 9, 1, 0x0EF9},
    {0x0F03, 0x1C, 0x0F01, 0x0F02, 0, 6, 0, 0x0F10},
    {0x0F1C, 0x1D, 0x0F1A, 0x0F1B, 10, 25, 0, 0x0F29},
    {0x0F33, 0x2D, 0x0F31, 0x0F32, 10, 50, 0, 0x0F29},
};

/// Utilidades: confirmação (`0x0E76`), rotinas, mensagem (`0x0E62`) e, se pedido, a calibração.
struct Utility {
    uint32_t addr;      ///< Endereço da rotina na ROM.
    uint32_t question;  ///< Ponteiro do texto da pergunta.
    uint32_t answers;   ///< Ponteiro do texto das respostas.
    std::array<uint32_t, 3> routines;  ///< 0 = nenhuma
    bool clearCoinItems;               ///< itens 0x70/0x71 = 0 (0x99F4)
    uint32_t message;  ///< Mensagem mostrada no fim (`0x0E62`).
    bool calibrate;                    ///< CALL $0CC0 no fim
};

/// Utilidades do menu (restaurar fábrica, ajustes padrão, recordes, auditorias, créditos).
const Utility kUtilities[] = {
    {0x0DC9, 0xC873, 0x0E52, {0x9972, 0x9A99, 0x99A3}, true, 0x0DDA, true},  // FULL FACTORY RESTORE
    {0x0DDB, 0xC87B, 0x0E52, {0x99A3, 0, 0}, false, 0x0DE5, true},           // DEFAULT ADJUSTMENTS
    {0x0DE6, 0xC881, 0x0E52, {0x9A99, 0, 0}, false, 0x0DEF, false},          // RESET HIGH SCORES
    {0x0DF0, 0xC887, 0x0E52, {0x9972, 0, 0}, false, 0x0DF9, false},          // CLEAR GAME AUDITS
    {0x0E54, 0x0E4D, 0x0E53, {0, 0, 0}, true, 0x0E61, false},                // CLEAR CREDITS
};

/// `0x0E54`/`0x0DC9`: texto da pergunta. Em 0x0E54 o ponteiro vem de `[0E4D]`; nas demais de `[C8xx]`.
void utility(Ctx& c, const Utility& u) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[2] = c.rd(u.question);
    cpu.ar[3] = c.rd(u.answers);
    c.call(0x0E76);
    if (cpu.carry()) return;  // RETSLO: "NO"
    cpu.dp = 0;
    for (uint32_t r : u.routines) {
        if (r) c.call(r);
    }
    if (u.clearCoinItems) {
        cpu.r[2] = 0;
        cpu.ar[2] = 0x70;
        c.call(0x99F4);
        cpu.ar[2] = 0x71;
        c.call(0x99F4);
    }
    cpu.ar[2] = c.rd(u.message);
    c.call(0x0E62);
    if (u.calibrate) c.call(0x0CC0);
}

/// Formatadores dos ajustes numéricos: texto de R5 em `[0723]`; AR2 = texto; preservam R2.
void formatValue(Ctx& c, uint32_t value) {
    const uint32_t r2 = c.cpu.r[2];
    c.number(value);
    c.cpu.ar[2] = c.rd(vars::kNumberBuf);
    c.cpu.r[2] = r2;
}

/// `0x0BF5`: detalhes do preço sob o cursor (R0 = R6), redesenhados quando ele muda (`[EBE0]`).
void pricingDetails(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    if (cpu.r[0] != cpu.r[6]) return;
    if (cpu.r[0] == c.rd(0xEBE0)) return;
    c.wr(0xEBE0, cpu.r[0]);
    SavedRegs saved(cpu);
    const uint32_t rc = cpu.rc;
    c.shape(0xA7E5, 0x10E, 0xDC, 0x1F2, 0xE5, 0);
    c.shape(0xA7E5, 0x1D2, 0x64, 0x1F2, 0xF9, 0);
    c.shape(0xA7E5, 0x10E, 0x3C, 0x1F2, 0x5A, 0);
    const uint32_t record = tmsMpyi(cpu.r[0] + c.rd(0xEBDF), 0xB) + c.rd(0xB080);
    if (Ctx::s32(cpu.ar[7]) > 0) {
        // 00C10  nome do preço (até 3 linhas) na cor 3
        cpu.rc = 3;
        cpu.ar[4] = record + 3;
        c.text(0x10E, 0x3C, c.rd(cpu.ar[4]++));
        for (uint16_t y : {uint16_t{0x46}, uint16_t{0x50}}) {
            cpu.ar[2] = c.rd(cpu.ar[4]++);
            if (cpu.ar[2] == 0) break;
            c.text(0x10E, y, cpu.ar[2]);
        }
        cpu.rc = 0xF;
        static constexpr std::pair<uint16_t, uint32_t> kLabels[] = {
            {0x32, 0xC849}, {0x64, 0xC84A}, {0x6E, 0xC84B}, {0x78, 0xC84C}, {0x82, 0xC84D}, {0x8C, 0xC84E},
            {0x96, 0xC84F}, {0xA0, 0xC850}, {0xAA, 0xC851}, {0xB4, 0xC852}, {0xC8, 0xC853}, {0xF0, 0xC854}};
        for (const auto& [y, ptr] : kLabels) c.text(0x10E, y, c.rd(ptr));
    }
    cpu.rc = rc;
    if (Ctx::s32(cpu.ar[7]) > 0) {
        // 00C5A  bytes do registro, um por linha, alinhados à direita em x = 0x1EA
        cpu.ar[4] = record;
        cpu.r[4] = c.rd(cpu.ar[4]++);
        cpu.r[2] = 0x1EA;
        cpu.r[3] = 0x64;
        for (;;) {
            cpu.r[0] = cpu.r[4] & 0xFFu;
            const uint32_t x = cpu.r[2];
            cpu.r[2] = cpu.r[0];
            cpu.ar[2] = c.rd(vars::kNumberBuf);
            c.call(0xA762);
            cpu.r[2] = x;
            // 00C6B  o recuo usa o R0 devolvido pela 0xA762, como no original
            cpu.r[1] = 0;
            if (Ctx::s32(cpu.r[0]) >= 10) cpu.r[1] = static_cast<uint32_t>(-8);
            if (Ctx::s32(cpu.r[0]) >= 100) cpu.r[1] = static_cast<uint32_t>(-16);
            cpu.r[2] += cpu.r[1];
            c.call(0xA7F4);
            cpu.r[2] = x;
            cpu.r[4] >>= 8;
            cpu.r[3] += 0xA;
            if (Ctx::s32(cpu.r[3]) < 0x8C) continue;
            if (cpu.r[3] == 0xB4 || !(Ctx::s32(cpu.r[3]) > 0x8C)) cpu.r[4] = c.rd(cpu.ar[4]++);
            if (Ctx::s32(cpu.r[3]) < 0xBE) continue;
            break;
        }
        // 00C7E  contadores mecânicos (esquerda, centro, direita, quarto)
        cpu.ar[4] += 3;
        cpu.r[3] = 0xDC;
        for (uint16_t x : {uint16_t{0x10E}, uint16_t{0x17E}, uint16_t{0x146}, uint16_t{0x1BE}}) {
            cpu.ar[2] = c.rd(cpu.ar[4]++);
            cpu.r[2] = x;
            c.call(0xA7F4);
        }
        // 00C8C  "YES" se este é o preço gravado no item 0
        cpu.r[1] = cpu.r[6] + c.rd(0xEBDF);
        cpu.ar[2] = 0;
        c.call(0x99E9);
        cpu.r[0] = (cpu.r[1] == cpu.r[0]) ? 1u : 0u;
        cpu.r[3] = 0xF0;
        cpu.ar[2] = c.rd(0x0E4E);
        cpu.r[2] = 0x1DA;
        if (cpu.r[0] == 0) {
            cpu.ar[2] = c.rd(0x0E4F);
            cpu.r[2] = 0x1E2;
        }
        c.call(0xA7F4);
    }
    saved.restore();
}

/// `0x73A9` + 3: AR0 = registro de preço `[B080] + (AR0 & 0x7F) · 11` (limitado a `[B081]`) + 3.
void priceRecordAt(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[0] = tmsMpyi(cpu.ar[0] & 0x7Fu, 0xB) + c.rd(0xB080);
    if (!(Ctx::s32(cpu.ar[0]) < Ctx::s32(c.rd(0xB081)))) cpu.ar[0] = c.rd(0xB080);
}

/// `0x73D6`: copia o preço padrão escolhido para os itens personalizados 9–0x16.
void copyStandardPricing(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    SavedRegs saved(cpu);
    // 073D7  unidades por moeda (R2) → itens 9–0xC
    struct Step { uint32_t routine, item; };
    static constexpr Step kUnits[] = {{0xC1000F, 9}, {0xC1001B, 0xA}, {0xC10028, 0xB}, {0xC10035, 0xC}};
    for (const Step& s : kUnits) {
        c.call(s.routine);
        cpu.ar[2] = s.item;
        c.call(0x99EE);
    }
    // 073E3  campos (R1) → itens 0xD–0x12
    static constexpr Step kFields[] = {{0xC10042, 0xD}, {0xC1004E, 0xE}, {0xC1005B, 0xF},
                                       {0xC10068, 0x10}, {0xC10075, 0x11}, {0xC10081, 0x12}};
    for (const Step& s : kFields) {
        c.call(s.routine);
        cpu.r[2] = cpu.r[1];
        cpu.ar[2] = s.item;
        c.call(0x99EE);
    }
    // 073FB  contadores mecânicos: as rotinas devolvem R0, mas o original grava R2 (o campo anterior)
    static constexpr Step kCounters[] = {{0xC1008E, 0x13}, {0xC10096, 0x14}, {0xC1009F, 0x15}, {0xC100A8, 0x16}};
    for (const Step& s : kCounters) {
        c.call(s.routine);
        cpu.ar[2] = s.item;
        c.call(0x99EE);
    }
    saved.restore();
}

} // namespace

/// Registra as telas de ajustes, as utilidades e as rotinas auxiliares do menu.
void registerAdjustments(SubroutineDispatcher& d, IBusyWait& wait) {
    for (const Toggle& t : kToggles) {
        addSvc(d, wait, t.addr, [&t](Ctx& c) {
            Tms32031State& cpu = c.cpu;
            cpu.r[5] = c.item(t.item);
            cpu.ar[2] = c.rd(t.line1);
            cpu.ar[3] = c.rd(t.line2);
            if (t.first) {
                cpu.ar[4] = c.rd(t.first);
                cpu.ar[5] = c.rd(t.second);
                c.call(0x0E7D);
            } else {
                c.call(0x0E7A);
            }
            c.setItem(t.item, cpu.r[5]);
        });
    }
    for (const Numeric& n : kNumerics) {
        addSvc(d, wait, n.addr, [&n](Ctx& c) {
            Tms32031State& cpu = c.cpu;
            cpu.r[5] = c.item(n.item);
            cpu.ar[2] = c.rd(n.line1);
            cpu.ar[3] = c.rd(n.line2);
            cpu.r[6] = n.low;
            cpu.r[7] = n.high;
            cpu.ar[4] = c.rd(n.formatter);
            if (n.step) {
                cpu.r[4] = n.step;
                c.call(0x0F5B);
            } else {
                c.call(0x0F5A);
            }
            c.setItem(n.item, cpu.r[5]);
        });
    }
    // 00F41  volume mínimo (item 0x20, 0–255) editado em 0–30: ×0.117645 e ×8.5
    addSvc(d, wait, 0x0F41, [](Ctx& c) {
        Tms32031State& cpu = c.cpu;
        const double k = shortFloat(static_cast<uint16_t>(c.rd(0x0F44)));
        cpu.r[5] = tmsFix(static_cast<double>(Ctx::s32(c.item(0x20))) * k);
        cpu.ar[2] = c.rd(0x0F40);
        cpu.ar[3] = c.rd(0x0E52);
        cpu.r[6] = 0;
        cpu.r[7] = 0x1E;
        cpu.ar[4] = c.rd(0x0F52);
        c.call(0x0F5A);
        const double scale = shortFloat(static_cast<uint16_t>(c.rd(0x0F4D)));
        cpu.r[2] = tmsFix(static_cast<double>(Ctx::s32(cpu.r[5])) * scale);
        cpu.ar[2] = 0x20;
        c.call(0x99EE);
    });
    addSvc(d, wait, 0x0EFA, [](Ctx& c) { formatValue(c, c.cpu.r[5]); });
    addSvc(d, wait, 0x0F11, [](Ctx& c) { formatValue(c, tmsMpyi(c.cpu.r[5], 5) + 0x3C); });
    addSvc(d, wait, 0x0F2A, [](Ctx& c) { formatValue(c, c.cpu.r[5]); });
    addSvc(d, wait, 0x0F53, [](Ctx& c) { formatValue(c, c.cpu.r[5]); });
    for (const Utility& u : kUtilities) {
        addSvc(d, wait, u.addr, [&u](Ctx& c) { utility(c, u); });
    }
    // 00BDC  preços de um país: submenu [[0BDB] + 2·R6] com R6 itens a partir de [B082 + R6]
    addSvc(d, wait, 0x0BDC, [](Ctx& c) {
        Tms32031State& cpu = c.cpu;
        c.wr(0xEBE0, 0xFFFFFFFFu);
        cpu.ar[5] = cpu.r[6] + c.rd(0xB082);
        cpu.r[0] = c.rd(cpu.ar[5]);
        c.wr(0xEBDF, cpu.r[0]);
        cpu.r[0] = tmsMpyi(cpu.r[6], 2);
        cpu.ar[5] = c.rd(0x0BDB) + cpu.r[0];
        cpu.ar[6] = c.rd(cpu.ar[5] + 1);
        cpu.ar[5] = c.rd(cpu.ar[5]);
        cpu.ar[7] = c.rd(0x0FC3);
        menuLoop(c, true);
    });
    addSvc(d, wait, 0x0BEA, [](Ctx& c) {
        bevelBox(c, 0x14, 0x2D, 0xFA, 0xFA, false);
        bevelBox(c, 0x106, 0x2D, 0x1F4, 0xFA, false);
    });
    addSvc(d, wait, 0x0BF5, pricingDetails);
    addSvc(d, wait, 0x0C97, [](Ctx& c) {
        c.cpu.ar[2] = c.rd(0x0E4E);
        c.cpu.r[2] = 0x1DA;
        if (c.cpu.r[0] == 0) {
            c.cpu.ar[2] = c.rd(0x0E4F);
            c.cpu.r[2] = 0x1E2;
        }
        c.call(0xA7F4);
    });
    // 00FC8  "SELECT THIS PRICING": item 0 = preço, item 0x17 = 1 (padrão), zera 0x70/0x71 e copia
    addSvc(d, wait, 0x0FC8, [](Ctx& c) {
        Tms32031State& cpu = c.cpu;
        c.wr(0xEBE0, 0xFFFFFFFFu);
        cpu.ar[2] = c.rd(0xC893);
        cpu.ar[3] = c.rd(0x0E53);
        const uint32_t r6 = cpu.r[6];
        c.call(0x0E76);
        cpu.r[6] = r6;
        if (cpu.carry()) return;
        c.setItem(0, cpu.r[6] + c.rd(0xEBDF));
        c.setItem(0x17, 1);
        c.setItem(0x70, 0);
        c.setItem(0x71, 0);
        c.call(0x73D6);
        cpu.ar[2] = c.rd(0x0FE1);
        c.call(0x0E62);
    });
    // 00FE7  SHOW FRACTION (item 0x12): YES grava 1
    addSvc(d, wait, 0x0FE7, [](Ctx& c) {
        Tms32031State& cpu = c.cpu;
        cpu.r[5] = c.item(0x12);
        cpu.ar[2] = c.rd(0xC899);
        cpu.ar[3] = c.rd(0x0E52);
        c.call(0x0E77);
        c.setItem(0x12, cpu.carry() ? 0u : 1u);
    });
    // 00FF2  USE CUSTOM PRICING: item 0x17 = 0 (personalizado) ou 1; avisa ao trocar para o personalizado
    addSvc(d, wait, 0x0FF2, [](Ctx& c) {
        Tms32031State& cpu = c.cpu;
        cpu.r[5] = c.item(0x17) ^ 1u;
        cpu.ar[2] = c.rd(0xC89F);
        cpu.ar[3] = c.rd(0x0E52);
        c.call(0x0E77);
        const uint32_t r2 = cpu.carry() ? 1u : 0u;
        c.setItem(0x17, r2);
        cpu.r[2] = r2;
        if (r2 == 1) return;
        cpu.ar[2] = c.rd(0x1004);
        c.call(0x0E62);
    });
    // 01032  preço personalizado: item 9 + R6 (R6 < 9) ou 10 + R6, de 0 a 100
    addSvc(d, wait, 0x1032, [](Ctx& c) {
        Tms32031State& cpu = c.cpu;
        auto itemOf = [&cpu]() { return cpu.r[6] + (Ctx::s32(cpu.r[6]) < 9 ? 9u : 0x13u - 9u); };
        cpu.r[5] = c.item(itemOf());
        cpu.ar[2] = c.rd(c.rd(0xC8A0) + cpu.r[6]);
        cpu.ar[3] = c.rd(0x0E52);
        const uint32_t r6 = cpu.r[6];
        cpu.r[6] = 0;
        cpu.r[7] = 100;
        cpu.ar[4] = c.rd(0x0EF9);
        c.call(0x0F5A);
        cpu.r[6] = r6;
        c.setItem(itemOf(), cpu.r[5]);
        c.setItem(0x70, 0);
        c.setItem(0x71, 0);
    });
    addSvc(d, wait, 0x73A9, priceRecordAt);
    addSvc(d, wait, 0x73B0, [](Ctx& c) {
        priceRecordAt(c);
        c.cpu.ar[0] += 3;
    });
    addSvc(d, wait, 0x73D6, copyStandardPricing);
}

} // namespace rea::core::service
