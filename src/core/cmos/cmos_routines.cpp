/**
 * @file cmos_routines.cpp
 * @brief Tradução das rotinas de CMOS (`0x993F`–`0x9A1F`).
 */
#include "core/cmos/cmos_routines.hpp"

#include "core/vunit_hw.hpp"

namespace rea::core::cmos {

namespace {
/// Endereço de PALAVRA de `@($off)` numa página DP explícita.
constexpr uint32_t at(uint32_t dp, uint32_t off) { return (dp << 16) | off; }
} // namespace

void slowBus(Tms32031State& cpu, MemoryBus& bus) {
    // 0993F  PUSH DP / LDIU $80,DP / LDI $90,R1 / STI R1,@($8064) / POP DP / RETS
    cpu.r[1] = 0x0090;
    bus.writeWord(vunit::kBusControl, cpu.r[1]);
}

void normalBus(Tms32031State& cpu, MemoryBus& bus) {
    // 09945  idem, com R1 = $470.
    cpu.r[1] = 0x0470;
    bus.writeWord(vunit::kBusControl, cpu.r[1]);
}

void lock(Tms32031State& cpu, MemoryBus& bus) {
    // 0994B  R1 = @($E572) & $F00 → @($995020)
    cpu.r[1] = bus.readWord(at(0x00, kProtectExtra)) & 0x0F00u;
    bus.writeWord(vunit::kCmosProtect, cpu.r[1]);
}

void unlock(Tms32031State& cpu, MemoryBus& bus) {
    // 09953  R1 = (@($E572) & $F00) | $C00 → @($995020)
    cpu.r[1] = (bus.readWord(at(0x00, kProtectExtra)) & 0x0F00u) | 0x0C00u;
    bus.writeWord(vunit::kCmosProtect, cpu.r[1]);
}

void read32(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t saved_r1 = cpu.r[1];         // 099F9  PUSH R1
    subs.call(0x993F, cpu, bus);                // 099FA  CALL $993F (barramento lento)
    // 099FC–09A09  Junta os bits 31–24 de 4 palavras, MSB primeiro.
    //               LSH é deslocamento LÓGICO no TMS320C3x (sem extensão de sinal).
    cpu.r[0] = bus.readWord(cpu.ar[2]++) >> 24;
    for (int i = 0; i < 3; ++i) {
        cpu.r[0] <<= 8;
        cpu.r[1] = bus.readWord(cpu.ar[2]++) >> 24;
        cpu.r[0] |= cpu.r[1];
    }
    subs.call(0x9945, cpu, bus);                // 09A0A  CALL $9945 (barramento normal)
    cpu.r[1] = saved_r1;                        // 09A0B  POP R1
}

void write32(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t saved_r1 = cpu.r[1];         // 09A0D  PUSH R1/R2/R3
    const uint32_t saved_r2 = cpu.r[2];
    const uint32_t saved_r3 = cpu.r[3];
    subs.call(0x993F, cpu, bus);                // 09A10  barramento lento
    subs.call(0x9953, cpu, bus);                // 09A11  destrava a CMOS
    // 09A13–09A19  STI R2,*AR2++ e LSH 8,R2 alternados: cada palavra recebe o
    //               valor deslocado, de modo que os bits 31–24 levam um byte (MSB primeiro).
    for (int i = 0; i < 4; ++i) {
        bus.writeWord(cpu.ar[2]++, cpu.r[2]);
        if (i < 3) cpu.r[2] <<= 8;
    }
    subs.call(0x994B, cpu, bus);                // 09A1A  trava a CMOS
    subs.call(0x9945, cpu, bus);                // 09A1B  barramento normal
    cpu.r[3] = saved_r3;                        // 09A1C  POP R3/R2/R1
    cpu.r[2] = saved_r2;
    cpu.r[1] = saved_r1;
}

namespace {
/// `LSH 2,AR2 / ADDI @($9936),AR2` — índice → endereço do item.
void indexToAddress(Tms32031State& cpu, MemoryBus& bus) {
    cpu.ar[2] = (cpu.ar[2] << 2) + bus.readWord(cpu.direct(kTableBasePtr));
}
} // namespace

void getItem(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    indexToAddress(cpu, bus);                   // 099E9–099EA
    subs.call(0x99F9, cpu, bus);                // 099EB  CALL $99F9
    cpu.ar[2] -= 4;                             // 099EC  NOP *AR2--(4)
}

void setItem(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    indexToAddress(cpu, bus);                   // 099F4–099F5
    subs.call(0x9A0D, cpu, bus);                // 099F6  CALL $9A0D
    cpu.ar[2] -= 4;                             // 099F7  NOP *AR2--(4)
}

void incrementItem(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    indexToAddress(cpu, bus);                   // 099DA–099DB
    subs.call(0x99F9, cpu, bus);                // 099DC  lê
    cpu.ar[2] -= 4;                             // 099DD  volta ao início do item
    cpu.r[0] += 1;                              // 099DE  ADDI 1,R0
    cpu.r[2] = cpu.r[0];                        // 099DF  LDI R0,R2
    subs.call(0x9A0D, cpu, bus);                // 099E0  grava (AR2 termina em endereço + 4)
}

void addItem(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    indexToAddress(cpu, bus);                   // 099E2–099E3
    subs.call(0x99F9, cpu, bus);                // 099E4  lê
    cpu.ar[2] -= 4;                             // 099E5  volta ao início do item
    cpu.r[2] += cpu.r[0];                       // 099E6  ADDI R0,R2
    subs.call(0x9A0D, cpu, bus);                // 099E7  grava
}

void checksum(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 099CA  PUSH R1, AR2, AR5; AR5 = 0; R1 = 0
    const uint32_t r1 = cpu.r[1], ar2 = cpu.ar[2], ar5 = cpu.ar[5];
    cpu.ar[5] = 0;
    cpu.r[1] = 0;
    // 099CF  laço: R1 += item[AR5] enquanto AR5 < 0x2E
    do {
        cpu.ar[2] = cpu.ar[5];
        subs.call(0x99E9, cpu, bus);
        cpu.r[1] += cpu.r[0];
        cpu.ar[5] += 1;
    } while (static_cast<int32_t>(cpu.ar[5]) < static_cast<int32_t>(kChecksumCount));
    // 099D5  R0 = R1; POP AR5, AR2, R1
    cpu.r[0] = cpu.r[1];
    cpu.ar[5] = ar5;
    cpu.ar[2] = ar2;
    cpu.r[1] = r1;
}

void setItemWithChecksum(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    subs.call(0x99F4, cpu, bus);   // 099EE  grava o item
    subs.call(0x99CA, cpu, bus);   // 099EF  R0 = checksum
    cpu.r[2] = cpu.r[0];           // 099F0
    cpu.ar[2] = kChecksumItem;     // 099F1
    subs.call(0x99F4, cpu, bus);   // 099F2  item 0x35 = checksum
}

void validateRanges(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 09981  PUSH R0–R4, AR2, AR5, AR6
    const Tms32031State saved = cpu;
    // 09989  AR5 = 0; AR6 = [C733] (tabela na ROM)
    cpu.ar[5] = 0;
    cpu.ar[6] = bus.readWord(cpu.direct(kRangeTablePtr));
    do {
        // 0998B  AR2 = AR5; CALL $99E9 → R0 = item
        cpu.ar[2] = cpu.ar[5];
        subs.call(0x99E9, cpu, bus);
        // 0998D  R1 = mín, R2 = máx, R3 = padrão
        cpu.r[1] = bus.readWord(cpu.ar[6]++);
        cpu.r[2] = bus.readWord(cpu.ar[6]++);
        cpu.r[3] = bus.readWord(cpu.ar[6]++);
        const auto v = static_cast<int32_t>(cpu.r[0]);
        // 09990  BLT fora; 09992 BLE dentro
        if (v < static_cast<int32_t>(cpu.r[1]) || v > static_cast<int32_t>(cpu.r[2])) {
            // 09994  R2 = padrão; AR2 = AR5; CALL $99EE
            cpu.r[2] = cpu.r[3];
            cpu.ar[2] = cpu.ar[5];
            subs.call(0x99EE, cpu, bus);
        }
        // 09997  AR5++; enquanto AR5 < 0x2E
        cpu.ar[5] += 1;
    } while (static_cast<int32_t>(cpu.ar[5]) < static_cast<int32_t>(kChecksumCount));
    // 0999A  POP (ordem inversa)
    for (int i : {0, 1, 2, 3, 4}) cpu.r[i] = saved.r[i];
    cpu.ar[2] = saved.ar[2];
    cpu.ar[5] = saved.ar[5];
    cpu.ar[6] = saved.ar[6];
}

namespace {
/// Trecho repetido de `0x995C`: item = clamp(item, 0, max) com LDIGT/LDILT.
void clampItem(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t item, int32_t max) {
    cpu.ar[2] = item;                                       // LDI item,AR2
    subs.call(0x99E9, cpu, bus);                            // CALL $99E9
    cpu.r[2] = cpu.r[0];                                    // LDI R0,R2
    const auto v = static_cast<int32_t>(cpu.r[0]);
    if (v > max) cpu.r[2] = static_cast<uint32_t>(max);     // CMPI max,R0; LDIGT max,R2
    if (v < 0) cpu.r[2] = 0;                                // CMPI 0,R0;   LDILT 0,R2
    cpu.ar[2] = item;
    subs.call(0x99F4, cpu, bus);                            // CALL $99F4 (sem checksum)
}
} // namespace

void validateAll(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    clampItem(cpu, bus, subs, kItemClamp10, 10);   // 0995C–09964
    clampItem(cpu, bus, subs, kItemClamp30, 30);   // 09965–0996D
    subs.call(0x9AA9, cpu, bus);                   // 0996E  tabela de recordes
    subs.call(0x9981, cpu, bus);                   // 0996F  faixas dos ajustes
    cpu.st &= ~Tms32031State::kStCarry;            // 09970  ANDN 1,ST
}

// --- Tabela de recordes na CMOS (0x9A77–0x9B17) -------------------------------------------------
// 15 tabelas (R6: pista 0..13 e a corrida inteira, 14) de 10 entradas (R7) com 8 palavras cada:
// tempo (32 bits em 4 bytes, `0x9A0D`) + 3 iniciais e o carro (1 byte cada, bits 31..24).

void hiscoreAddress(Tms32031State& cpu, MemoryBus& bus) {
    // 09AD2  AR2 = R6·0x50 + R7·8 + 0x3F8 + [9936]
    cpu.ar[2] = cpu.r[6] * 0x50u + cpu.r[7] * 8u + 0x3F8u + bus.readWord(cpu.direct(0x9936));
}

void hiscoreWrite(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 09AEB  R0 = tempo, R1..R4 = iniciais e carro → AR2 (AR2 += 8). Preserva R0–R3.
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3];
    cpu.r[2] = cpu.r[0];
    subs.call(0x9A0D, cpu, bus);
    cpu.r[2] = r2;
    subs.call(0x993F, cpu, bus);
    subs.call(0x9953, cpu, bus);
    cpu.r[1] = r1;
    bus.writeWord(cpu.ar[2]++, r1 << 24);
    bus.writeWord(cpu.ar[2]++, r2 << 24);
    bus.writeWord(cpu.ar[2]++, r3 << 24);
    bus.writeWord(cpu.ar[2]++, cpu.r[4] << 24);
    subs.call(0x994B, cpu, bus);
    subs.call(0x9945, cpu, bus);
    cpu.r[0] = r0; cpu.r[1] = r1; cpu.r[2] = r2; cpu.r[3] = r3;
}

