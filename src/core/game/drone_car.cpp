/**
 * @file drone_car.cpp
 * @brief Tradução de `0x50CB`–`0x5139`, `0x509A`, `0x54FD`–`0x5542`, `0x5360`, `0x536C`,
 *        `0x2945`–`0x2975`, `0x8F3E`–`0x8F66` e `0xAE3A`–`0xAEA3`.
 */
#include "core/game/drone_car.hpp"

#include "core/proc/process_manager.hpp"
#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

constexpr uint32_t kCarTable  = 0x5051; ///< `[5051]` = 0x5052: 6 palavras por carro.
constexpr uint32_t kCarObjs   = 0x508E; ///< `[508E]` = 0xE8A3: objeto de cada carro.
constexpr uint32_t kModels    = 0x961B; ///< `[961B]`: 11 palavras por modelo.
constexpr uint32_t kCarType   = 0x216;  ///< Tipo de objeto do carro.

/// Lê o float TMS em @p a e o converte para double.
double rf(MemoryBus& b, uint32_t a) { return tmsToDouble(b.readWord(a)); }
/// Converte @p v para float TMS e grava em @p a.
void wf(MemoryBus& b, uint32_t a, double v) { b.writeWord(a, doubleToTms(v)); }
/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
int32_t si(uint32_t v) { return static_cast<int32_t>(v); }
/// Converte a constante curta @p v em float.
double k(uint16_t v) { return shortFloat(v); }
/// Liga @p bits na palavra em @p a.
void orBits(MemoryBus& b, uint32_t a, uint32_t bits) { b.writeWord(a, b.readWord(a) | bits); }

/// 0509A  R0 = fator de velocidade (item 0x1B da CMOS, dificuldade [E639]/[C8D5] e pista).
void speedFactor(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[2] = 0x1B;
    subs.call(0x99E9, cpu, bus);
    double r0 = static_cast<double>(si(cpu.r[0])) * k(0xB23D) + k(0xF4CD);
    double r1;
    if (bus.readWord(0xC8E3) == 0) {
        r1 = rf(bus, 0xE639);
    } else {
        r1 = rf(bus, 0xC8D5);
        if (r1 > k(0x0148)) r1 = k(0x0148);
        if (r1 < k(0xF4CD)) r1 = k(0xF4CD);
        r1 = (r1 + rf(bus, 0xE639)) * 0.5;
    }
    if (r1 > k(0x0148)) r1 = k(0x0148);
    if (r1 < k(0xF4CD)) r1 = k(0xF4CD);
    r1 -= 1.0;
    int32_t t = si(bus.readWord(0xE481));
    if (t < 0) t = 0;
    if (t > 0xD) t = 0xD;
    cpu.ar[2] = bus.readWord(0x50BC) + static_cast<uint32_t>(t);
    cpu.r[2] = bus.readWord(cpu.ar[2]);
    r0 += r1 + tmsToDouble(cpu.r[2]);
    if (r0 < k(0xF400)) r0 = k(0xF400);
    if (r0 > k(0x0200)) r0 = k(0x0200);
    cpu.r[1] = doubleToTms(r1);
    cpu.r[0] = doubleToTms(r0);
}

