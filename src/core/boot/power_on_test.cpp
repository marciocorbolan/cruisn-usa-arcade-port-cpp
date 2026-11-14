/**
 * @file power_on_test.cpp
 * @brief Tradução de `0x62D5`–`0x656B` (autoteste de energização), `0x503C` e `0x065F`.
 */
#include "core/boot/power_on_test.hpp"

#include "core/busy_wait.hpp"
#include "core/vunit_hw.hpp"

namespace rea::core::boot {

namespace {

constexpr uint32_t kControlShadow = 0xC8C7;  ///< Cópia de 0x994000 (bit 7 = LED, bit 3 = watchdog).
constexpr uint32_t kRamTestCopy = 0xC770;    ///< [C770] = 0x809D00: cópia da 0x64FE na RAM interna.
constexpr uint32_t kSeed = 0x61C1;           ///< [61C1] = 0x5A5A5A5A.
constexpr uint32_t kBlinkPause = 0x7A1200u + 1u;   ///< `RPTS 0x7A1200` de NOP: ~0,32 s a 25 MIPS
constexpr uint32_t kDigitPause = 0x2160EC0u + 1u;  ///< `RPTS 0x2160EC0` de NOP entre dígitos: ~1,4 s

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// Próximo valor da sequência do teste: `R1 = x; x <<= 1; R1 ^= x; LDILT 2,R6; x |= R6`.
inline uint32_t nextPattern(uint32_t x) {
    const uint32_t shifted = x << 1;
    return shifted | ((s(x ^ shifted) < 0) ? 2u : 0u);
}

/// `0x503C`: inverte o bit 3 (watchdog) de `[C8C7]` e grava em 0x994000 com bus control 0x1068.
void kickWatchdogSlow(Tms32031State& cpu, MemoryBus& bus) {
    const uint32_t busctl = bus.readWord(vunit::kBusControl);
    bus.writeWord(vunit::kBusControl, 0x1068);
    const uint32_t v = bus.readWord(kControlShadow) ^ 0x8u;
    bus.writeWord(kControlShadow, v);
    bus.writeWord(vunit::kControl, v);
    bus.writeWord(vunit::kBusControl, busctl);
    (void)cpu;  // R0 e DP salvos/restaurados pela rotina original
}

/**
 * @brief `0x64FE` (executada da cópia em 0x809D00): teste de RAM por sequência pseudoaleatória.
 * @details Entrada: AR2 = início, R2 = palavras, R3 = máscara, RC = passadas, RS = bus control,
 *          BK = passo. Saída: R0 = 0 (passou) ou 1 (falhou). Preserva DP, R6 e AR6.
 */
void ramTest(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r6 = cpu.r[6], ar6 = cpu.ar[6], dp = cpu.dp;
    // 06502  CALL $6506 → BU [6505] = 0x503C (watchdog)
    subs.call(0x503C, cpu, bus);
    // 0650B  guarda o bus control e usa RS
    const uint32_t busctl = bus.readWord(vunit::kBusControl);
    bus.writeWord(vunit::kBusControl, cpu.rs);
    cpu.ar[3] = 0;
    cpu.r[4] = bus.readWord(kSeed);
    cpu.ar[0] = cpu.rc - 1;
    cpu.r[2] -= 1;
    cpu.ar[4] = cpu.r[2];
    bool fail = false;
    for (;;) {
        // 06517  gravação: o corpo roda até o DBUD achar AR4 < 0 (AR4 não é recarregado nas passadas
        //        seguintes — com AR4 = −1 grava só uma palavra; quirk preservado)
        cpu.ar[1] = cpu.ar[2];
        uint32_t x = cpu.r[4];
        do {
            x = nextPattern(x);
            bus.writeWord(cpu.ar[1], x);
            cpu.ar[1] += cpu.bk;
            cpu.ar[4] -= 1;
        } while (s(cpu.ar[4]) >= 0);
        // 06523  conferência
        cpu.ar[1] = cpu.ar[2];
        x = cpu.r[4];
        cpu.r[7] = 0;
        cpu.ar[4] = cpu.r[2];
        do {
            x = nextPattern(x);
            cpu.r[7] += 1;
            cpu.r[1] = bus.readWord(cpu.ar[1]) & cpu.r[3];
            cpu.ar[1] += cpu.bk;
            cpu.r[4] = x;
            if ((x & cpu.r[3]) != cpu.r[1]) {
                fail = true;
                break;
            }
            cpu.ar[4] -= 1;
        } while (s(cpu.ar[4]) >= 0);
        if (fail) break;
        // 0653D  DBU AR0,$6517
        cpu.ar[0] -= 1;
        if (s(cpu.ar[0]) < 0) break;
    }
    cpu.r[0] = fail ? 1u : 0u;
    // 0653F  restaura o bus control (POP R1)
    cpu.r[1] = busctl;
    bus.writeWord(vunit::kBusControl, busctl);
    cpu.ar[6] = ar6;
    cpu.r[6] = r6;
    cpu.dp = dp;
}

/// `0x64FC`: `LDI ($C770),R0 / BU R0` → a cópia da 0x64FE em 0x809D00.
void ramTestEntry(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = bus.readWord(kRamTestCopy);
    ramTest(cpu, bus, subs);
}

/// `0x6548`: copia `[C772]`..`[C771]` (0x64FE–0x6547) para `[C773]` = 0x809D00.
void copyRamTest(Tms32031State& cpu, MemoryBus& bus) {
    cpu.ar[0] = bus.readWord(0xC771);
    cpu.ar[1] = bus.readWord(0xC772);
    cpu.ar[0] = cpu.ar[0] - cpu.ar[1] - 1;
    cpu.ar[2] = bus.readWord(0xC773);
    cpu.rc = cpu.ar[0];
    for (uint32_t i = 0; i <= cpu.ar[0]; ++i) {
        cpu.r[0] = bus.readWord(cpu.ar[1]++);
        bus.writeWord(cpu.ar[2]++, cpu.r[0]);
    }
    cpu.rc = 0xFFFFFFFFu;
}

/**
 * @brief `0x6552`: R0 = Σ ((palavra & R2) LSH R3) em 16 bits, sobre RC palavras a partir de AR2.
 * @details Preserva R1, RC e AR2. Antes chama `[6559]` = 0x5029 (watchdog).
 */
void romChecksum(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r1 = cpu.r[1], rc = cpu.rc, ar2 = cpu.ar[2];
    subs.call(0x5029, cpu, bus);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < rc; ++i) sum += tmsLsh(bus.readWord(cpu.ar[2]++) & cpu.r[2], cpu.r[3]);
    cpu.r[0] = sum & 0xFFFFu;  // 06566  LSH 16 / LSH −16
    cpu.ar[2] = ar2;
    cpu.rc = rc;
    cpu.r[1] = r1;
}

/// `0x62F7`: dígitos do rótulo "Unnn" em `[*+AR4(5)]` → R1, R2, R3 (0 quando o caractere falta).
void labelDigits(Tms32031State& cpu, MemoryBus& bus) {
    cpu.ar[0] = bus.readWord(bus.readWord(cpu.ar[4] + 5));
    const uint32_t w = cpu.ar[0];
    cpu.r[1] = ((w >> 8) & 0xFFu) - 0x30u;
    cpu.r[2] = (w >> 16) & 0xFFu;
    cpu.r[3] = (w >> 24) & 0xFFu;
    if (cpu.r[2] == 0) {
        cpu.r[3] = 0;  // 0630A  SUBI R2,R2 / SUBI R3,R3
        return;
    }
    cpu.r[2] -= 0x30u;
    if (cpu.r[3] == 0) return;
    cpu.r[3] -= 0x30u;
}

/// `0x6321` / `0x6328`: LED (bit 7 de `[C8C7]`) apagado / aceso.
void setLed(MemoryBus& bus, bool on) {
    uint32_t v = bus.readWord(kControlShadow);
    v = on ? (v | 0x80u) : (v & ~0x80u);
    bus.writeWord(kControlShadow, v);
    bus.writeWord(vunit::kControl, v);
}

/**
 * @brief `0x632F`: pisca R1, R2 e R3 vezes no LED, com pausas.
 * @details Cada piscada: LED apagado, `RPTS 0x7A1200` de NOP, aceso, outra pausa igual; depois de cada
 *          dígito, `RPTS 0x2160EC0`. Com @p wait as pausas passam como tempo de quadros (sem alimentar o
 *          watchdog); sem ele (testes) só as escritas no LED são feitas. Preserva R0–R3 e AR4.
 * @return false se a espera foi interrompida (RESET pedido ou fim da execução).
 */
bool blinkCode(Tms32031State& cpu, MemoryBus& bus, IBusyWait* wait = nullptr) {
    auto pause = [&](uint32_t n) { return !wait || wait->spin(cpu, n); };
    const uint32_t digits[3] = {cpu.r[1], cpu.r[2], cpu.r[3]};
    for (uint32_t n : digits) {
        for (int32_t i = s(n) - 1; i >= 0; --i) {
            setLed(bus, false);
            if (!pause(kBlinkPause)) return false;
            setLed(bus, true);
            if (!pause(kBlinkPause)) return false;
        }
        if (!pause(kDigitPause)) return false;
    }
    cpu.rc = 0xFFFFFFFFu;  // RC termina em −1 após o último RPTS
    return true;
}

/// `0x630D`: falha não fatal — pisca o código do rótulo e volta (preserva os registradores).
void blinkLabel(Tms32031State& cpu, MemoryBus& bus, IBusyWait* wait = nullptr) {
    const Tms32031State saved = cpu;
    labelDigits(cpu, bus);
    blinkCode(cpu, bus, wait);
    cpu.rs = saved.rs;
    cpu.re = saved.re;
    cpu.rc = saved.rc;
    cpu.ar[0] = saved.ar[0];
    cpu.r[1] = saved.r[1];
    cpu.r[2] = saved.r[2];
    cpu.r[3] = saved.r[3];
}

/// Parâmetros de teste de RAM a partir de `*+AR4(0)`: AR2, R2, R3, RC, RS, BK (AR0 avança 6).
void loadRamParams(Tms32031State& cpu, MemoryBus& bus) {
    cpu.ar[0] = bus.readWord(cpu.ar[4]);
    cpu.ar[2] = bus.readWord(cpu.ar[0]++);
    cpu.r[2] = bus.readWord(cpu.ar[0]++);
    cpu.r[3] = bus.readWord(cpu.ar[0]++);
    cpu.rc = bus.readWord(cpu.ar[0]++);
    cpu.rs = bus.readWord(cpu.ar[0]++);
    cpu.bk = bus.readWord(cpu.ar[0]++);
}

/// `CALL $64FC` com PUSH/POP de AR0, AR4 (e AR5, AR6 quando @p keep56).
void callRamTest(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, bool keep56) {
    const uint32_t ar0 = cpu.ar[0], ar4 = cpu.ar[4], ar5 = cpu.ar[5], ar6 = cpu.ar[6];
    subs.call(0x64FC, cpu, bus);
    cpu.ar[0] = ar0;
    cpu.ar[4] = ar4;
    if (keep56) {
        cpu.ar[5] = ar5;
        cpu.ar[6] = ar6;
    }
}

/// Rótulo `*+AR4(5)` centrado no retângulo da entrada AR4, cor RC (`0x63C1`–`0x63CD` e similares).
void drawLabel(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t color) {
    const uint32_t x0 = bus.readWord(cpu.ar[4] + 1), y0 = bus.readWord(cpu.ar[4] + 2);
    const uint32_t x1 = bus.readWord(cpu.ar[4] + 3), y1 = bus.readWord(cpu.ar[4] + 4);
    cpu.ar[2] = bus.readWord(cpu.ar[4] + 5);
    cpu.r[2] = tmsLsh(x1 - x0, 0x7Fu) + x0 - 0xF;  // LSH −1 (lógico)
    cpu.r[3] = tmsLsh(y1 - y0, 0x7Fu) + y0 - 4;
    cpu.rc = color;
    subs.call(0xA7F4, cpu, bus);
}

/// Caixa interna do resultado (`0xA7E5`, cor RS) + rótulo em cor 8 (`0x63B8`–`0x63CD`).
void drawResult(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[2] = bus.readWord(cpu.ar[4] + 1) + 1;
    cpu.r[2] = bus.readWord(cpu.ar[4] + 2) + 1;
    cpu.r[3] = bus.readWord(cpu.ar[4] + 3) - 1;
    cpu.rc = bus.readWord(cpu.ar[4] + 4) - 1;
    subs.call(0xA7E5, cpu, bus);
    drawLabel(cpu, bus, subs, 8);
}

/// Moldura (`0xABFC`, RS = 1) + rótulo em cor 0xB (`0x63D3`–`0x63E7`).
void drawFrame(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[2] = bus.readWord(cpu.ar[4] + 1);
    cpu.r[2] = bus.readWord(cpu.ar[4] + 2);
    cpu.r[3] = bus.readWord(cpu.ar[4] + 3);
    cpu.rc = bus.readWord(cpu.ar[4] + 4);
    cpu.rs = 1;
    const uint32_t dp = cpu.dp;
    subs.call(0xABFC, cpu, bus);
    cpu.dp = dp;
    drawLabel(cpu, bus, subs, 0xB);
}

/// `0x62D5`: testa as 4 RAMs rápidas (U56/U57/U59/U60); falha = pisca o rótulo no LED e trava.
void powerOnFastRam(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, IBusyWait* wait = nullptr) {
    // 062D5  desliga GIE (com IE = 0 durante a troca)
    cpu.st &= ~Tms32031State::kStGie;
    cpu.dp = 0;
    subs.call(0x6548, cpu, bus);
    cpu.ar[5] = bus.readWord(0xC761);
    cpu.ar[6] = 3;
    cpu.ar[4] = bus.readWord(0xC762);
    for (;;) {
        loadRamParams(cpu, bus);
        callRamTest(cpu, bus, subs, true);
        bus.writeWord(cpu.ar[5]++, cpu.r[0]);
        if (cpu.r[0] == 1) {
            // 0631E  CALL $62F7 / 0631F CALL $632F / BU $631F — trava piscando o código
            labelDigits(cpu, bus);
            if (wait) {
                while (blinkCode(cpu, bus, wait)) {}  // BU $631F: pisca para sempre (até um RESET)
            } else {
                blinkCode(cpu, bus);
                subs.call(0x631F, cpu, bus);  // laço sem saída (fica registrado como pendente)
            }
            return;
        }
        cpu.ar[4] += 7;
        cpu.r[0] = bus.readWord(cpu.ar[4]);
        if (cpu.ar[6]-- == 0) break;  // DBU AR6
    }
}

/// `0x6403`: CMOS (U31) — grava e relê o byte alto do item 0xFC seis vezes; desenha o resultado.
void cmosTest(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t ar4 = cpu.ar[4], r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3];
    cpu.ar[4] = bus.readWord(0xC766);
    cpu.r[3] = 0x5A5A5A5Au;
    bus.writeWord(vunit::kBusControl, 0x88);
    subs.call(0x9953, cpu, bus);  // libera a CMOS
    cpu.ar[5] = 5;
    bool ok = true;
    for (;;) {
        cpu.r[2] = (cpu.r[3] & 0xFFu) << 24;
        cpu.ar[2] = (0xFCu << 2) + bus.readWord(0x9936);
        bus.writeWord(cpu.ar[2], cpu.r[2]);
        cpu.r[2] >>= 24;
        cpu.ar[0] = bus.readWord(0xC767);
        cpu.r[0] = bus.readWord(cpu.ar[0]);  // leitura da ROM (0xC00000) entre gravar e reler
        cpu.r[0] = bus.readWord(cpu.ar[2]) >> 24;
        if (cpu.r[0] != cpu.r[2]) {
            ok = false;
            break;
        }
        cpu.r[1] = cpu.r[3];
        cpu.r[3] <<= 1;
        cpu.r[1] ^= cpu.r[3];
        if (s(cpu.r[1]) < 0) cpu.r[3] |= 1;
        cpu.r[3] = tmsMpyi(cpu.r[3], 0x794F);
        if (cpu.ar[5]-- == 0) break;  // DBU AR5
    }
    subs.call(0x994B, cpu, bus);  // protege a CMOS
    cpu.rs = ok ? 4u : 3u;
    bus.writeWord(vunit::kBusControl, 0x1048);
    drawResult(cpu, bus, subs);
    cpu.r[3] = r3;
    cpu.r[2] = r2;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
    cpu.ar[4] = ar4;
}