void hiscoreWriteDefault(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 09ADC  entrada padrão de 2 palavras em AR1: tempo e (carro:8 | inicial3:8 | inicial2:8 | inicial1:8).
    //        R4 = carro (não preservado).
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3];
    cpu.r[0] = bus.readWord(cpu.ar[1]);
    cpu.r[1] = bus.readWord(cpu.ar[1] + 1);
    cpu.r[2] = cpu.r[1] >> 8;
    cpu.r[3] = cpu.r[1] >> 16;
    cpu.r[4] = cpu.r[1] >> 24;
    hiscoreWrite(cpu, bus, subs);
    cpu.r[0] = r0; cpu.r[1] = r1; cpu.r[2] = r2; cpu.r[3] = r3;
}

void hiscoreRead(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 09B06  R0 = tempo (`0x99F9`); R1..R4 = bytes 31..24 das 4 palavras seguintes (AR2 += 8).
    subs.call(0x99F9, cpu, bus);
    subs.call(0x993F, cpu, bus);
    subs.call(0x9953, cpu, bus);
    uint32_t v[4];
    for (auto& w : v) w = bus.readWord(cpu.ar[2]++) >> 24;
    subs.call(0x994B, cpu, bus);
    subs.call(0x9945, cpu, bus);
    cpu.r[1] = v[0]; cpu.r[2] = v[1]; cpu.r[3] = v[2]; cpu.r[4] = v[3];
}