/// 054FD  posiciona o carro AR4 a partir do nó AR2, AR3 passos à frente (ver o arquivo).
void placeOnTrack(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t ar5 = cpu.ar[5], ar6 = cpu.ar[6];
    cpu.ar[5] = bus.readWord(0x5E);
    for (uint32_t i = 0; i < 3; ++i) bus.writeWord(cpu.ar[5] + i, bus.readWord(cpu.ar[2] + 1 + i));
    cpu.ar[2] = bus.readWord(cpu.ar[2] + 0x21);
    auto heading = [&]() {
        // 05507
        cpu.r[2] = doubleToTms(rf(bus, cpu.ar[2] + 1) - rf(bus, cpu.ar[5]));
        cpu.r[3] = doubleToTms(rf(bus, cpu.ar[2] + 3) - rf(bus, cpu.ar[5] + 2));
        subs.call(0x931A, cpu, bus);
        cpu.r[0] = doubleToTms(tmsToDouble(cpu.r[0]) - k(0x0491));
        cpu.r[2] = cpu.r[0];
    };
    heading();
    while (cpu.ar[3] != 0) {
        // 05510  um passo de 5500 no rumo R2
        cpu.r[0] = doubleToTms(5500.0);
        subs.call(0x9076, cpu, bus);
        wf(bus, cpu.ar[5], tmsToDouble(cpu.r[0]) + rf(bus, cpu.ar[5]));
        wf(bus, cpu.ar[5] + 2, tmsToDouble(cpu.r[1]) + rf(bus, cpu.ar[5] + 2));
        cpu.ar[3] -= 1;
        if (si(cpu.ar[3]) <= 0) break;
        // 05519  avança os nós já alcançados (≤ 5000)
        for (;;) {
            const double dx = rf(bus, cpu.ar[2] + 1) - rf(bus, cpu.ar[5]);
            const double dz = rf(bus, cpu.ar[2] + 3) - rf(bus, cpu.ar[5] + 2);
            cpu.r[2] = doubleToTms(dx * dx + dz * dz);
            subs.call(0xA5F1, cpu, bus);
            if (tmsToDouble(cpu.r[0]) > 5000.0) break;
            cpu.ar[2] = bus.readWord(cpu.ar[2] + 0x21);
        }
        heading();
    }
    // 05526  nó atual, deslocamento lateral [AR7+0x1B]
    bus.writeWord(cpu.ar[7] + 0x10, cpu.ar[2]);
    bus.writeWord(cpu.ar[7] + 0x13, bus.readWord(cpu.ar[2] + 0x1E));
    const uint32_t r2 = cpu.r[2];
    cpu.ar[2] = bus.readWord(0x5A);
    subs.call(0x938A, cpu, bus);
    subs.call(0x9414, cpu, bus);
    bus.writeWord(cpu.ar[2], bus.readWord(cpu.ar[7] + 0x1B));
    cpu.r[2] = bus.readWord(0x5A);
    cpu.r[3] = cpu.ar[2];
    subs.call(0x941E, cpu, bus);
    cpu.ar[0] = bus.readWord(0x5D);
    wf(bus, cpu.ar[4] + 1, rf(bus, cpu.ar[5]) + rf(bus, cpu.ar[0]));
    wf(bus, cpu.ar[4] + 2, rf(bus, cpu.ar[5] + 1) - rf(bus, cpu.ar[5] + 0x48) + rf(bus, cpu.ar[0] + 1));
    wf(bus, cpu.ar[4] + 3, rf(bus, cpu.ar[5] + 2) + rf(bus, cpu.ar[0] + 2));
    cpu.r[2] = r2;
    cpu.ar[6] = ar6;
    cpu.ar[5] = ar5;
}

/// 02945  dados do carro (0x5B palavras) em [AR4+0x1B]; carry = sucesso. Preserva R0 e AR1.
void carData(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r0 = cpu.r[0], ar1 = cpu.ar[1];
    subs.call(0x9024, cpu, bus);
    if (cpu.carry()) {
        bus.writeWord(cpu.ar[4] + 0x1B, cpu.ar[0]);
        for (uint32_t i = 0; i < 0x5B; ++i) wf(bus, cpu.ar[0] + i, 0.0);
        subs.call(0x2185, cpu, bus);
        cpu.ar[1] = bus.readWord(cpu.ar[4] + 0xD);
        bus.writeWord(cpu.ar[4] + 0x1D,
                      tmsFix(static_cast<double>(si(bus.readWord(cpu.ar[1]))) * k(0x00CD)));
        for (uint32_t f : {0x31u, 0x1Eu, 0x1Fu, 0x20u, 0x34u, 0x36u, 0x33u, 0x38u, 0x56u, 0x59u, 0x57u, 0x58u})
            bus.writeWord(cpu.ar[0] + f, 0);
        wf(bus, cpu.ar[0] + 0x39, 0.0);
        bus.writeWord(cpu.ar[0] + 0x41, 1);
        bus.writeWord(cpu.ar[0] + 0x44, bus.readWord(0x2913));
        bus.writeWord(cpu.ar[0] + 0x45, bus.readWord(0x2914));
        wf(bus, cpu.ar[0] + 0x23, k(0xF51F));
        wf(bus, cpu.ar[0] + 0x30, 1.0);
        wf(bus, cpu.ar[0] + 0x22, 1.0);
        orBits(bus, cpu.ar[4] + 0xE, 1u << 27);
        cpu.st |= Tms32031State::kStCarry;
    }
    cpu.ar[1] = ar1;
    cpu.r[0] = r0;
}

