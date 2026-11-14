/**
 * @file video_routines.cpp
 * @brief Tradução das primitivas de vídeo (`0x8E4E`–`0x8EB6`, `0xA731`–`0xA83F`).
 */
#include "core/video/video_routines.hpp"

#include "core/math/int_math.hpp"
#include "core/tms_float.hpp"
#include "core/vunit_hw.hpp"

#include <cmath>
#include <utility>
#include <vector>

namespace rea::core::video {

using namespace video_vars;

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// Lê com DP = 0 (endereçamento direto na página 0).
uint32_t rd0(Tms32031State& cpu, MemoryBus& bus, uint32_t off) {
    cpu.dp = 0;
    return bus.readWord(cpu.direct(off));
}

/// `LDIU $98,DP / LDI @($0040),R0 / ANDN clear,R0 / OR set,R0 / STI R0,@($0040) / SUBI DP,DP`.
void setPageControl(Tms32031State& cpu, MemoryBus& bus, uint32_t clear, uint32_t set) {
    cpu.r[0] = bus.readWord(vunit::kPageControl);
    cpu.r[0] &= ~clear;
    cpu.r[0] |= set;
    bus.writeWord(vunit::kPageControl, cpu.r[0]);
    cpu.dp = 0;
}

/// Trecho traduzido da ROM em `0x8E65`.
void selectPage1Both(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[0] = rd0(cpu, bus, kPage1Ptr);
    bus.writeWord(cpu.direct(kDrawBase), cpu.r[0]);
    setPageControl(cpu, bus, 0, 0x5);
}

/// Trecho traduzido da ROM em `0x8E4E`.
void drawPage1ShowPage0(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[0] = rd0(cpu, bus, kForcePage1);
    if (cpu.r[0] != 0) { selectPage1Both(cpu, bus); return; }
    cpu.r[0] = rd0(cpu, bus, kPage1Ptr);
    bus.writeWord(cpu.direct(kDrawBase), cpu.r[0]);
    setPageControl(cpu, bus, 0x1, 0x4);
}

/// Trecho traduzido da ROM em `0x8E59`.
void drawPage0ShowPage1(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[0] = rd0(cpu, bus, kForcePage1);
    if (cpu.r[0] != 0) { selectPage1Both(cpu, bus); return; }
    cpu.r[0] = rd0(cpu, bus, kPage0Ptr);
    bus.writeWord(cpu.direct(kDrawBase), cpu.r[0]);
    // 08E5F  OR 1; ANDN 4
    cpu.r[0] = bus.readWord(vunit::kPageControl) | 0x1u;
    cpu.r[0] &= ~0x4u;
    bus.writeWord(vunit::kPageControl, cpu.r[0]);
    cpu.dp = 0;
}

/// Padrão `PUSH AR2 / PUSH R3 / AR2 = base / R3 = n / PUSH R2 / R2 = cor / CALL $8E98 / POP R2, R3, AR2`.
void fillArea(Tms32031State& cpu, MemoryBus& bus, uint32_t base, uint32_t countM1, uint32_t color) {
    const uint32_t ar2 = cpu.ar[2], r3 = cpu.r[3], r2 = cpu.r[2];
    cpu.ar[2] = base;
    cpu.r[3] = countM1;
    cpu.r[2] = color;
    fillWords(cpu, bus);
    cpu.r[2] = r2;
    cpu.r[3] = r3;
    cpu.ar[2] = ar2;
}

/// Trecho traduzido da ROM em `0x8E7D`.
void clearPage0(Tms32031State& cpu, MemoryBus& bus) {
    fillArea(cpu, bus, rd0(cpu, bus, kPage0Ptr), rd0(cpu, bus, kPageWordsM1Ptr), 0);
}

/// Trecho traduzido da ROM em `0x8E79`.
void clearBothPages(Tms32031State& cpu, MemoryBus& bus) {
    clearPage0(cpu, bus);
    fillArea(cpu, bus, rd0(cpu, bus, kPage1Ptr), rd0(cpu, bus, kPageWordsM1Ptr), 0);
}

/// Trecho traduzido da ROM em `0x8EB1`.
void clearPalette(Tms32031State& cpu, MemoryBus& bus) {
    cpu.ar[0] = 0x009Eu << 16;
    cpu.r[1] = 0;
    for (uint32_t i = 0; i < vunit::kPaletteWords; ++i) bus.writeWord(cpu.ar[0]++, cpu.r[1]);
    cpu.rc = 0xFFFFFFFFu;
}

/// Pacote de caracteres empilhados (último empilhado = primeiro do texto) em palavras de 4 bytes.
uint32_t packChars(Tms32031State& cpu, MemoryBus& bus, const std::vector<uint32_t>& stack, uint32_t prefix) {
    // 07A783/0A7C4  R6 = 0; R1 = 0 (ou '>' com R6 = 8)
    cpu.r[6] = prefix ? 8u : 0u;
    cpu.r[1] = prefix;
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        cpu.r[0] = tmsLsh(*it, cpu.r[6]);
        cpu.r[1] |= cpu.r[0];
        cpu.r[6] += 8;
        if (s(cpu.r[6]) >= 32) {
            bus.writeWord(cpu.ar[2]++, cpu.r[1]);
            cpu.r[1] = 0;
            cpu.r[6] = 0;
        }
    }
    // 0A794  completa a última palavra (terminador)
    bus.writeWord(cpu.ar[2], cpu.r[1]);
    return cpu.r[6];
}

} // namespace

