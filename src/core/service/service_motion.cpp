/**
 * @file service_motion.cpp
 * @brief Plataforma de movimento de 3 eixos: menu de movimento (`0x46A1`–`0x4729`, `0x460B`), instalação
 *        (`0x4539`–`0x460A`, `0x4615`, `0x4644`, `0x4A72`, `0x4A9D`), envio de posições (`0x47E8`),
 *        avisos (`0x4961`, `0x4979`), comandos (`0x490B`, `0x4A65`, `0x48C4`, `0x4954`) e o atraso `0x4991`.
 *
 * @details O controlador de movimento (Galil) fala pelo UART do latch `0x995000` (ver `io/serial_printer.hpp`):
 * `0x48D2` envia um texto e `0x4A4E` trata a resposta. Os sinais de segurança (tapetes, feixe, chave de
 * falha) chegam nos bits 8–15 de IN1 (`[0x991030]`); a máscara `[0x4995]` = 0xFF80 dá "há erro".
 * Tudo só roda com o DIP de movimento (`0x4693`, Z = 1) e o ajuste 0x34 = 1 (`0x469D`); sem eles, os itens
 * do menu mostram "WARNING MOTION DIP SWITCH OFF" / "WARNING MOTION NOT DETECTED" piscando.
 *
 * As leituras `LDI *AR2,AR2` com AR2 = 0 depois de cada acesso a `0x991030` (leitura de `[0]`, só atraso
 * de barramento) não têm efeito e não foram reproduzidas.
 */
#include "core/non_local_jump.hpp"
#include "core/service/service_internal.hpp"
#include "core/tms_float.hpp"

#include <cstdlib>