/// 08F3E  peças do modelo AR2 (lista `[[961B] + 11·AR2 + 2]`) e o processo [8F67] (ID 0x230).
void carParts(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t ar0 = cpu.ar[0], ar3 = cpu.ar[3];
    cpu.ar[2] = bus.readWord(cpu.ar[2] * 0xB + bus.readWord(kModels) + 2);
    if (cpu.ar[2] != 0) {
        orBits(bus, cpu.ar[4] + 0xE, 0x800);
        cpu.ar[3] = cpu.ar[4] + 0x1A;
        cpu.rc = bus.readWord(cpu.ar[2]++);
        for (int32_t i = 0; i <= si(cpu.rc); ++i) {
            subs.call(0x9001, cpu, bus);
            bus.writeWord(cpu.ar[3], cpu.ar[0]);
            for (uint32_t j = 0; j < 3; ++j) {
                cpu.r[0] = bus.readWord(cpu.ar[2]++);
                bus.writeWord(cpu.ar[0] + 1 + j, cpu.r[0]);
                bus.writeWord(cpu.ar[0] + 4 + j, cpu.r[0]);
            }
            bus.writeWord(cpu.ar[0] + 0x10, bus.readWord(cpu.ar[2]++));
            bus.writeWord(cpu.ar[0] + 0x12, bus.readWord(cpu.ar[2]++));
            bus.writeWord(cpu.ar[0] + 0x11, cpu.ar[4]);
            cpu.ar[3] = cpu.ar[0];
        }
        bus.writeWord(cpu.ar[3], 0);
        cpu.r[0] = bus.readWord(cpu.ar[2]++);
        cpu.ar[2] = bus.readWord(0x8F67);
        cpu.r[2] = 0x230;
        subs.call(0xA868, cpu, bus);
        bus.writeWord(cpu.ar[4] + 0x16, cpu.ar[0]);
    }
    cpu.ar[3] = ar3;
    cpu.ar[0] = ar0;
}

/// 0AE6A  peças a partir da lista AR2 (inteiros) se `[AR4+0xE]` & 0x800 ainda não está ligado.
void carPartsInt(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r1 = cpu.r[1], r2 = cpu.r[2], r6 = cpu.r[6], ar0 = cpu.ar[0], ar2 = cpu.ar[2], ar3 = cpu.ar[3];
    cpu.r[0] = 0x800;
    cpu.r[1] = bus.readWord(cpu.ar[4] + 0xE);
    if (!(cpu.r[1] & cpu.r[0])) {
        bus.writeWord(cpu.ar[4] + 0xE, cpu.r[1] | cpu.r[0]);
        cpu.ar[3] = cpu.ar[4] + 0x1A;
        cpu.rc = bus.readWord(cpu.ar[2]++) - 1;
        cpu.r[6] = bus.readWord(cpu.ar[2]++);
        for (int32_t i = 0; i <= si(cpu.rc); ++i) {
            subs.call(0x9001, cpu, bus);
            bus.writeWord(cpu.ar[3], cpu.ar[0]);
            cpu.r[0] = bus.readWord(cpu.ar[2]++);
            cpu.r[6] -= cpu.r[0];
            bus.writeWord(cpu.ar[0] + 0x10, cpu.r[0] - 1);
            for (uint32_t j = 0; j < 3; ++j) {
                cpu.r[0] = doubleToTms(static_cast<double>(si(bus.readWord(cpu.ar[2]++))));
                bus.writeWord(cpu.ar[0] + 1 + j, cpu.r[0]);
                bus.writeWord(cpu.ar[0] + 4 + j, cpu.r[0]);
            }
            cpu.ar[3] = cpu.ar[0];
        }
        // 0AE8C  peça final com o resto dos polígonos
        subs.call(0x9001, cpu, bus);
        bus.writeWord(cpu.ar[3], cpu.ar[0]);
        cpu.r[6] -= 1;
        bus.writeWord(cpu.ar[0] + 0x10, cpu.r[6]);
        for (uint32_t j = 1; j <= 6; ++j) wf(bus, cpu.ar[0] + j, 0.0);
        cpu.ar[3] = cpu.ar[0];
        bus.writeWord(cpu.ar[3], 0);
        cpu.ar[0] += 7;
        subs.call(0x93D7, cpu, bus);
        cpu.ar[2] = bus.readWord(0xAEA7);
        cpu.r[2] = 0x230;
        subs.call(0xA868, cpu, bus);
        bus.writeWord(cpu.ar[4] + 0x16, cpu.ar[0]);
    }
    cpu.ar[3] = ar3;
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
    cpu.r[6] = r6;
    cpu.r[2] = r2;
    cpu.r[1] = r1;
}