void submitQuad(Tms32031State& cpu, MemoryBus& bus) {
    // 0AECC  grava o retângulo no modelo (DP = 0)
    cpu.dp = 0;
    auto st = [&](uint32_t off, uint32_t v) { bus.writeWord(cpu.direct(off), v); };
    st(0xEA35, cpu.rs);   // cor base
    st(0xEA46, cpu.re);   // página de textura
    st(0xEA36, cpu.ar[2]);
    st(0xEA3F, cpu.ar[2]);
    st(0xEA37, cpu.r[2]);
    st(0xEA3A, cpu.r[2]);
    st(0xEA39, cpu.r[3]);
    st(0xEA3C, cpu.r[3]);
    st(0xEA3D, cpu.rc);
    st(0xEA40, cpu.rc);
    sendQuadTemplate(cpu, bus);
}

void sendQuadTemplate(Tms32031State& cpu, MemoryBus& bus) {
    // 0AED6  PUSH AR0, AR1, R0, R1, RC, RS, RE; espera [0x980082] & 4 == 0 (duas vezes)
    const uint32_t ar0 = cpu.ar[0], ar1 = cpu.ar[1], r0 = cpu.r[0], r1 = cpu.r[1], rc = cpu.rc, rs = cpu.rs, re = cpu.re;
    for (int i = 0; i < 2; ++i) {
        do { cpu.r[0] = bus.readWord(vunit::kVideoStatus) & 0x4u; } while (cpu.r[0] != 0);
    }
    // 0AEE7  15 palavras do modelo [0x48] para a fila 0x600000 (pula os offsets 4, 7, 10 e 13)
    cpu.ar[0] = 0x0060u << 16;
    cpu.ar[1] = bus.readWord(cpu.direct(kQuadTemplatePtr));
    for (uint32_t off : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u, 11u, 12u, 14u, 15u, 16u, 17u, 18u}) {
        cpu.r[0] = bus.readWord(cpu.ar[1] + off);
        bus.writeWord(cpu.ar[0], cpu.r[0]);
    }
    cpu.ar[1] += 19;
    // 0AEF9  dispara (leitura de 0x980083); leitura descartada de [0]
    cpu.r[0] = bus.readWord(0x980083);
    cpu.ar[3] = 0;
    cpu.r[0] = bus.readWord(cpu.ar[3]);
    cpu.re = re;
    cpu.rs = rs;
    cpu.rc = rc;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
}

void clearRenderPage(Tms32031State& cpu, MemoryBus& bus) {
    // 08E6D  [C940] != 0 → RETS
    cpu.r[0] = bus.readWord(cpu.direct(0xC940));
    if (cpu.r[0] != 0) return;
    // 08E6F  modo 0 (liso); retângulo 0..0x1FF × 0..0x18F, cor 0, textura 0
    cpu.ar[2] = 0;
    bus.writeWord(cpu.direct(0xEA34), cpu.ar[2]);
    cpu.r[2] = 0;
    cpu.r[3] = 0x01FF;
    cpu.rc = 0x018F;
    cpu.rs = 0;
    cpu.re = 0x00A0u >> 16;  // LDI $A0,RE / LSH −16,RE = 0
    submitQuad(cpu, bus);
}

