/**
 * @file cabinet_outputs.cpp
 * @brief Tradução de `0x4E4F`–`0x4EC6`, `0x4FAE`–`0x4FEE`, `0xAFC2`–`0xB019`, `0x4996`–`0x49B7`.
 */
#include "core/io/cabinet_outputs.hpp"

#include "core/non_local_jump.hpp"

#include "core/tms_float.hpp"

namespace rea::core::io {

namespace {

using namespace out_vars;

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// Limita @p v à faixa [@p lo, @p hi].
double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

/// Arredonda para a precisão do STF (o original grava os resultados intermediários).
double stf(double v) { return tmsToDouble(doubleToTms(v)); }

/// 04E77  endereço 5 e o dado; os bits 0xC0 dependem de [C8E3]/[E867]
void writeLamps(Tms32031State& cpu, MemoryBus& bus) {
    cpu.ar[0] = kOutputPort;
    cpu.r[0] &= 0xFFu;
    if (bus.readWord(kForceLamps) != 0 && bus.readWord(kLampMask) == 0) cpu.r[0] &= ~0xC0u;
    else cpu.r[0] |= 0xC0u;
    cpu.r[1] = cpu.r[0] | 0xFB00u;
    cpu.r[2] = 0xFFFFF705u;
    writeOutput(bus, 5, cpu.r[0]);
    cpu.r[0] |= 0xFF00u;
}

/// 04FDC  sem créditos no atrato: padrão 2, 4, 8 a cada 60 quadros
void idleLamps(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[0] = bus.readWord(kIdleTimer) + 1;
    if (s(cpu.r[0]) < 0) cpu.r[0] = 0;
    bus.writeWord(kIdleTimer, cpu.r[0]);
    if (s(cpu.r[0]) < 60) return;
    cpu.r[0] = 0;
    bus.writeWord(kIdleTimer, cpu.r[0]);
    cpu.r[0] = bus.readWord(kIdleStep) + 1;
    if (s(cpu.r[0]) >= 3) cpu.r[0] = 0;
    if (s(cpu.r[0]) < 0) cpu.r[0] = 0;
    bus.writeWord(kIdleStep, cpu.r[0]);
    cpu.ar[0] = cpu.r[0] + bus.readWord(kIdlePatterns);
    cpu.r[0] = bus.readWord(cpu.ar[0]);
    writeLamps(cpu, bus);  // BU 04E77 (não grava [C943])
}

} // namespace

void writeOutput(MemoryBus& bus, uint32_t addr, uint32_t data) {
    const uint32_t a = addr & 0xFFu, d = data & 0xFFu;
    bus.writeWord(kOutputPort, 0xFFFFFF00u | a);
    bus.writeWord(kOutputPort, 0xFFFFF700u | a);
    bus.writeWord(kOutputPort, 0xFFFFFF00u | a);
    bus.writeWord(kOutputPort, 0xFF00u | d);
    bus.writeWord(kOutputPort, 0xFB00u | d);
    bus.writeWord(kOutputPort, 0xFF00u | d);
}

void updateLamps(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t mode = bus.readWord(kGameMode) & 0xFu;
    if (mode == 1) {
        // 04E9B  START pisca: aceso 0–29, apagado 30–49; escreve sempre (sem os bits 0xC0)
        cpu.r[0] = bus.readWord(kStartLamp);
        cpu.r[1] = bus.readWord(kBlinkCounter) + 1;
        bus.writeWord(kBlinkCounter, cpu.r[1]);
        if (s(cpu.r[1]) >= 30) {
            cpu.r[0] = 0;
            if (s(cpu.r[1]) >= 50) {
                cpu.r[1] = 0;
                bus.writeWord(kBlinkCounter, cpu.r[1]);
            }
        }
        cpu.dp = 0;
        cpu.ar[0] = kOutputPort;
        writeOutput(bus, 5, cpu.r[0]);
        cpu.r[1] = cpu.r[0] | 0xFB00u;
        cpu.r[0] |= 0xFF00u;
        return;
    }
    if (mode == 2) {
        cpu.ar[2] = 0x19;
        subs.call(0x99E9, cpu, bus);
        if (cpu.r[0] == 1) {
            cpu.r[0] = bus.readWord(kStartLamp) | 1u;
            bus.writeWord(kStartLamp, cpu.r[0]);
        } else {
            cpu.ar[2] = 0x71;
            subs.call(0x99E9, cpu, bus);
            if (s(cpu.r[0]) <= 0) {
                idleLamps(cpu, bus);
                return;
            }
        }
    }
    // 04E61  START pisca: aceso 0–29, apagado 30–59
    cpu.r[0] = bus.readWord(kStartLamp);
    if (cpu.r[0] & 1u) {
        cpu.r[1] = bus.readWord(kBlinkCounter) + 1;
        bus.writeWord(kBlinkCounter, cpu.r[1]);
        if (s(cpu.r[1]) >= 30) {
            cpu.r[0] &= ~1u;
            if (s(cpu.r[1]) >= 60) {
                cpu.r[1] = 0;
                bus.writeWord(kBlinkCounter, cpu.r[1]);
            }
        }
        cpu.dp = 0;
    }
    // 04E72  só escreve se mudou (ou com [C8E3] != 0)
    cpu.r[2] = bus.readWord(kForceLamps);
    if (cpu.r[2] == 0 && cpu.r[0] == bus.readWord(kLastLamps)) return;
    bus.writeWord(kLastLamps, cpu.r[0]);
    writeLamps(cpu, bus);
}

void updateWheelMotor(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 0AFC2  ganho, limite e amortecimento por modo
    const uint32_t mode = bus.readWord(kGameMode) & 0xFu;
    uint32_t gain = doubleToTms(0.0), limit = doubleToTms(126.0), damp = doubleToTms(2.5);
    switch (mode) {
    case 4: gain = bus.readWord(kWheelGain); damp = bus.readWord(kWheelGain); break;
    case 5: gain = doubleToTms(2.0); break;
    case 7: gain = doubleToTms(0.0); break;
    case 3: gain = doubleToTms(0.5); break;
    case 6: gain = doubleToTms(0.75); break;
    case 1: damp = doubleToTms(0.0); break;
    default: break;
    }
    bus.writeWord(kWheelGain, gain);
    bus.writeWord(kWheelLimit, limit);
    bus.writeWord(kWheelDamp, damp);
    // 0AFE3  erro de posição e velocidade filtrada
    const double wheel = static_cast<double>(s(bus.readWord(kWheelPos)));
    double r0 = tmsToDouble(bus.readWord(kWheelCenter)) - wheel;
    const double prev = tmsToDouble(bus.readWord(kWheelPrev));
    bus.writeWord(kWheelPrev, doubleToTms(wheel));
    double r2 = clamp(wheel - prev, -22.0, 22.0);  // 0AFE8  SUBF3 R1,R2,R2 = R1 − R2: atual − anterior
    const double r1 = tmsToDouble(bus.readWord(kWheelVel)) * shortFloat(0xF0CD);
    r2 = stf(r2 * shortFloat(0xE666) + r1);
    bus.writeWord(kWheelVel, doubleToTms(r2));
    r2 = r2 * shortFloat(0x10CD) * tmsToDouble(damp);
    r0 = r0 * tmsToDouble(gain) - r2;
    const double lim = tmsToDouble(limit);
    r0 = clamp(r0, -lim, lim);
    cpu.r[0] = doubleToTms(r0);
    bus.writeWord(kWheelForce, cpu.r[0]);
    cpu.r[0] = tmsFix(tmsToDouble(cpu.r[0])) & 0xFFu;
    cpu.r[1] = doubleToTms(-lim);
    cpu.r[2] = doubleToTms(r2);
    cpu.r[3] = damp;
    subs.call(0xB001, cpu, bus);
}

void secretSequence(Tms32031State& cpu, MemoryBus& bus, ISubroutines& /*subs*/) {
    auto reset = [&] {
        cpu.r[0] = 0;
        bus.writeWord(kSecretStep, cpu.r[0]);
        bus.writeWord(kSecretTimer, cpu.r[0]);
    };
    if ((bus.readWord(kGameMode) & 0xFu) != 2) {
        reset();
        return;
    }
    // 04FB2  R2 = botões & máscara; compara com o passo atual
    cpu.r[2] = bus.readWord(kButtons) & bus.readWord(kSecretMask);
    cpu.ar[0] = bus.readWord(kSecretStep);
    cpu.r[0] = cpu.ar[0];
    cpu.ar[0] += bus.readWord(kSecretTable);
    cpu.r[1] = bus.readWord(cpu.ar[0]);
    if (cpu.r[2] == cpu.r[1]) {
        // 04FC7  acertou: avança; no 7º, BR $0669
        cpu.r[0] = bus.readWord(kSecretStep) + 1;
        if (cpu.r[0] == 7) {
            reset();
            throw NonLocalJump{0x0669};  // BR (salto não-local; ver o cabeçalho)
        }
        bus.writeWord(kSecretStep, cpu.r[0]);
        cpu.r[0] = 120;
        bus.writeWord(kSecretTimer, cpu.r[0]);
        return;
    }
    // 04FBA  ainda segurando o botão do passo anterior: gasta o prazo
    cpu.r[1] = bus.readWord(kSecretStep);
    if (cpu.r[1] == 0) {
        reset();
        return;
    }
    cpu.r[1] -= 1;
    cpu.ar[0] = cpu.r[1] + bus.readWord(kSecretTable);
    cpu.r[1] = bus.readWord(cpu.ar[0]);
    if (cpu.r[2] != cpu.r[1]) {
        reset();
        return;
    }
    cpu.r[0] = bus.readWord(kSecretTimer) - 1;
    if (s(cpu.r[0]) < 0) {
        reset();
        return;
    }
    bus.writeWord(kSecretTimer, cpu.r[0]);
}

bool motionRequest(MemoryBus& bus) {
    // 04996  carry = ([0x991030] & [4995]) != 0 (a leitura do endereço 0 é só espera do barramento)
    return (bus.readWord(kMotionPort) & bus.readWord(kMotionMask)) != 0;
}

void pollMotion(Tms32031State& cpu, MemoryBus& bus) {
    // 049B1  0x4693: bit 0x40 de [0x992000] >> 16 ligado → nada
    cpu.r[0] = bus.readWord(kDipPort) >> 16;
    cpu.dp = 0;
    if (cpu.r[0] & 0x40u) return;
    if (!motionRequest(bus)) {
        cpu.st &= ~Tms32031State::kStCarry;
        return;
    }
    cpu.st |= Tms32031State::kStCarry;
    cpu.r[0] = 1;
    bus.writeWord(kMotionPending, cpu.r[0]);
}

void registerCabinetOutputs(SubroutineDispatcher& d) {
    d.add(0x49B1, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { pollMotion(c, b); });
    d.add(0x4996, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        if (motionRequest(b)) c.st |= Tms32031State::kStCarry;
        else c.st &= ~Tms32031State::kStCarry;
        c.dp = 0;
    });
    // 049A7  [EBB7] = 0 com o GIE desligado; religa (0x4C33)
    d.add(0x49A7, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.st &= ~Tms32031State::kStGie;
        c.r[0] = 0;
        b.writeWord(kMotionPending, c.r[0]);
        s.call(0x4C33, c, b);
    });
    d.add(0x4E4F, updateLamps);
    d.add(0xAFC2, updateWheelMotor);
    d.add(0x4FAE, secretSequence);
    // 0B001  endereço 4 (motor) com o dado R0; preserva R0, R1 e DP
    d.add(0xB001, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { writeOutput(b, 4, c.r[0]); });
}

} // namespace rea::core::io