/// 0AE3A  liga o carro AR4 ao processo AR7: dados (0x2945), listas, flags e o modelo.
void carSetup(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = bus.readWord(cpu.ar[7] + 0x19);
    subs.call(0x2945, cpu, bus);
    cpu.ar[2] = cpu.ar[4];
    subs.call(0x7093, cpu, bus);
    subs.call(0x6762, cpu, bus);
    bus.writeWord(cpu.ar[4] + 0x17, cpu.ar[7]);
    orBits(bus, cpu.ar[4] + 0xE, 1u << 23);
    bus.writeWord(cpu.ar[7] + 0x11, 1);
    cpu.ar[5] = bus.readWord(cpu.ar[4] + 0x1B);
    wf(bus, cpu.ar[5] + 0x24, k(0xF4CD));
    wf(bus, cpu.ar[5] + 0x2B, 0.0);
    wf(bus, cpu.ar[5] + 0x23, k(0xF333));
    wf(bus, cpu.ar[7] + 0xF, 1.0);
    bus.writeWord(cpu.ar[7] + 0x13, 0x101);
    cpu.ar[2] = bus.readWord(cpu.ar[7] + 0x19) * 0xB + bus.readWord(kModels);
    bus.writeWord(cpu.ar[5] + 0x30, bus.readWord(cpu.ar[2] + 6));
    cpu.r[0] = bus.readWord(cpu.ar[2] + 3);
    if (cpu.r[0] != 0) {
        bus.writeWord(cpu.ar[4] + 0x18, cpu.r[0]);
        orBits(bus, cpu.ar[4] + 0xE, 0x200);
        cpu.r[0] = bus.readWord(cpu.ar[2] + 4);
        if (cpu.r[0] != 0) {
            bus.writeWord(cpu.ar[4] + 0x19, cpu.r[0]);
            orBits(bus, cpu.ar[4] + 0xE, 0x4);
        }
    }
    if (bus.readWord(cpu.ar[4] + 0xF) == 0x213) return;
    cpu.r[0] = bus.readWord(cpu.ar[2] + 0xA);
    if (cpu.r[0] == 0) return;
    cpu.ar[2] = cpu.r[0];
    subs.call(0xAE6A, cpu, bus);
}