void fillWords(Tms32031State& cpu, MemoryBus& bus) {
    // 08E98  PUSH R0, AR1, AR2, IE; IE = [8099DF]; PUSH DP
    const uint32_t r0 = cpu.r[0], ar1 = cpu.ar[1], ar2 = cpu.ar[2], ie = cpu.ie;
    cpu.ie = bus.readWord(0x800000u | kFillIeMask);
    const uint32_t dp = 0;  // 08E9E  SUBI DP,DP antes do PUSH DP
    cpu.dp = 0x80;
    cpu.ar[1] = 0;
    cpu.rc = cpu.r[3];
    cpu.r[0] = kBusWait;
    bus.writeWord(vunit::kBusControl, cpu.r[0]);
    // 08EA5  RPTB: *AR2++ = R2 (RC + 1 vezes)
    for (;;) {
        bus.writeWord(cpu.ar[2]++, cpu.r[2]);
        if (s(--cpu.rc) < 0) break;
    }
    // 08EA7  leitura descartada de [0]; barramento normal
    cpu.r[0] = bus.readWord(cpu.ar[1]);
    cpu.r[0] = kBusNormal;
    bus.writeWord(vunit::kBusControl, cpu.r[0]);
    cpu.dp = dp;
    cpu.ie = ie;
    cpu.ar[2] = ar2;
    cpu.ar[1] = ar1;
    cpu.r[0] = r0;
}

void plotPixel(Tms32031State& cpu, MemoryBus& bus) {
    // 0A824  PUSH AR1, AR2, R2, IE; desliga o GIE; [808064] = 0x1000
    const uint32_t ar1 = cpu.ar[1], ar2 = cpu.ar[2], r2 = cpu.r[2];
    cpu.ar[1] = kBusWait;
    cpu.st &= 0xDFFFu;  // AND $DFFF,ST (GIE = 0)
    bus.writeWord(vunit::kBusControl, cpu.ar[1]);
    // 0A830  AR2 = [CE13] + x + (y << 9)
    cpu.dp = 0;
    cpu.ar[1] = 0;
    cpu.r[2] <<= 9;
    cpu.ar[2] += bus.readWord(cpu.direct(kDrawBase));
    cpu.ar[2] += cpu.r[2];
    cpu.dp = 0x80;
    bus.writeWord(cpu.ar[2], cpu.r[3]);
    // 0A837  leitura descartada de [0]; barramento normal; CALL $A731 (RETIU liga o GIE)
    cpu.r[2] = bus.readWord(cpu.ar[1]);
    cpu.r[2] = kBusNormal;
    bus.writeWord(vunit::kBusControl, cpu.r[2]);
    cpu.dp = 0;
    cpu.st |= Tms32031State::kStGie;
    cpu.r[2] = r2;
    cpu.ar[2] = ar2;
    cpu.ar[1] = ar1;
}

void fillRect(Tms32031State& cpu, MemoryBus& bus) {
    // 0A7E5  PUSH AR2
    const uint32_t ar2 = cpu.ar[2];
    for (;;) {
        // 0A7E6  pixel com a cor RS; AR2++ enquanto AR2 ≤ R3
        do {
            const uint32_t r3 = cpu.r[3];
            cpu.r[3] = cpu.rs;
            plotPixel(cpu, bus);
            cpu.r[3] = r3;
            cpu.ar[2] += 1;
        } while (s(cpu.r[3] - cpu.ar[2]) >= 0);
        // 0A7ED  POP AR2; PUSH AR2; R2++ enquanto R2 ≤ RC
        cpu.ar[2] = ar2;
        cpu.r[2] += 1;
        if (s(cpu.rc - cpu.r[2]) < 0) break;
    }
    cpu.ar[2] = ar2;
}