void hiscoreReset(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 09A99  15 tabelas × 10 entradas a partir de [C736] (a 15ª, corrida inteira, de [9A62]); depois 0x9A77
    for (cpu.r[6] = 0; cpu.r[6] <= 14; ++cpu.r[6]) {
        cpu.r[7] = 0;
        cpu.ar[1] = bus.readWord(cpu.direct(0xC736));
        if (cpu.r[6] == 14) cpu.ar[1] = bus.readWord(cpu.direct(0x9A62));
        for (int i = 0; i < 10; ++i) {
            hiscoreAddress(cpu, bus);
            hiscoreWriteDefault(cpu, bus, subs);
            cpu.ar[1] += 2;
            ++cpu.r[7];
        }
    }
    cpu.ar[4] = 0xFFFFFFFFu;
    cpu.ar[5] = 0xFFFFFFFFu;
    subs.call(0x9A77, cpu, bus);
}

void hiscoreClearExtra(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 09A77  tabela 15, entrada 0: 15 valores de 32 bits = −1
    cpu.r[6] = 0xF;
    cpu.r[7] = 0;
    hiscoreAddress(cpu, bus);
    cpu.r[2] = 0xFFFFFFFFu;
    for (int i = 0; i < 15; ++i) subs.call(0x9A0D, cpu, bus);
    cpu.ar[5] = 0xFFFFFFFFu;
}