/// 050CB  processo do carro R4.
void carProcess(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t p = cpu.ar[7];
    wf(bus, p + 0x29, 0.0);
    bus.writeWord(0xE8A2, 0);
    bus.writeWord(p + 0x28, 0);
    bus.writeWord(p + 0xE, cpu.r[4]);
    cpu.ar[0] = cpu.r[4] * 6 + bus.readWord(kCarTable);
    bus.writeWord(p + 0x27, cpu.ar[0]);
    if (si(cpu.r[4]) >= 8 && bus.readWord(0xC8E3) != 0) {
        cpu.r[0] = bus.readWord(0xC8D8);
        subs.call(0xA66E, cpu, bus);  // BR $A66E
        return;
    }
    // 050DC  objeto do modelo
    cpu.r[0] = bus.readWord(cpu.ar[0]);
    bus.writeWord(p + 0x19, cpu.r[0]);
    cpu.ar[5] = cpu.r[0];
    cpu.ar[2] = bus.readWord(cpu.r[0] * 0xB + bus.readWord(kModels));
    subs.call(0x700A, cpu, bus);
    if (cpu.carry()) {
        subs.call(0xA896, cpu, bus);
        return;
    }
    cpu.ar[4] = cpu.ar[0];
    bus.writeWord(cpu.r[4] + bus.readWord(kCarObjs), cpu.ar[4]);
    cpu.ar[2] = cpu.ar[5];
    subs.call(0x8F3E, cpu, bus);
    subs.call(0xAE3A, cpu, bus);
    cpu.ar[0] = bus.readWord(p + 0x27);
    cpu.r[0] = bus.readWord(cpu.ar[0] + 5);
    if (cpu.r[0] != 0) {
        cpu.ar[2] = cpu.r[0];
        subs.call(0x9EDB, cpu, bus);
        bus.writeWord(cpu.ar[4] + 0x10, cpu.r[0]);
        orBits(bus, cpu.ar[4] + 0xE, 0x400);
    }
    wf(bus, cpu.ar[5] + 0x45, rf(bus, cpu.ar[5] + 0x44) * k(0x0400));
    // 050F8  lateral, lado, velocidade
    cpu.ar[0] = bus.readWord(p + 0x27);
    cpu.r[0] = bus.readWord(cpu.ar[0] + 2);
    bus.writeWord(p + 0x1B, cpu.r[0]);
    bus.writeWord(p + 0x20, cpu.r[0]);
    bus.writeWord(p + 0xC, tmsToDouble(cpu.r[0]) > 600.0 ? 3u : 2u);
    if ((bus.readWord(0xC8C5) & 0xFu) == 2) {
        cpu.r[0] = doubleToTms(1.0);
    } else {
        subs.call(0x509A, cpu, bus);
    }
    cpu.ar[0] = bus.readWord(p + 0x27);
    wf(bus, cpu.ar[5] + 0x23, tmsToDouble(cpu.r[0]) * rf(bus, cpu.ar[0] + 3));
    bus.writeWord(p + 0x26, bus.readWord(cpu.ar[0] + 4));
    // 0510A  posição na pista: 2º nó da lista, [entrada+1] passos
    cpu.ar[2] = bus.readWord(0xE46E);
    cpu.r[0] = bus.readWord(cpu.ar[2] + 0x1E) & ~0xFFu;
    cpu.ar[2] = bus.readWord(cpu.ar[2] + 0x21);
    cpu.ar[3] = bus.readWord(cpu.ar[0] + 1);
    subs.call(0x54FD, cpu, bus);
    bus.writeWord(cpu.ar[4] + 0x15, cpu.r[2]);
    bus.writeWord(cpu.ar[5] + 0x2C, cpu.r[2]);
    bus.writeWord(cpu.ar[5] + 0x2D, cpu.r[2]);
    cpu.ar[2] = cpu.ar[4] + 4;
    subs.call(0x938A, cpu, bus);
    for (uint32_t a : {cpu.ar[4] + 0xF, cpu.ar[5] + 0x40, p + 3}) bus.writeWord(a, kCarType);
    wf(bus, cpu.ar[5] + 0x2B, 0.0);
    wf(bus, cpu.ar[5] + 0x22, k(0xE614));
    wf(bus, p + 0xF, 1.0);
    wf(bus, p + 0x21, 1.0);
    cpu.r[0] = doubleToTms(k(0xB4CD));
    subs.call(0x8EC2, cpu, bus);
    wf(bus, p + 0x22, tmsToDouble(cpu.r[0]) + k(0x019A));
    bus.writeWord(p + 0x23, 0x78);
    bus.writeWord(p + 0x24, 0x78);
    bus.writeWord(p + 0x25, 0);
    cpu.ar[2] = cpu.ar[4];
    subs.call(0x715F, cpu, bus);
    subs.call(0x7093, cpu, bus);
    cpu.r[2] = bus.readWord(p + 0xE);
    bus.writeWord(cpu.ar[5] + 0x59, cpu.r[2]);
    if (bus.readWord(0xC8E3) != 0) {
        // 05131  carro do outro gabinete no link
        cpu.r[0] = (bus.readWord(0xC8CC) & 1u) ? 0u : 1u;
        cpu.r[0] ^= (cpu.r[2] >> 1) & 1u;
        bus.writeWord(cpu.ar[5] + 0x58, cpu.r[0]);
        if (cpu.r[0] != 0) {
            subs.call(0xA718, cpu, bus);  // BNE $A718
            return;
        }
    }
    // 0513A  laço de IA (ponte do port: o PCB passa a apontar para 0x513A)
    bus.writeWord(p + proc::pcb::kPc, 0x513A);
    subs.call(0x513A, cpu, bus);
}