void drawText(Tms32031State& cpu, MemoryBus& bus) {
    // 0A7F4  PUSH R4, R5; RS = 3; SUBI RS,RS (RS = 0)
    const uint32_t r4 = cpu.r[4], r5 = cpu.r[5];
    cpu.rs = 0;
    for (;;) {
        // 0A7F8  terminou a palavra: próxima
        if (cpu.rs == static_cast<uint32_t>(-32)) {
            cpu.rs = 0;
            cpu.ar[2] += 1;
        }
        // 0A7FC  AR0 = byte atual
        cpu.ar[0] = tmsLsh(bus.readWord(cpu.ar[2]), cpu.rs);
        cpu.rs -= 8;
        cpu.ar[0] &= 0xFFu;
        if (cpu.ar[0] == 0) break;
        if (cpu.ar[0] != 0x20) {
            // 0A804  AR0 = fonte + (c − 0x28) · 7; 7 linhas × 8 colunas
            cpu.ar[0] -= kFirstGlyph;
            cpu.ar[0] = tmsMpyi(cpu.ar[0], kGlyphRows);
            cpu.ar[0] += bus.readWord(cpu.direct(kFontPtr));
            cpu.r[5] = kGlyphRows;
            do {
                cpu.r[1] = bus.readWord(cpu.ar[0]++) << 24;
                cpu.r[4] = 8;
                do {
                    // 0A80B  ASH 1: o bit que sai vai para o carry; BHS pula com carry 0
                    const bool on = (cpu.r[1] & 0x80000000u) != 0;
                    cpu.r[1] <<= 1;
                    if (on) {
                        const uint32_t ar2 = cpu.ar[2], r2 = cpu.r[2], r3 = cpu.r[3];
                        cpu.ar[2] = cpu.r[2];
                        cpu.r[2] = cpu.r[3];
                        cpu.r[3] = cpu.rc;
                        plotPixel(cpu, bus);
                        cpu.r[3] = r3;
                        cpu.r[2] = r2;
                        cpu.ar[2] = ar2;
                    }
                    cpu.r[2] += 1;
                } while (s(--cpu.r[4]) > 0);
                cpu.r[2] -= 8;
                cpu.r[3] += 1;
            } while (s(--cpu.r[5]) > 0);
            cpu.r[3] -= kGlyphRows;
        }
        // 0A81F  próximo caractere: x += 8
        cpu.r[2] += 8;
    }
    // 0A821  POP R5, R4
    cpu.r[5] = r5;
    cpu.r[4] = r4;
}

void formatDecimal(Tms32031State& cpu, MemoryBus& bus, ISubroutines&, bool pad2) {
    // 0A762/0A74E  PUSH R0–R3, AR0, AR1, AR2, AR7, R6, R7
    const Tms32031State saved = cpu;
    cpu.r[7] = 0;
    if (cpu.r[2] == 0) {
        // 0A7A8 / 0A7A4  "0" (8 bits) ou "00" (16 bits)
        bus.writeWord(cpu.ar[2], pad2 ? 0x3030u : 0x30u);
        const uint32_t bits = pad2 ? 0x10u : 0x08u;
        cpu = saved;
        cpu.r[0] = bits;
        return;
    }
    if (s(cpu.r[2]) < 0) cpu.r[7] = 1;
    if (!pad2) cpu.ar[7] = 0;
    cpu.r[2] = s(cpu.r[2]) < 0 ? 0u - cpu.r[2] : cpu.r[2];
    cpu.r[3] = 0;
    if (pad2 && s(cpu.r[2]) <= 9) cpu.ar[7] = 1;  // 0A75E
    // 0A773  dígitos do menos significativo para o mais
    std::vector<uint32_t> stack;
    do {
        cpu.r[0] = math::modI(cpu.r[2], 10) | 0x30u;
        stack.push_back(cpu.r[0]);
        cpu.r[3] += 1;
        cpu.r[0] = math::divI(cpu.r[2], 10);
        cpu.r[2] = cpu.r[0];
    } while (s(cpu.r[2]) > 0);
    // 0A77E  AR7 == 1 → '0' à esquerda
    if (cpu.ar[7] == 1) {
        stack.push_back(0x30);
        cpu.r[3] += 1;
    }
    const uint32_t bits = packChars(cpu, bus, stack, cpu.r[7] ? 0x3Eu : 0u);
    cpu = saved;
    cpu.r[0] = bits;
}