void hiscoreValidate(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 09AA9  tabelas 0..9: iniciais ' ' ou '0'..'Z', carro 0..10, tempo 0..14400; senão refaz tudo (0x9A99)
    auto initialOk = [](uint32_t ch) { return ch == 0x20 || (static_cast<int32_t>(ch) >= 0x30 && static_cast<int32_t>(ch) <= 0x5A); };
    cpu.ar[1] = bus.readWord(cpu.direct(0xC737));
    for (cpu.r[6] = 0; cpu.r[6] <= 9; ++cpu.r[6]) {
        cpu.r[7] = 0;
        for (int i = 0; i < 10; ++i) {
            hiscoreAddress(cpu, bus);
            hiscoreRead(cpu, bus, subs);
            const auto car = static_cast<int32_t>(cpu.r[4]), time = static_cast<int32_t>(cpu.r[0]);
            if (!initialOk(cpu.r[1]) || !initialOk(cpu.r[2]) || !initialOk(cpu.r[3]) || car < 0 || car > 10 ||
                time > 0x3840 || time < 0) {
                hiscoreReset(cpu, bus, subs);
                return;
            }
            cpu.ar[1] += 2;
            ++cpu.r[7];
        }
    }
    cpu.ar[4] = 0xFFFFFFFFu;
    cpu.ar[5] = 0xFFFFFFFFu;
}

void clearAudits(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 09972  0xC3 contadores de 32 bits a partir de [9936] + 0xDC = 0. Preserva R0, R2, AR2, AR4.
    const uint32_t r0 = cpu.r[0], r2 = cpu.r[2], ar2 = cpu.ar[2], ar4 = cpu.ar[4];
    cpu.ar[2] = bus.readWord(cpu.direct(0x9936)) + 0xDC;
    cpu.r[2] = 0;
    for (int i = 0; i < 0xC3; ++i) subs.call(0x9A0D, cpu, bus);
    cpu.r[0] = r0; cpu.r[2] = r2; cpu.ar[2] = ar2; cpu.ar[4] = ar4;
}

