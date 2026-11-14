/**
 * @file service_audits.cpp
 * @brief Auditorias do jogo (`0x14D0`–`0x154C`) e os valores calculados `0x1419`/`0x1426`.
 *
 * @details A tabela `[C8BC]` = 0x1430 lista, por linha, o item da CMOS e o ponteiro do rótulo.
 * Marcadores: −3 = linha em branco, −1 = fim da página, −2 = volta à primeira página.
 * Um item 0x5A00xx é calculado: a palavra seguinte é a rotina (`0x1419`, `0x1426`).
 * VOLUME ± passa de página; ENTER sai. Pela sequência secreta (`[EBBB]` = 1), a chave de
 * IN1 bit 0x10 abre o registro de erros (`0xAB79`) e a de IN1 bit 0x20 sai.
 */
#include "core/service/service_internal.hpp"

namespace rea::core::service {

namespace {

constexpr uint32_t kEndOfPage = 0xFFFFFFFFu;  ///< −1
constexpr uint32_t kRestart = 0xFFFFFFFEu;    ///< −2
constexpr uint32_t kBlank = 0xFFFFFFFDu;      ///< −3

/// Moldura da página (`0x14DF`).
void auditFrame(Ctx& c) {
    c.shape(0xAC23, 0x0A, 0x28, 0x190, 0x28, 0xB);
    c.shape(0xAC23, 0x0A, 0x28, 0x0A, 0xDC, 0xB);
    c.shape(0xAC23, 0x190, 0x28, 0x190, 0xDC, 0xD);
    c.shape(0xAC23, 0x0A, 0xDC, 0x190, 0xDC, 0xD);
}

/// Escreve uma página a partir de AR5. @return false se a tabela pediu para recomeçar (−2).
bool auditPage(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    c.shape(0xA7E5, 0x0C, 0x2A, 0x18E, 0xDA, 0xC);
    cpu.r[5] = 0x32;
    cpu.rc = 0xF;
    for (;;) {
        cpu.ar[2] = c.rd(cpu.ar[5]++);
        if (cpu.ar[2] == kEndOfPage) return true;
        if (cpu.ar[2] == kRestart) return false;
        if (cpu.ar[2] != kBlank) {
            cpu.r[0] = cpu.ar[2] >> 16;
            if (cpu.r[0] == 0x5A) {
                cpu.r[0] = c.rd(cpu.ar[5]++);
                c.call(cpu.r[0]);  // valor calculado
            } else {
                c.call(0x99E9);
            }
            cpu.r[2] = cpu.r[0];
            cpu.ar[2] = c.rd(vars::kNumberBuf);
            c.call(0xA762);
            cpu.r[3] = cpu.r[5];
            cpu.r[2] = 0xFA;
            cpu.rc = 0xF;
            c.call(0xA7F4);
            cpu.ar[2] = c.rd(cpu.ar[5]++);
            cpu.r[3] = cpu.r[5];
            cpu.r[2] = 0x14;
            cpu.rc = 0xF;
            c.call(0xA7F4);
        }
        cpu.r[5] += 0xA;
    }
}

/// `0x14D0`: tela de auditorias (páginas de contadores da CMOS).
void audits(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    for (;;) {
        // 014D0
        clearScreen(c);
        cpu.ar[4] = 0;
        c.textAt(0x32, 0x14A, 0xC8BA, 0xF);
        if (c.rd(vars::kPrinterFlag) == 0) {
            c.textAt(0x32, 0x154, 0xC8BB, 0xF);
            c.textAt(0xB4, 0x173, 0xC7E3, 0xF);  // 0x06A8
        }
        bool reprint = false;
        while (!reprint) {
            auditFrame(c);
            cpu.ar[5] = c.rd(0xC8BC);
            while (auditPage(c)) {
                // 0151D  espera uma tecla; [EBE6] guarda as DIPs para limpar a linha quando mudarem
                c.wr(0xEBE6, 0);
                bool next = false;
                while (!next) {
                    c.burn(cycles::kPoll);
                    cpu.r[0] = c.rd(vars::kDips);
                    cpu.r[1] = c.rd(0xEBE6);
                    if (cpu.r[1] != cpu.r[0]) {
                        c.wr(0xEBE6, cpu.r[0]);
                        c.shape(0xA7E5, 0x32, 0xF0, 0x12C, 0xF9, 0);
                    }
                    cpu.r[0] = c.rd(vars::kKeys);
                    cpu.r[1] = c.rd(vars::kKeys);
                    if ((cpu.r[1] & keys::kEnter) == 0) {
                        // 01549  espera soltar ENTER e volta
                        while (~c.rd(vars::kKeys) & keys::kEnter) c.burn(cycles::kPoll);
                        return;
                    }
                    cpu.r[1] >>= 16;
                    if ((cpu.r[1] & 0x10u) == 0) {
                        cpu.r[0] = c.rd(vars::kPrinterFlag);
                        if (cpu.r[0] != 0) {
                            c.call(0xAB79);  // registro de erros
                            reprint = true;
                            break;
                        }
                        // R0 = 0 cai no teste de VOLUME abaixo como se as duas estivessem apertadas
                    }
                    if ((cpu.r[0] & keys::kUp) == 0 || (cpu.r[0] & keys::kDown) == 0) {
                        while (~c.rd(vars::kKeys) & 0x0300u) c.burn(cycles::kPoll);
                        next = true;
                    } else if ((cpu.r[1] & keys::kService) == 0) {
                        while (((c.rd(vars::kKeys) >> 16) & keys::kService) == 0) c.burn(cycles::kPoll);
                        next = true;
                    } else if (c.rd(vars::kPrinterFlag) != 0 && (cpu.r[1] & 0x20u) == 0) {
                        return;
                    }
                }
                if (reprint) break;
            }
            if (reprint) break;
            // −2: recomeça da primeira página (BU $14DF)
        }
    }
}


/**
 * @brief `0xAB79`: registro de erros da CMOS (gravado por `0xAB1A`; item 0xDE = número de entradas).
 * @details 5 entradas por página, cada uma com "LOG n" e os 16 valores em hexadecimal (`0x99F9` + `0xA7AC`),
 * 6 por linha. Teclas (IN1, ativas em 0): VIEW 1 (0x10) sai, VIEW 3 (0x40) próxima página (volta à primeira
 * no fim), VIEW 1 + 2 + 3 (0x70) apaga o registro; ENTER também sai.
 */
void errorLog(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    for (;;) {
        // 0AB79
        c.call(0x8E79);
        c.call(0x8E4E);
        c.wr(vars::kDrawPage, 0x900000);
        cpu.rc = 0xF;
        c.text(0x0A, 0x136, c.rd(0xB13A));  // VIEW 1  TO EXIT
        c.text(0x0A, 0x140, c.rd(0xB13B));  // VIEW 3  NEXT PAGE
        c.text(0x0A, 0x14A, c.rd(0xB13C));  // VIEW 3 + VIEW 2 + VIEW 1 CLEAR LOG
        bool first_page = true;
        bool cleared = false;
        while (!cleared) {
            if (first_page) {
                // 0AB8C  primeira entrada
                cpu.ar[2] = 0x4BF + 0x3F8 + c.rd(0x9936);
                cpu.ar[6] = 0;
            }
            first_page = false;
            // 0AB90  moldura da página e "NO ERRORS" com o registro vazio
            const uint32_t entry = cpu.ar[2];
            cpu.ar[2] = 5;
            cpu.r[2] = 0x23;
            cpu.r[3] = 0x1FE;
            cpu.rc = 0x127;
            c.call(0x0628);
            cpu.ar[2] = entry;
            cpu.r[0] = c.item(0xDE);
            cpu.ar[2] = entry;
            if (Ctx::s32(cpu.r[0]) <= 0) {
                c.text(0x0A, 0x32, c.rd(0xB13D));  // NO ERRORS (RC como a 0x0628 deixou: sem LDI no ROM)
            }
            cpu.r[4] = 5;
            cpu.r[6] = 0x0A;
            cpu.r[7] = 0x32;
            bool restart = false;
            for (;;) {
                // 0ABA4
                const uint32_t ar2 = cpu.ar[2];
                cpu.r[0] = c.item(0xDE);
                cpu.ar[2] = ar2;
                if (cpu.ar[6] == cpu.r[0] || cpu.r[0] == 0) break;
                if (Ctx::s32(cpu.ar[6]) >= 0x1E) {
                    restart = true;  // BGE $AB8C: recomeça da primeira entrada sem esperar tecla
                    break;
                }
                cpu.ar[2] = c.rd(0x6FEA);
                cpu.r[2] = cpu.ar[6] + 1;
                c.call(0xA762);
                cpu.rc = 0xF;
                c.text(0x87, cpu.r[7] - 0x0A, c.rd(0x6FEA));
                cpu.rc = 0xF;
                c.text(0x64, cpu.r[7] - 0x0A, c.rd(0xB13F));  // LOG
                cpu.ar[2] = ar2;
                cpu.ar[5] = 0x0F;
                do {
                    c.call(0x99F9);  // R0 = valor; AR2 += 4
                    cpu.r[2] = cpu.r[0];
                    const uint32_t a = cpu.ar[2];
                    cpu.ar[2] = c.rd(0x6FEA);
                    c.call(0xA7AC);
                    cpu.rc = 0xF;
                    c.text(cpu.r[6], cpu.r[7], c.rd(0x6FEA));
                    cpu.ar[2] = a;
                    cpu.r[6] += 0x46;
                    if (Ctx::s32(cpu.r[6]) >= 0x1C2) {
                        cpu.r[6] = 0x0A;
                        cpu.r[7] += 0x0A;
                    }
                } while (Ctx::s32(--cpu.ar[5]) >= 0);
                cpu.r[6] = 0x0A;
                cpu.r[7] += 0x14;
                cpu.ar[6] += 1;
                if (Ctx::s32(--cpu.r[4]) < 0) break;
            }
            if (restart) {
                first_page = true;
                continue;
            }
            for (;;) {
                // 0ABD8  teclas
                c.waitVblank();
                cpu.r[1] = c.rd(vars::kKeys);
                if ((cpu.r[1] & keys::kEnter) == 0) {
                    while (~c.rd(vars::kKeys) & keys::kEnter) c.burn(cycles::kPoll);  // 0ABF8
                    return;
                }
                cpu.r[1] >>= 16;
                cpu.r[0] = cpu.r[1] & 0x70u;
                if (cpu.r[0] == 0) {
                    // 0ABE0  as três VIEW: apaga o registro
                    c.setItem(0xDE, 0);
                    c.waitVblank();
                    c.waitVblank();
                    c.waitVblank();
                    cleared = true;
                    break;
                }
                if ((cpu.r[1] & 0x40u) == 0) {
                    // 0ABE9  VIEW 3: próxima página, ou a primeira no fim
                    const uint32_t ar2 = cpu.ar[2];
                    cpu.r[0] = c.item(0xDE);
                    cpu.ar[2] = ar2;
                    first_page = Ctx::s32(cpu.ar[6]) >= Ctx::s32(cpu.r[0]);
                    break;
                }
                if ((cpu.r[1] & 0x10u) == 0) {
                    // 0ABF3  VIEW 1: espera soltar e sai
                    while (((c.rd(vars::kKeys) >> 16) & 0x10u) == 0) c.burn(cycles::kPoll);
                    return;
                }
            }
        }
    }
}

} // namespace

/// Registra as telas de auditoria e o registro de erros.
void registerAudits(SubroutineDispatcher& d, IBusyWait& wait) {
    addSvc(d, wait, 0x14D0, audits);
    addSvc(d, wait, 0xAB79, errorLog);
    // 01419  porcentagem: item 0x6A · 100 / (itens 0xD3 + 0xD2); 0 se a soma é 0
    addSvc(d, wait, 0x1419, [](Ctx& c) {
        Tms32031State& cpu = c.cpu;
        cpu.r[1] = c.item(0xD3);
        cpu.r[1] += c.item(0xD2);
        if (cpu.r[1] == 0) {
            cpu.r[0] = cpu.r[1];
            return;
        }
        cpu.r[0] = tmsMpyi(c.item(0x6A), 0x64);
        c.call(0xA531);
    });
    // 01426  média: item 0x77 / (itens 0xD3 + 0xD2)
    addSvc(d, wait, 0x1426, [](Ctx& c) {
        Tms32031State& cpu = c.cpu;
        cpu.r[1] = c.item(0xD3);
        cpu.r[1] += c.item(0xD2);
        c.item(0x77);
        c.call(0xA531);
    });
}

} // namespace rea::core::service