void formatHex(Tms32031State& cpu, MemoryBus& bus) {
    // 0A7AC  PUSH R0–R3, AR0, AR1, AR2, AR7, R6, R7
    const Tms32031State saved = cpu;
    if (cpu.r[2] == 0) {
        bus.writeWord(cpu.ar[2], 0x30u);  // 0A7E1
        cpu = saved;
        cpu.r[0] = 8;
        return;
    }
    std::vector<uint32_t> stack;
    do {
        // 0A7B9  nibble → '0'–'9' ou 'A'–'F'
        cpu.r[0] = cpu.r[2] & 0xFu;
        cpu.r[0] += s(cpu.r[0]) >= 10 ? 0x37u : 0x30u;
        stack.push_back(cpu.r[0]);
        cpu.r[2] >>= 4;  // LSH −4; BGT
    } while (s(cpu.r[2]) > 0);
    const uint32_t bits = packChars(cpu, bus, stack, 0);
    cpu = saved;
    cpu.r[0] = bits;
}

namespace {

/// `A585` + `RND`: recíproco arredondado para 32 bits.
double recip(int32_t v) { return tmsRound(1.0 / static_cast<double>(v)); }

/// `FIX`: piso para inteiro.
int32_t fix(double v) { return static_cast<int32_t>(std::floor(tmsToDouble(doubleToTms(v)))); }

} // namespace

void drawLine(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 0AC23  quadro na pilha: +1 cor (RS), +2 x0 (AR2), +3 y0 (R2); RS = x1 (R3); RC = y1
    const uint32_t r4 = cpu.r[4], r5 = cpu.r[5], r6 = cpu.r[6], r7 = cpu.r[7], ar3 = cpu.ar[3], dp = cpu.dp;
    const uint32_t color = cpu.rs;
    int32_t x0 = static_cast<int32_t>(cpu.ar[2]), y0 = static_cast<int32_t>(cpu.r[2]);
    int32_t x1 = static_cast<int32_t>(cpu.r[3]), y1 = static_cast<int32_t>(cpu.rc);
    // Saída: RS = x final (a troca da linha íngreme não mexe em RS); RC = y final (as trocas da
    // vertical e da íngreme deixam o maior y em RC).
    uint32_t rs_out = cpu.r[3], rc_out = cpu.rc;
    auto plot = [&](int32_t x, int32_t y) {
        cpu.ar[2] = static_cast<uint32_t>(x);
        cpu.r[2] = static_cast<uint32_t>(y);
        cpu.r[3] = color;
        subs.call(0xA824, cpu, bus);
    };
    if (x0 == x1) {
        // 0ACB2  vertical
        if (y0 > y1) std::swap(y0, y1);
        rc_out = static_cast<uint32_t>(y1);
        for (int32_t y = y0; y <= y1; ++y) plot(x0, y);
    } else if (y0 == y1) {
        // 0ACA1  horizontal
        if (x1 < x0) std::swap(x0, x1);
        rs_out = static_cast<uint32_t>(x1);
        for (int32_t x = x0; x <= x1; ++x) plot(x, y0);
    } else {
        // 0AC33  inclinação dx/dy (SUBI3 a,b = a − b)
        double slope = static_cast<double>(x1 - x0) * recip(y1 - y0);
        if (!(slope > 1.0) && !(slope < -1.0)) {
            // 0AC40  percorre y (troca as pontas se y1 < y0)
            if (y1 < y0) {
                std::swap(x0, x1);
                std::swap(y0, y1);
                slope = static_cast<double>(x1 - x0) * recip(y1 - y0);
            }
            rc_out = static_cast<uint32_t>(y1);
            double x = static_cast<double>(x0);
            for (int32_t y = y0; y <= y1; ++y) {
                plot(fix(x), y);
                x += slope; // slot atrasado do BGED
            }
        } else {
            // 0AC6D  percorre x
            if (x1 < x0) {
                std::swap(x0, x1);
                std::swap(y0, y1);
            }
            rs_out = static_cast<uint32_t>(x1);
            const double dy = static_cast<double>(y1 - y0) * recip(x1 - x0);
            double y = static_cast<double>(y0);
            for (int32_t x = x0; x <= x1; ++x) {
                plot(x, fix(y));
                y += dy;
            }
        }
    }
    cpu.rs = rs_out;
    cpu.rc = rc_out;
    cpu.r[4] = r4; cpu.r[5] = r5; cpu.r[6] = r6; cpu.r[7] = r7; cpu.ar[3] = ar3; cpu.dp = dp;
}