/// `0x63D2`: molduras e rótulos de todas as entradas a partir de `[61C2]`; depois `0x63EC` (CMOS).
void drawAllFrames(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[4] = bus.readWord(0x61C2);
    do {
        drawFrame(cpu, bus, subs);
        cpu.ar[4] += 7;
        cpu.r[0] = bus.readWord(cpu.ar[4]);
    } while (cpu.r[0] != 0);
    subs.call(0x63EC, cpu, bus);
}

/// `0x63EC`: moldura e rótulo da entrada `[C765]` (CMOS U31).
void drawCmosFrame(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[4] = bus.readWord(0xC765);
    drawFrame(cpu, bus, subs);
}

/// Texto `[ptr]` em (x, y) com a cor @p color (`LDI RC / R2 / R3 / LDI (ptr),AR2 / CALL $A7F4`).
void text(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t ptr, uint32_t x, uint32_t y,
          uint32_t color) {
    cpu.rc = color;
    cpu.r[2] = x;
    cpu.r[3] = y;
    cpu.ar[2] = bus.readWord(ptr);
    subs.call(0xA7F4, cpu, bus);
}

/// `0x065F`: copia a tabela `[C7DF]` (contagem + dados) para a RAM de paleta 0x9E0000.
void restorePalette(Tms32031State& cpu, MemoryBus& bus) {
    cpu.ar[2] = bus.readWord(0xC7DF);
    cpu.ar[1] = vunit::kPaletteBase;
    cpu.ar[0] = bus.readWord(cpu.ar[2]++) - 1;
    do {
        cpu.r[0] = bus.readWord(cpu.ar[2]++);
        bus.writeWord(cpu.ar[1]++, cpu.r[0]);
    } while (cpu.ar[0]-- != 0);  // DBU AR0
}