void factoryAdjustments(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 099A3  ajustes 0x00..0x2D = padrão da tabela (mín, máx, padrão) de [C734]
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3], ar2 = cpu.ar[2], ar5 = cpu.ar[5],
                   ar6 = cpu.ar[6];
    cpu.ar[6] = bus.readWord(cpu.direct(0xC734));
    for (cpu.ar[5] = 0; cpu.ar[5] < 0x2E; ++cpu.ar[5]) {
        cpu.r[2] = bus.readWord(cpu.ar[6] + 2);
        cpu.ar[6] += 3;
        cpu.ar[2] = cpu.ar[5];
        subs.call(0x99F4, cpu, bus);
    }
    // 099B4  ajuste 0 = registro de preço dos DIPs (0x73B7), ou o padrão se passar do máximo
    subs.call(0x73B7, cpu, bus);
    cpu.ar[2] = bus.readWord(cpu.direct(0xC735));
    cpu.r[0] = bus.readWord(cpu.ar[2] + 1);
    if (static_cast<int32_t>(cpu.r[2]) > static_cast<int32_t>(cpu.r[0])) cpu.r[2] = bus.readWord(cpu.ar[2] + 2);
    cpu.ar[2] = 0;
    subs.call(0x99EE, cpu, bus);
    subs.call(0x99CA, cpu, bus);
    cpu.r[2] = cpu.r[0];
    cpu.ar[2] = kChecksumItem;
    subs.call(0x99F4, cpu, bus);
    cpu.r[0] = r0; cpu.r[1] = r1; cpu.r[2] = r2; cpu.r[3] = r3; cpu.ar[2] = ar2; cpu.ar[5] = ar5; cpu.ar[6] = ar6;
}

void factoryRestore(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 00DCE  contadores, recordes e ajustes de fábrica; itens 0x70/0x71 = 0; aviso [0DDA] (0x0E62) e
    //        a tela de configuração (0x0CC0)
    subs.call(0x9972, cpu, bus);
    subs.call(0x9A99, cpu, bus);
    subs.call(0x99A3, cpu, bus);
    cpu.r[2] = 0;
    cpu.ar[2] = 0x70;
    subs.call(0x99F4, cpu, bus);
    cpu.ar[2] = 0x71;
    subs.call(0x99F4, cpu, bus);
    cpu.ar[2] = bus.readWord(cpu.direct(0x0DDA));
    subs.call(0x0E62, cpu, bus);
    subs.call(0x0CC0, cpu, bus);
}

void coinMeters(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 075F1  a cada 8 quadros alterna: pulso ligado (bits 0x100/0x200 por contador pendente) ou desligado
    cpu.dp = 0;
    cpu.r[0] = bus.readWord(kCoinTick) + 1;
    bus.writeWord(kCoinTick, cpu.r[0]);
    if (static_cast<int32_t>(cpu.r[0]) < 8) return;
    cpu.r[0] = 0;
    bus.writeWord(kCoinTick, cpu.r[0]);
    const bool wasOn = bus.readWord(kCoinPhase) != 0;
    bus.writeWord(kCoinPhase, wasOn ? 0u : 1u);
    cpu.r[0] = wasOn ? 0u : 1u;
    if (!wasOn) {
        // 0761C  fase desligada: só limpa os bits 0xF00
        cpu.st &= ~Tms32031State::kStGie;
        cpu.r[0] = bus.readWord(kProtectExtra) & ~0xF00u;
        bus.writeWord(kProtectExtra, cpu.r[0]);
    } else {
        // 07600  fase ligada: um pulso por contador [E573]/[E574] pendente
        cpu.r[0] = bus.readWord(kProtectExtra) & ~0xF00u;
        cpu.r[1] = bus.readWord(kCoinCount1) - 1;
        if (static_cast<int32_t>(cpu.r[1]) >= 0) {
            bus.writeWord(kCoinCount1, cpu.r[1]);
            cpu.r[0] |= 0x100u;
        }
        cpu.r[1] = bus.readWord(kCoinCount2) - 1;
        if (static_cast<int32_t>(cpu.r[1]) >= 0) {
            bus.writeWord(kCoinCount2, cpu.r[1]);
            cpu.r[0] |= 0x200u;
        }
        cpu.st &= ~Tms32031State::kStGie;
    }
    bus.writeWord(kCmosProtectPort, cpu.r[0]);
    if (wasOn) bus.writeWord(kProtectExtra, cpu.r[0]);
    subs.call(0x4C33, cpu, bus);  // RETIU: religa o GIE
    cpu.dp = 0;
}