void drawRect(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 0ABFC  quadro: +1 cor (RS), +2 x0 (AR2), +3 x1 (R3); R5 = y0, R4 = y1
    const uint32_t r4 = cpu.r[4], r5 = cpu.r[5], dp = cpu.dp, ar3 = cpu.ar[3];
    const uint32_t color = cpu.rs, x0 = cpu.ar[2], x1 = cpu.r[3], y0 = cpu.r[2], y1 = cpu.rc;
    auto line = [&](uint32_t ax, uint32_t ay, uint32_t bx, uint32_t by) {
        cpu.ar[2] = ax; cpu.r[2] = ay; cpu.r[3] = bx; cpu.rc = by; cpu.rs = color;
        drawLine(cpu, bus, subs);
    };
    line(x0, y0, x1, y0);  // topo
    line(x0, y1, x1, y1);  // base
    line(x0, y0, x0, y1);  // esquerda
    line(x1, y0, x1, y1);  // direita
    cpu.r[4] = r4; cpu.r[5] = r5; cpu.dp = dp; cpu.ar[3] = ar3;
}

/// Registra as rotinas de vídeo no despachante.
void registerRoutines(SubroutineDispatcher& d) {
    using S = ISubroutines;
    d.add(0x8E4E, [](Tms32031State& c, MemoryBus& b, S&) { drawPage1ShowPage0(c, b); });
    d.add(0x8E59, [](Tms32031State& c, MemoryBus& b, S&) { drawPage0ShowPage1(c, b); });
    d.add(0x8E65, [](Tms32031State& c, MemoryBus& b, S&) { selectPage1Both(c, b); });
    d.add(0x8E79, [](Tms32031State& c, MemoryBus& b, S&) { clearBothPages(c, b); });
    d.add(0x8E7D, [](Tms32031State& c, MemoryBus& b, S&) { clearPage0(c, b); });
    d.add(0x8E88, [](Tms32031State& c, MemoryBus& b, S&) {
        fillArea(c, b, rd0(c, b, kPageTable + 0), 0x3FF, 0);  // 08E81 zera R2
    });
    d.add(0x8E8D, [](Tms32031State& c, MemoryBus& b, S&) {
        fillArea(c, b, rd0(c, b, kPageTable + 1), 0x3FF, 0);
    });
    d.add(0x8E92, [](Tms32031State& c, MemoryBus& b, S&) {
        // 08E92  AR2 = página 0; R3 = [8E96]; R2 = [8E97]; BU $8E98 (sem salvar AR2/R2/R3)
        c.ar[2] = rd0(c, b, kPage0Ptr);
        c.r[3] = rd0(c, b, kFillCountPtr);
        c.r[2] = rd0(c, b, kFillColorPtr);
        fillWords(c, b);
    });
    d.add(0x8E98, [](Tms32031State& c, MemoryBus& b, S&) { fillWords(c, b); });
    d.add(0xAECC, [](Tms32031State& c, MemoryBus& b, S&) { submitQuad(c, b); });
    d.add(0xAED6, [](Tms32031State& c, MemoryBus& b, S&) { sendQuadTemplate(c, b); });
    d.add(0x8E6D, [](Tms32031State& c, MemoryBus& b, S&) { clearRenderPage(c, b); });
    d.add(0x8EB1, [](Tms32031State& c, MemoryBus& b, S&) { clearPalette(c, b); });
    d.add(0xA731, [](Tms32031State& c, MemoryBus&, S&) { c.st |= Tms32031State::kStGie; });  // RETIU
    d.add(0xA824, [](Tms32031State& c, MemoryBus& b, S&) { plotPixel(c, b); });
    d.add(0xAC23, drawLine);
    d.add(0xABFC, drawRect);
    d.add(0xA7E5, [](Tms32031State& c, MemoryBus& b, S&) { fillRect(c, b); });
    d.add(0xA7F4, [](Tms32031State& c, MemoryBus& b, S&) { drawText(c, b); });
    d.add(0xA762, [](Tms32031State& c, MemoryBus& b, S& s) { formatDecimal(c, b, s, false); });
    d.add(0xA74E, [](Tms32031State& c, MemoryBus& b, S& s) { formatDecimal(c, b, s, true); });
    d.add(0xA7AC, [](Tms32031State& c, MemoryBus& b, S&) { formatHex(c, b); });
}

} // namespace rea::core::video