/// Linhas da moldura da tela de diagnóstico (`0x6469`–`0x64B1`): AR2, R2, R3, RC.
constexpr uint32_t kGridLines[][4] = {
    {0x14, 0x32, 0x1EA, 0x32},   {0x14, 0x32, 0x14, 0x168},   {0x1EA, 0x32, 0x1EA, 0x168},
    {0x14, 0x168, 0x50, 0x168},  {0x50, 0x168, 0x50, 0x154},  {0x50, 0x154, 0x64, 0x154},
    {0x64, 0x154, 0x64, 0x168},  {0x64, 0x168, 0xC8, 0x168},  {0xC8, 0x168, 0xC8, 0x154},
    {0xC8, 0x154, 0xDC, 0x154},  {0xDC, 0x154, 0xDC, 0x168},  {0xDC, 0x168, 0x1EA, 0x168},
};

/**
 * @brief `0x644D`: paleta (U85/U87) e VRAM (U102/U95/U94/U101) + tela com molduras, títulos e
 *        resultados (incluindo as RAMs rápidas da `0x62D5`) e a CMOS.
 */
void diagnosticScreen(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[5] = bus.readWord(0xC768);
    cpu.ar[6] = 5;
    cpu.ar[4] = bus.readWord(0xC769);
    for (;;) {
        loadRamParams(cpu, bus);
        callRamTest(cpu, bus, subs, true);
        bus.writeWord(cpu.ar[5]++, cpu.r[0]);
        if (cpu.r[0] != 0) subs.call(0x630D, cpu, bus);
        cpu.ar[4] += 7;
        cpu.r[0] = bus.readWord(cpu.ar[4]);
        if (cpu.ar[6]-- == 0) break;
    }
    subs.call(0x065F, cpu, bus);
    cpu.r[0] = 0;
    bus.writeWord(cpu.direct(0x0000), cpu.r[0]);  // 06467  STI R0,($0000) com o DP do chamador
    subs.call(0x8E79, cpu, bus);
    const uint32_t dp = cpu.dp;
    for (const auto& l : kGridLines) {
        cpu.ar[2] = l[0];
        cpu.r[2] = l[1];
        cpu.r[3] = l[2];
        cpu.rc = l[3];
        cpu.rs = 1;
        subs.call(0xAC23, cpu, bus);
    }
    cpu.dp = dp;
    subs.call(0x63D2, cpu, bus);
    text(cpu, bus, subs, 0xC76A, 0x32, 0x14, 0xF);  // CRUISN USA (TM)
    text(cpu, bus, subs, 0xC76B, 0x32, 0x1E, 0xF);  // data da versão
    text(cpu, bus, subs, 0xC76C, 0x10E, 0x1E, 0xF); // VERSION  4.1
    // 064C1  10 resultados a partir de [C76D] (6 da 0x644D + 4 posições seguintes)
    cpu.ar[5] = bus.readWord(0xC76D);
    cpu.ar[6] = 9;
    cpu.ar[4] = bus.readWord(0xC76E);
    for (;;) {
        cpu.r[0] = bus.readWord(cpu.ar[5]++);
        cpu.rs = cpu.r[0] == 0 ? 4u : 3u;
        drawResult(cpu, bus, subs);
        cpu.ar[4] += 7;
        cpu.r[0] = bus.readWord(cpu.ar[4]);
        if (cpu.ar[6]-- == 0) break;
    }
    // 064DF  as 4 RAMs rápidas passaram (senão a 0x62D5 teria travado): verde
    cpu.ar[6] = 3;
    cpu.ar[4] = bus.readWord(0xC76F);
    for (;;) {
        cpu.rs = 4;
        drawResult(cpu, bus, subs);
        cpu.ar[4] += 7;
        cpu.r[0] = bus.readWord(cpu.ar[4]);
        if (cpu.ar[6]-- == 0) break;
    }
    subs.call(0x6403, cpu, bus);
}