/// 02EFD  [AR5+0x37] = nó da lista [E46F] (+0x21) mais próximo do carro AR4 no plano XZ.
void nearestNode(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    cpu.ar[0] = 0;
    cpu.r[0] = bus.readWord(0xE46F);
    if (cpu.r[0] == 0) return;
    const uint32_t ir0 = cpu.ir0;
    cpu.ir0 = 3;
    double best = 32767.0 * 32767.0;
    const double x = rf(bus, cpu.ar[4] + 1), z = rf(bus, cpu.ar[4] + 3);
    cpu.r[3] = bus.readWord(cpu.ar[4] + 1);
    cpu.r[4] = bus.readWord(cpu.ar[4] + 3);
    cpu.ar[2] = cpu.r[0];
    do {
        const double dx = rf(bus, cpu.ar[2] + 1) - x, dz = rf(bus, cpu.ar[2] + 3) - z;
        const double d2 = dx * dx + dz * dz;
        if (best > d2) {
            cpu.ar[0] = cpu.ar[2];
            best = d2;
        }
        cpu.r[0] = bus.readWord(cpu.ar[2] + 0x21);
        cpu.ar[2] = cpu.r[0];  // 02F10  BNED (slot: AR2 = R0)
    } while (cpu.r[0] != 0);
    cpu.r[1] = doubleToTms(best);
    cpu.r[2] = doubleToTms(best);
    bus.writeWord(cpu.ar[5] + 0x37, cpu.ar[0]);
    cpu.ir0 = ir0;
}

} // namespace

void registerDroneCar(SubroutineDispatcher& d) {
    d.add(0x2EFD, nearestNode);
    d.add(0x50CB, carProcess);
    d.add(0x509A, speedFactor);
    d.add(0x54FD, placeOnTrack);
    d.add(0x2945, carData);
    d.add(0x8F3E, carParts);
    d.add(0xAE3A, carSetup);
    d.add(0xAE6A, carPartsInt);
    // 05360  AR2 = trecho de índice R2 na lista [E46E] (sem achar: trava, como no original)
    d.add(0x5360, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        for (c.ar[2] = b.readWord(0xE46E); c.ar[2] != 0; c.ar[2] = b.readWord(c.ar[2] + 0x21)) {
            c.r[0] = b.readWord(c.ar[2] + 0x1E);
            if (c.r[2] == c.r[0]) return;
        }
        s.call(0x5368, c, b);  // 05368: BU $ (trava)
    });
    // 0536C  [AR7+0x12] = nó da pista de índice ≥ R2 ([AA2F]); [AR7+0x13] = índice
    d.add(0x536C, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.ar[0] = b.readWord(0xAA2F) + 3;
        do {
            c.r[0] = b.readWord(c.ar[0]);
            c.ar[0] += 4;
        } while (si(c.r[0]) < si(c.r[2]));
        c.ar[0] -= 7;
        b.writeWord(c.ar[7] + 0x12, c.ar[0]);
        b.writeWord(c.ar[7] + 0x13, c.r[0]);
    });
    // 09076  R0 = −R0·sen(R2); R1 = R0·cos(R2). Preserva R3.
    d.add(0x9076, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t r3 = c.r[3];
        c.r[1] = c.r[0];
        c.r[3] = doubleToTms(-tmsToDouble(c.r[0]));
        s.call(0x92DF, c, b);
        c.r[1] = doubleToTms(tmsToDouble(c.r[1]) * tmsToDouble(c.r[0]));
        s.call(0x92E2, c, b);
        c.r[0] = doubleToTms(tmsToDouble(c.r[3]) * tmsToDouble(c.r[0]));
        c.r[3] = r3;
    });
}

} // namespace rea::core::game