/// `0xAB1A`: registra um erro (R0 = código) na CMOS. O contador é o item 0xDE (até 0x1C entradas);
/// cada entrada ocupa 0x40 palavras a partir de `[9936]` + 0x8B7 e guarda 16 valores de 4 bytes:
/// código, PC do processo atual (`[C97E]+2`), DP, R0, AR0, AR1, AR2, `[AR2+F]`, AR4, `[C8C5]`,
/// `[AR2+3]`, AR7, `[C97E]+3`, itens 0x6F e 0x75 e o índice do nó do carro `[[[E876]+37]+1E]`.
/// Preserva todos os registradores (empilhados em 0AB1A–0AB28).
void errorLog(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const Tms32031State saved = cpu;
    cpu.dp = 0;                                         // 0AB2A  SUBI DP,DP
    cpu.ar[2] = 0xDE;
    subs.call(0x99E9, cpu, bus);                        // 0AB2C  contador de erros
    cpu.r[0] += 1;
    if (static_cast<int32_t>(cpu.r[0]) < 0x1D) {        // 0AB2F  BGE fim
        cpu.r[2] = cpu.r[0];
        cpu.ar[2] = 0xDE;
        subs.call(0x99F4, cpu, bus);
        cpu.ar[2] = (cpu.r[0] - 1) * 0x40 + 0x4BF + 0x3F8 + bus.readWord(0x9936);
        auto put = [&](uint32_t v) { cpu.r[2] = v; subs.call(0x9A0D, cpu, bus); };
        const uint32_t proc = bus.readWord(0xC97E);
        put(saved.r[0]);                                // *-AR3(A)
        put(bus.readWord(proc + 2));
        put(saved.dp);                                  // *-AR3(8)
        put(saved.r[0]);                                // *-AR3(9)
        put(saved.ar[0]);
        put(saved.ar[1]);
        put(saved.ar[2]);
        put(bus.readWord(saved.ar[2] + 0xF));
        put(saved.ar[4]);                               // *-AR3(3)
        put(bus.readWord(0xC8C5));
        put(bus.readWord(saved.ar[2] + 3));
        put(saved.ar[7]);                               // *-AR3
        put(bus.readWord(proc + 3));
        for (uint32_t item : {0x6Fu, 0x75u}) {
            const uint32_t ar2 = cpu.ar[2];
            cpu.ar[2] = item;
            subs.call(0x99E9, cpu, bus);
            cpu.ar[2] = ar2;
            put(cpu.r[0]);
        }
        put(bus.readWord(bus.readWord(bus.readWord(0xE876) + 0x37) + 0x1E));
    }
    const uint32_t st = cpu.st;                         // ST não é empilhado
    cpu = saved;                                        // 0AB69  POP AR7 … R1
    cpu.st = st;
}

void registerRoutines(SubroutineDispatcher& d) {
    d.add(0x75F1, coinMeters);
    d.add(0x4C33, [](Tms32031State& c, MemoryBus&, ISubroutines&) { c.st |= Tms32031State::kStGie; });
    d.add(0x9981, validateRanges);
    d.add(0x995C, validateAll);
    d.add(0x99CA, checksum);
    d.add(0x99EE, setItemWithChecksum);
    d.add(0x993F, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { slowBus(c, b); });
    d.add(0x9945, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { normalBus(c, b); });
    d.add(0x994B, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { lock(c, b); });
    d.add(0x9953, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { unlock(c, b); });
    d.add(0x99F9, read32);
    d.add(0x9A0D, write32);
    d.add(0x99E9, getItem);
    d.add(0xAB1A, errorLog);
    d.add(0x99F4, setItem);
    d.add(0x99DA, incrementItem);
    d.add(0x99E2, addItem);
    d.add(0x0DCE, factoryRestore);
    d.add(0x9972, clearAudits);
    d.add(0x99A3, factoryAdjustments);
    d.add(0x9A77, hiscoreClearExtra);
    d.add(0x9A99, hiscoreReset);
    d.add(0x9AA9, hiscoreValidate);
    d.add(0x9AD2, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { hiscoreAddress(c, b); });
    d.add(0x9ADC, hiscoreWriteDefault);
    d.add(0x9AEB, hiscoreWrite);
    d.add(0x9B06, hiscoreRead);
}

} // namespace rea::core::cmos