/// `0x6381`: tela de diagnóstico, checksums das ROMs (U10–U29) e RAM de texturas (U69–U76).
void powerOnScreen(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    bus.writeWord(vunit::kPaletteBase, 0);  // 06382  DP = 0x9E: [0x9E0000] = 0
    cpu.r[0] = 0;
    subs.call(0x8E4E, cpu, bus);
    cpu.r[0] = vunit::kVideoRamBase;
    bus.writeWord(cpu.direct(0xCE13), cpu.r[0]);
    cpu.st &= ~Tms32031State::kStGie;
    subs.call(0x6548, cpu, bus);
    subs.call(0x644D, cpu, bus);
    text(cpu, bus, subs, 0xC763, 0x32, 0x28, 0xF);  // CPU BOARD TEST
    cpu.ar[4] = bus.readWord(0xC764);
    for (;;) {
        cpu.r[0] = bus.readWord(cpu.ar[4] + 6);
        if (cpu.r[0] != 0) {
            // 0639B  checksum: AR2 = início, RC = palavras, R2 = máscara, R3 = deslocamento
            cpu.ar[0] = bus.readWord(cpu.ar[4]);
            cpu.ar[2] = bus.readWord(cpu.ar[0]++);
            cpu.rc = bus.readWord(cpu.ar[0]++);
            cpu.r[2] = bus.readWord(cpu.ar[0]++);
            cpu.r[3] = bus.readWord(cpu.ar[0]++);
            subs.call(0x6552, cpu, bus);
            cpu.ar[0] = bus.readWord(cpu.ar[0]);
            cpu.r[1] = bus.readWord(cpu.ar[0]);
            cpu.rs = cpu.r[0] == cpu.r[1] ? 4u : 3u;
        } else {
            // 063A7  teste de RAM (entrada −1 encerra)
            cpu.ar[0] = bus.readWord(cpu.ar[4]);
            if (cpu.ar[0] == 0xFFFFFFFFu) break;
            loadRamParams(cpu, bus);
            callRamTest(cpu, bus, subs, false);
            cpu.rs = cpu.r[0] == 0 ? 4u : 3u;
        }
        drawResult(cpu, bus, subs);
        cpu.ar[4] += 7;
        cpu.r[0] = bus.readWord(cpu.ar[4]);
        if (cpu.r[0] == 0) break;
    }
    subs.call(0x6403, cpu, bus);  // 063D0
}

} // namespace