namespace rea::core::service {

namespace {

constexpr uint32_t kIn1 = 0x991030;        ///< IN1: segurança da plataforma nos bits 8–15
constexpr uint32_t kErrorMask = 0x4995;    ///< `[0x4995]` = 0xFF80
constexpr uint32_t kItemMotion = 0x34;     ///< ajuste da CMOS: movimento ligado
constexpr uint32_t kZero = 0x80000000u;    ///< `LDF 0.0`

/// Converte o float TMS @p raw para double.
double f(uint32_t raw) { return tmsToDouble(raw); }
/// Converte @p v para float TMS.
uint32_t tf(double v) { return doubleToTms(v); }

/// `[0x991030]` & `[0x4995]`.
uint32_t safetyError(Ctx& c) { return c.rd(kIn1) & c.rd(kErrorMask); }

/// `0x48D2` + `0x4A4E`: envia o texto @p str ao controlador e trata a resposta.
void send(Ctx& c, uint32_t str) {
    c.cpu.ar[2] = str;
    c.call(0x48D2);
    c.call(0x4A4E);
}

/// `0xA7E5` com AR2 = 1 e RS = 0 (apaga uma faixa da tela).
void wipe(Ctx& c, uint32_t r2, uint32_t r3, uint32_t rc) { c.shape(0xA7E5, 1, r2, r3, rc, 0); }

/// `0x4991`: R0 · 5000 voltas de `SUBI`/`BGE` (5 ciclos cada). Saída: R0 = −1.
void delay(Ctx& c, uint32_t r0) {
    c.cpu.r[0] = r0;
    c.burn((static_cast<uint64_t>(tmsMpyi(r0, 0x1388)) + 1) * 5);
    c.cpu.r[0] = 0xFFFFFFFFu;
}

/// `AR3` + 1 quadros (`LDI n,AR3` / `CALL $4EC7` / `DBU AR3`).
void frames(Ctx& c, uint32_t n) {
    c.cpu.ar[3] = n;
    do {
        c.waitVblank();
    } while (Ctx::s32(--c.cpu.ar[3]) >= 0);
}

/// `0x8E4E` e desenho na página 0 (`[CE13]` = 0x900000).
void drawOnPage0(Ctx& c) {
    c.call(0x8E4E);
    c.wr(vars::kDrawPage, 0x900000);
}

/// `0x4961` / `0x4979`: pisca o aviso (@p dark na cor 0, @p lit na cor 1) três vezes, com o som 0x3D8.
void blinkWarning(Ctx& c, uint32_t dark, uint32_t lit) {
    Tms32031State& cpu = c.cpu;
    drawOnPage0(c);
    cpu.ar[5] = 2;
    do {
        cpu.rc = 0;
        c.text(0x96, 0x82, c.rd(dark));
        delay(c, 0xFA);
        cpu.rc = 1;
        c.text(0x96, 0x82, c.rd(lit));
        c.sound(0x03D8);
        delay(c, 0xFA);
    } while (Ctx::s32(--cpu.ar[5]) >= 0);
}

/// `0x490B`: pulsa os endereços 0x0C e 0x0B do latch (inicializa o controlador). GIE desligado.
void resetController(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.st &= ~Tms32031State::kStGie;
    cpu.ar[3] = 0x995000;
    for (uint32_t n : {0x0Cu, 0x0Bu}) {
        for (const auto& [lo, hi] : {std::pair<uint32_t, uint32_t>{0xFFFFFF00u | n, 0xFFFFF700u | n},
                                     {0xFFFFFF00u, 0xFFFFFB00u}}) {
            cpu.r[0] = lo;
            cpu.r[1] = hi;
            for (uint32_t v : {lo, hi, lo}) {
                c.wr(cpu.ar[3], v);
                c.call(0x4689);
            }
        }
    }
    c.call(0x4C33);
}

/// `0x4A60`: religa as interrupções e desliga o movimento (ajuste 0x34 = 0).
void motionLost(Ctx& c) {
    c.call(0x4C33);
    c.setItem(kItemMotion, 0);
}

/// Ciclos de uma leitura de estado (`0x49B8`): 6 escritas no latch, cada uma com `0x4689` (RPTS 100).
constexpr uint32_t kStatusCycles = 6 * 110;

/// `0x4A65`: espera o controlador responder (até 10 000 leituras de estado) e esvazia a recepção
/// (até 1000). Sem resposta: `0x4A60`.
void awaitController(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    for (cpu.r[6] = 10000;;) {
        c.burn(kStatusCycles);
        c.call(0x49B8);
        if (cpu.carry()) break;  // BLO
        if (--cpu.r[6] == 0) return motionLost(c);
    }
    for (cpu.r[6] = 1000;;) {
        c.call(0x49CE);
        if (!cpu.carry()) return;  // BHS
        if (--cpu.r[6] == 0) return motionLost(c);
    }
}

/// `0x48C4`: envia a lista de textos AR4 (até 0), um por quadro, e depois `[C7CC]` e `[C7CD]`.
void sendList(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    for (;;) {
        cpu.ar[2] = c.rd(cpu.ar[4]++);
        if (cpu.ar[2] == 0) break;
        c.call(0x48D2);
        c.call(0x4A4E);
        c.waitVblank();
    }
    send(c, c.rd(0xC7CC));
    send(c, c.rd(0xC7CD));
}

/// `0x4954`: com o movimento ligado, envia a configuração `[C7CF]` e zera as posições `[EBB4..EBB6]` e `[EB4E]`.
void configure(Ctx& c) {
    c.call(0x4693);
    if (!c.cpu.zero()) return;
    c.call(0x469D);
    if (!c.cpu.zero()) return;
    c.cpu.ar[4] = c.rd(0xC7CF);
    sendList(c);
    c.cpu.r[0] = 0;
    c.wr(0xEBB4, 0);
    c.wr(0xEBB5, 0);
    c.wr(0xEBB6, 0);
    c.wr(0xEB4E, 0);
}

/// `0x4615`: desliga os motores devagar: envia `[C78E]` e a lista `[C78F]` de dois em dois, com "SLOWLY
/// TURNING OFF MOTORS" e o som 0x3D8 entre eles.
void motorsOff(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    send(c, c.rd(0xC78E));
    wipe(c, 0xC8, 0x1F4, 0x186);
    delay(c, 0x1F4);
    cpu.ar[4] = c.rd(0xC78F);
    for (;;) {
        // 04621
        cpu.rc = 1;
        c.text(1, 0xC8, c.rd(0xC790));
        c.sound(0x03D8);
        delay(c, 0x7D0);
        cpu.ar[2] = c.rd(cpu.ar[4]++);
        if (cpu.ar[2] == 0) break;
        c.call(0x48D2);
        c.call(0x4A4E);
        wipe(c, 0xC8, 0x1F4, 0x186);
        delay(c, 0x1F4);
        cpu.ar[2] = c.rd(cpu.ar[4]++);
        if (cpu.ar[2] == 0) break;
        c.call(0x48D2);
        c.call(0x4A4E);
    }
    wipe(c, 0xC8, 0x1F4, 0x186);
}

/// `0x4644`: falha na instalação: motores desligados, ajuste 0x34 = 0, "MOTION SYSTEM HAS ERRORS /
/// DEACTIVATED" piscando 5 vezes e a causa (bits 12–15 de IN1, tabela `[4684]`) por 3000 · 5000 voltas.
void setupFailed(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    motorsOff(c);
    c.setItem(kItemMotion, 0);
    wipe(c, 0xFA, 0x1F4, 0x15E);
    cpu.ar[5] = 4;
    do {
        cpu.rc = 1;
        c.text(0x64, 0xFA, c.rd(0xC791));
        c.text(0x64, 0x104, c.rd(0xC792));
        c.sound(0x03D8);
        delay(c, 0x1C2);
        wipe(c, 0xFA, 0x1F4, 0x15E);
        delay(c, 0x96);
    } while (Ctx::s32(--cpu.ar[5]) >= 0);
    cpu.r[0] = (c.rd(kIn1) >> 12) & 0xFu;
    cpu.rc = 1;
    if (Ctx::s32(cpu.r[0]) >= 8) {
        c.text(0x64, 0xFA, c.rd(0xC793));  // BOARD NOT PLUGGED IN
    } else {
        cpu.ar[2] = c.rd(cpu.r[0] + c.rd(0x4684));
        c.text(0x64, 0xFA, cpu.ar[2]);
    }
    delay(c, 0xBB8);
    wipe(c, 0xFA, 0x1F4, 0x15E);
}

/// `0x4A72` (fim de curso superior, bit 8) / `0x4A9D` (inferior, bit 4): motores desligados, ajuste 0x34 = 0 e
/// o erro com os eixos sem a chave (`[EBB8..EBBA]`), 6 vezes.
void limitError(Ctx& c, bool upper) {
    Tms32031State& cpu = c.cpu;
    motorsOff(c);
    c.setItem(kItemMotion, 0);
    const uint32_t bit = upper ? 8u : 4u;
    const uint32_t base = upper ? 0xC7D7u : 0xC7DBu;
    if (!upper) wipe(c, 0xC8, 0x1F4, 0x186);  // 04AA1
    cpu.ar[5] = 5;
    do {
        wipe(c, 0xC8, 0x1F4, 0x186);
        delay(c, 0x32);
        cpu.rc = 1;
        c.text(1, 0xC8, c.rd(base));
        if (c.rd(0xEBB8) & bit) c.text(1, 0xD2, c.rd(base + 1));
        if (c.rd(0xEBB9) & bit) c.text(1, 0xDC, c.rd(base + 2));
        if (c.rd(0xEBBA) & bit) c.text(1, 0xE6, c.rd(base + 3));
        delay(c, 0x1C2);
    } while (Ctx::s32(--cpu.ar[5]) >= 0);
}

/// Espera de uma chave de fim de curso (`0x4588`/`0x45BF`): repete os 3 comandos @p cmds por até 514 quadros
/// até um eixo acusar o @p bit em `[EBB8..EBBA]`.
/// @return 0 = chegou; 1 = erro de segurança (recomeça em `0x4560`); 2 = esgotou o tempo.
int waitLimit(Ctx& c, uint32_t cmds, uint32_t bit) {
    Tms32031State& cpu = c.cpu;
    cpu.ar[3] = 0x201;
    for (;;) {
        const uint32_t ar3 = cpu.ar[3];
        send(c, c.rd(cmds));
        send(c, c.rd(cmds + 1));
        send(c, c.rd(cmds + 2));
        cpu.ar[3] = ar3;
        c.waitVblank();
        if (!((c.rd(0xEBB8) & bit) || (c.rd(0xEBB9) & bit) || (c.rd(0xEBBA) & bit))) return 0;
        if (safetyError(c) != 0) return 1;
        if (Ctx::s32(--cpu.ar[3]) < 0) return 2;
    }
}

/// `0x4539`: instalação da plataforma ("MOTION SETUP TESTING..."). Chamada pela `0x4530` com o DIP ligado.
void setup(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    cpu.rc = 1;
    c.text(1, 0xFA, c.rd(0xC774));
    resetController(c);
    awaitController(c);
    c.call(0x469D);
    if (!cpu.zero()) return setupFailed(c);
    cpu.rc = 1;
    c.text(0x32, 0x104, c.rd(0xC775));  // TESTING UPPER LIMITS
    for (uint32_t p = 0xC776; p <= 0xC779; ++p) send(c, c.rd(p));
    send(c, c.item(0x29) == 1 ? c.rd(0xC77A) : c.rd(0xC77B));
    frames(c, 0x64);
    c.call(0x4936);
    for (;;) {
        // 04560  espera os tapetes e o feixe livres (até 571 quadros)
        cpu.ar[3] = 0x23A;
        for (;;) {
            if (safetyError(c) == 0) break;
            cpu.rc = 1;
            c.text(0x32, 0x10E, c.rd(0xC77C));  // MOTION ERROR
            c.text(0x32, 0x118, c.rd(0xC77D));  // CLEAR MATS AND BEAM TO CONTINUE
            c.waitVblank();
            if (Ctx::s32(--cpu.ar[3]) < 0) return setupFailed(c);
        }
        // 04574
        delay(c, 0x7D0);
        c.call(0x4936);
        c.shape(0xA7E5, 0x32, 0x10E, 0x1F4, 0x121, 0);
        send(c, c.rd(0xC77E));
        frames(c, 0x0A);
        c.wr(0xEBB8, 0xFFFFFFFFu);
        c.wr(0xEBB9, 0xFFFFFFFFu);
        c.wr(0xEBBA, 0xFFFFFFFFu);
        const int up = waitLimit(c, 0xC77F, 8);
        if (up == 1) continue;
        if (up == 2) return limitError(c, true);
        // 045A7
        c.call(0x4996);
        if (cpu.carry()) return setupFailed(c);
        cpu.r[0] = (safetyError(c) >> 8) & 0xF0u;
        if (Ctx::s32(cpu.r[0]) >= 0x60) return setupFailed(c);
        cpu.rc = 1;
        c.text(0xFA, 0x104, c.rd(0xC782));  // LOWER LIMIT TEST
        send(c, c.rd(0xC783));
        frames(c, 0x0A);
        const int down = waitLimit(c, 0xC784, 4);
        if (down == 1) continue;
        if (down == 2) return limitError(c, false);
        break;
    }
    // 045DE
    send(c, c.rd(0xC787));
    c.call(0x469D);
    if (!cpu.zero()) return setupFailed(c);
    frames(c, 0xAB);
    for (uint32_t p = 0xC788; p <= 0xC78B; ++p) send(c, c.rd(p));
    c.waitVblank();
    c.call(0x49A7);
    c.call(0x4936);
    configure(c);
    if (safetyError(c) != 0) return setupFailed(c);
    if (c.rd(0xEBB7) != 0) return setupFailed(c);
    wipe(c, 0xFA, 0x1F4, 0x15E);
    cpu.rc = 1;
    c.text(0x12C, 0xF0, c.rd(0xC78C));  // MOTION SYSTEM OK
}

/// `0x47E8`: R2, R3, R4 ∈ [−1, 1] → posições 26000 ± 17000 dos eixos Z, Y e X; cada eixo que mudou 200 ou
/// mais é enviado ("ZA=", "YA=", "XA=" + número, `[C7A2..C7B6]`).
/// @note Peculiaridade preservada: Y e X guardam R0 (`STI R0`) em `[EBB6]`/`[EBB5]`, não a posição nova.
void sendPositions(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    const uint32_t k = tf(17000.0), base = tf(26000.0);
    uint32_t pos[3];
    for (int i = 0; i < 3; ++i) {
        const uint32_t v = tf(f(tf(f(cpu.r[2 + i]) * f(k))) + f(base));
        pos[i] = tmsFix(f(v));
    }
    cpu.r[1] = base;
    cpu.r[2] = pos[0];
    cpu.r[3] = pos[1];
    cpu.r[4] = pos[2];
    struct Axis { uint32_t last; uint32_t table; bool storeR0; };
    static constexpr Axis kAxes[] = {{0xEBB4, 0xC7A2, false}, {0xEBB6, 0xC7A9, true}, {0xEBB5, 0xC7B0, true}};
    const uint32_t values[3] = {pos[0], pos[2], pos[1]};  // R2, depois POP R4, depois POP R3
    for (int i = 0; i < 3; ++i) {
        const Axis& a = kAxes[i];
        cpu.r[2] = values[i];
        cpu.r[1] = static_cast<uint32_t>(std::abs(static_cast<long long>(Ctx::s32(c.rd(a.last))) - Ctx::s32(cpu.r[2])));
        if (Ctx::s32(cpu.r[1]) < 0xC8) continue;
        c.wr(a.last, a.storeR0 ? cpu.r[0] : cpu.r[2]);
        cpu.ar[2] = c.rd(a.table);
        c.call(0xA762);
        cpu.ar[0] = c.rd(a.table + 1);
        cpu.ar[1] = c.rd(a.table + 2);
        c.call(0x77D1);
        cpu.ar[0] = c.rd(a.table + 3);
        cpu.ar[1] = c.rd(a.table + 4);
        c.call(0x77E7);
        cpu.ar[0] = c.rd(a.table + 5);
        cpu.ar[1] = c.rd(a.table + 6);
        c.call(0x77E7);
        cpu.ar[2] = cpu.ar[0];
        c.call(0x48D2);
        c.call(0x4A4E);
    }
}

/// `0x472C` (chamada pela `0x472A` no processo do carro do jogador, com o DIP de movimento ligado).
/// @details
/// - `[EB4F]` ≠ 0 (botão de parada já acionado): nada;
/// - a cada 30 chamadas (`[EB51]`) envia `[C798]` (consulta de estado);
/// - `[EB52]` ≠ 0 (aviso/erro em curso, contado em `[C92F]`): com erro de segurança volta a 0x11D; ao zerar,
///   reinicia o controlador (`0x4936`) e, sem erro, envia `[C79B]` e `[EB53]` = 0;
/// - `[EB50]` ≤ 0 (o controlador parou de responder): ajuste 0x80 = 1 e 0x34 = 0;
/// - erro de segurança (`[C7A0]`): `[EB52]` = 0x11D, `[EB53]` = 1, gravidade `[EB54]` pelos bits 12–15; com o bit
///   15 em 0 e o 7 em 1, botão de parada: `[EB54]` = 3, `[EB4F]` = 1 e "MOTION STOP BUTTON HIT" por 171 quadros;
/// - senão (`[EB4E]` = 0 e ajuste 0x34 = 1): inclinação pelas alturas das 4 rodas do carro AR5 (`+7`, `+0xD`,
///   `+0x13`, `+0x19`) e pela rolagem/arfagem (`+0x42`, `+0x43`), limitada a ±1, enviada por `0x47E8`.
void motionRide(Ctx& c) {
    Tms32031State& cpu = c.cpu;
    if (c.rd(0xEB4F) != 0) return;
    cpu.r[0] = c.rd(0xEB51) - 1;
    c.wr(0xEB51, cpu.r[0]);
    if (Ctx::s32(cpu.r[0]) <= 0) {
        c.wr(0xEB51, 0x1E);
        send(c, c.rd(0xC798));
    }
    // 04737
    cpu.r[0] = c.rd(0xEB52);
    if (cpu.r[0] != 0) {
        c.wr(0xEB53, 1);
        cpu.r[2] = c.rd(kIn1);
        cpu.r[1] = c.rd(0xC799);
        if (cpu.r[2] & cpu.r[1]) {
            c.wr(0xEB52, 0x11D);
            return;
        }
        cpu.r[0] -= c.rd(0xC92F);
        if (Ctx::s32(cpu.r[0]) < 0) cpu.r[0] = 0;
        c.wr(0xEB52, cpu.r[0]);
        if (Ctx::s32(cpu.r[0]) > 0) return;
        c.call(0x4936);
        cpu.r[0] = c.rd(kIn1) & c.rd(0xC79A);
        if (cpu.r[0] != 0) return;
        send(c, c.rd(0xC79B));
        c.wr(0xEB53, 0);
        return;
    }
    // 0475A
    if (Ctx::s32(c.rd(0xEB50)) <= 0) {
        c.setItem(0x80, 1);
        c.setItem(kItemMotion, 0);
        return;
    }
    // 04774
    cpu.r[0] = c.rd(kIn1);
    cpu.r[1] = c.rd(0xC7A0);
    if (cpu.r[0] & cpu.r[1]) {
        c.wr(0xEB52, 0x11D);
        c.wr(0xEB53, 1);
        cpu.r[2] = (cpu.r[0] >> 12) & 0xFu;
        cpu.r[3] = (cpu.r[2] >= 3 && cpu.r[2] <= 5) ? 1u : (cpu.r[2] == 6 || cpu.r[2] == 7) ? 2u : 0u;
        c.wr(0xEB54, cpu.r[3]);
        cpu.r[1] = cpu.r[0] >> 16;
        if (cpu.r[0] & 0x8000u) return;  // RETSLO: carry = bit 15 (último bit do LSH −16)
        if (cpu.r[0] & 0x80u) {
            c.wr(0xEB54, 3);
            c.wr(0xEB4F, 1);
            cpu.ar[2] = c.rd(0xC7A1);  // MOTION STOP BUTTON HIT
            cpu.r[2] = tf(256.0);
            cpu.r[3] = tf(310.0);
            cpu.rc = 0xAB;
            c.call(0x7651);
            c.wr(cpu.ar[0] + 8, c.rd(cpu.ar[0] + 8) | 0x100u);
            c.wr(cpu.ar[1] + 8, c.rd(cpu.ar[1] + 8) | 0x100u);
            return;
        }
    }
    // 047A4
    if (c.rd(0xEB4E) != 0) return;
    c.call(0x469D);
    if (!cpu.zero()) return;
    const uint32_t car = cpu.ar[5];
    auto h = [&](uint32_t o) { return f(c.rd(car + o)); };
    const double mean = f(tf(f(tf(f(tf(h(7) + h(0xD))) + h(0x13))) + h(0x19)) * 0.25);
    double r2 = f(tf(f(tf(h(7) + h(0xD))) * 0.5));
    double r3 = h(0x13), r4 = h(0x19);
    r2 = f(tf(mean - r2));  // SUBRF R5,R2 = R5 − R2
    r3 = f(tf(mean - r3));
    r4 = f(tf(mean - r4));
    cpu.r[0] = tf(150.0);
    c.call(0xA585);  // R0 = 1/150
    const double k = f(cpu.r[0]);
    double twice = 0.0;  // R5 fica com 2·R4 (último MPYF3 R4,R1,R5)
    auto curve = [&](double v) {  // (2v − v²·k)·k
        twice = f(tf(v * 2.0));
        return f(tf(f(tf(twice - f(tf(f(tf(v * v)) * k)))) * k));
    };
    r2 = curve(r2);
    r3 = curve(r3);
    r4 = curve(r4);
    auto clamp = [](double v, double lim) { return v > lim ? lim : (v < -lim ? -lim : v); };
    const double lim = shortFloat(0xF666);  // 0.899902
    r2 = clamp(r2, lim);
    r3 = clamp(r3, lim);
    r4 = clamp(r4, lim);
    const double roll = f(tf(h(0x42) * 6.0));
    r3 = f(tf(r3 + roll));
    r4 = f(tf(r4 + roll));
    r2 = f(tf(r2 - roll));
    const double pitch = f(tf(h(0x43) * 6.0));
    r4 = f(tf(r4 + pitch));
    r3 = f(tf(r3 - pitch));
    r2 = clamp(r2, 1.0);
    r3 = clamp(r3, 1.0);
    r4 = clamp(r4, 1.0);
    cpu.r[1] = tf(pitch);
    cpu.r[2] = tf(r2);
    cpu.r[3] = tf(r3);
    cpu.r[4] = tf(r4);
    cpu.r[5] = tf(twice);
    sendPositions(c);
}

/// `0x46A1`/`0x46A3`/`0x46A5`/`0x46A7` → `0x46A8`: "MOTION BURNIN" com os eixos @p axes (IR1: 1 = frente,
/// 2 = traseiro esquerdo, 4 = traseiro direito) oscilando em senoides de passos `[C795..C797]` até ENTER.
void motionBurnin(Ctx& c, uint32_t axes) {
    Tms32031State& cpu = c.cpu;
    cpu.ir1 = axes;
    c.call(0x4693);
    if (!cpu.zero()) return blinkWarning(c, 0xC7D0, 0xC7D1);
    c.call(0x469D);
    if (!cpu.zero()) return blinkWarning(c, 0xC7D2, 0xC7D3);
    c.call(0x4530);
    c.call(0x469D);
    if (!cpu.zero()) return blinkWarning(c, 0xC7D2, 0xC7D3);
    c.call(0x8E79);
    drawOnPage0(c);
    cpu.ar[4] = 0;
    cpu.rc = 0xF;
    c.text(0x32, 0x14A, c.rd(0xC794));  // MOTION BURNIN
    uint32_t a[3] = {kZero, kZero, kZero};  // R5, R6, R7
    const double two_pi = shortFloat(0x2491);
    for (;;) {
        // 046BE
        cpu.r[0] = (c.rd(kIn1) >> 8) << 24;
        if (cpu.r[0] != 0) {
            c.call(0x13D5);
            throw NonLocalJump{0x0699};
        }
        for (int i = 0; i < 3; ++i) {
            if (!(cpu.ir1 & (1u << i))) continue;
            a[i] = tf(f(a[i]) + f(c.rd(0xC795 + i)));
            if (!(f(a[i]) < two_pi)) a[i] = tf(f(a[i]) - two_pi);
        }
        uint32_t s[3];
        for (int i = 2; i >= 0; --i) {  // R4 = sen(R7), R3 = sen(R6), R2 = sen(R5)
            cpu.r[2] = a[i];
            c.call(0xB01A);
            s[i] = cpu.r[0];
        }
        const double k = shortFloat(0xF4CD);  // 0.800049
        cpu.r[2] = tf(f(s[0]) * k);
        cpu.r[3] = tf(f(s[1]) * k);
        cpu.r[4] = tf(f(s[2]) * k);
        sendPositions(c);
        c.waitVblank();
        cpu.r[5] = a[0];
        cpu.r[6] = a[1];
        cpu.r[7] = a[2];
        cpu.r[0] = c.rd(vars::kKeys);
        cpu.r[1] = cpu.r[0];
        if ((cpu.r[1] & keys::kEnter) == 0) return;
    }
}

/// `0x460B` HARD MOTION OFF: reinicia o controlador, espera, envia `[C78D]` depois de 2500 · 5000 voltas.
void hardMotionOff(Ctx& c) {
    resetController(c);
    awaitController(c);
    c.sound(0x03D8);
    delay(c, 0x9C4);
    send(c, c.rd(0xC78D));
}

} // namespace

/// Registra as rotinas da plataforma de movimento.
void registerMotion(SubroutineDispatcher& d, IBusyWait& wait) {
    addSvc(d, wait, 0x4539, setup);
    addSvc(d, wait, 0x4615, motorsOff);
    addSvc(d, wait, 0x4644, setupFailed);
    addSvc(d, wait, 0x4A72, [](Ctx& c) { limitError(c, true); });
    addSvc(d, wait, 0x4A9D, [](Ctx& c) { limitError(c, false); });
    addSvc(d, wait, 0x460B, hardMotionOff);
    addSvc(d, wait, 0x46A1, [](Ctx& c) { motionBurnin(c, 1); });
    addSvc(d, wait, 0x46A3, [](Ctx& c) { motionBurnin(c, 2); });
    addSvc(d, wait, 0x46A5, [](Ctx& c) { motionBurnin(c, 4); });
    addSvc(d, wait, 0x46A7, [](Ctx& c) { motionBurnin(c, 7); });
    addSvc(d, wait, 0x47E8, sendPositions);
    addSvc(d, wait, 0x472C, motionRide);
    addSvc(d, wait, 0x4961, [](Ctx& c) { blinkWarning(c, 0xC7D0, 0xC7D1); });
    addSvc(d, wait, 0x4979, [](Ctx& c) { blinkWarning(c, 0xC7D2, 0xC7D3); });
    addSvc(d, wait, 0x4991, [](Ctx& c) { delay(c, c.cpu.r[0]); });
    addSvc(d, wait, 0x490B, resetController);
    addSvc(d, wait, 0x4A65, awaitController);
    addSvc(d, wait, 0x48C4, sendList);
    addSvc(d, wait, 0x4954, configure);
}

} // namespace rea::core::service