void registerPowerOnTest(SubroutineDispatcher& d) {
    d.add(0x62D5, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { powerOnFastRam(c, b, s); });
    d.add(0x62F7, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { labelDigits(c, b); });
    d.add(0x630D, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { blinkLabel(c, b); });
    d.add(0x6321, [](Tms32031State&, MemoryBus& b, ISubroutines&) { setLed(b, false); });
    d.add(0x6328, [](Tms32031State&, MemoryBus& b, ISubroutines&) { setLed(b, true); });
    d.add(0x632F, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { blinkCode(c, b); });
    d.add(0x6381, powerOnScreen);
    d.add(0x63D2, drawAllFrames);
    d.add(0x63EC, drawCmosFrame);
    d.add(0x6403, cmosTest);
    d.add(0x644D, diagnosticScreen);
    d.add(0x64FC, ramTestEntry);
    d.add(0x64FE, ramTest);
    d.add(0x6548, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { copyRamTest(c, b); });
    d.add(0x6552, romChecksum);
    d.add(0x503C, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { kickWatchdogSlow(c, b); });
    d.add(0x065F, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { restorePalette(c, b); });
}

void registerPowerOnPauses(SubroutineDispatcher& d, IBusyWait& wait) {
    IBusyWait* w = &wait;
    d.add(0x62D5, [w](Tms32031State& c, MemoryBus& b, ISubroutines& s) { powerOnFastRam(c, b, s, w); });
    d.add(0x630D, [w](Tms32031State& c, MemoryBus& b, ISubroutines&) { blinkLabel(c, b, w); });
    d.add(0x632F, [w](Tms32031State& c, MemoryBus& b, ISubroutines&) { blinkCode(c, b, w); });
}

} // namespace rea::core::boot
