/**
 * @file attract_demo.cpp
 * @brief Tradução de `0x559E`–`0x561F`, `0x5620`, `0x58DD`–`0x58EE`, `0x5943`–`0x59E2`.
 */
#include "core/game/attract_demo.hpp"

#include <cmath>

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

using namespace demo_vars;

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
/// Lê o float TMS em @p a e o converte para double.
double rf(MemoryBus& bus, uint32_t a) { return tmsToDouble(bus.readWord(a)); }
/// Converte @p v para float TMS e grava em @p a.
void wf(MemoryBus& bus, uint32_t a, double v) { bus.writeWord(a, doubleToTms(v)); }

/// Dorme um quadro pela `0xA874` e retoma o processo em @p resume.
void sleep1(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t resume) {
    cpu.ar[2] = 1;
    cpu.ret = resume;
    subs.call(0xA874, cpu, bus);
}

/// Cria o processo `[procPtr]` com o ID @p id (`0xA844`).
void spawn(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t procPtr, uint32_t id) {
    cpu.ar[2] = bus.readWord(procPtr);
    cpu.r[2] = id;
    subs.call(0xA844, cpu, bus);
}

// ---------------------------------------------------------------------------------------------
// Laço do roteiro (0x559E)

/// `0x55C2`: câmera (`0x5943`) e dorme um quadro (retoma em `0x55C5`).
void frameTail(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 055C2  câmera e 1 quadro
    subs.call(0x5943, cpu, bus);
    sleep1(cpu, bus, subs, 0x55C5);
}

/// `0x55CC`: próximo passo do roteiro (o terminador repete o último) e cria o objeto pendente.
void nextStep(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 055CC  terminador → repete o último passo
    cpu.ar[0] = bus.readWord(cpu.ar[6]);
    if (cpu.ar[0] == 0) cpu.ar[6] -= 3;
    // 055D0  objeto pendente
    cpu.r[0] = bus.readWord(cpu.ar[7] + kPendingObj);
    if (cpu.r[0] != 0) {
        cpu.ar[2] = cpu.r[0];
        subs.call(0x7093, cpu, bus);
        cpu.r[0] = 0;
        bus.writeWord(cpu.ar[7] + kPendingObj, cpu.r[0]);
    }
    // 055D6  início do passo e a duração
    cpu.ar[0] = bus.readWord(cpu.ar[6]++);
    subs.call(cpu.ar[0], cpu, bus);
    cpu.ar[5] = bus.readWord(cpu.ar[6]++);
    frameTail(cpu, bus, subs);
}

/// `0x55C5`: retomada a cada quadro; conta `[C941]` e avança o roteiro quando o passo acaba.
void demoResume(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 055C5
    cpu.r[0] = bus.readWord(kCountdown);
    if (cpu.r[0] != 0) bus.writeWord(kCountdown, cpu.r[0] - 1);
    if (s(cpu.ar[5]) > 0) {
        cpu.ar[0] = bus.readWord(cpu.ar[6]);  // 055C0  função por quadro
        subs.call(cpu.ar[0], cpu, bus);
        frameTail(cpu, bus, subs);
        return;
    }
    cpu.ar[6] += 1;
    nextStep(cpu, bus, subs);
}

/// `0x559E`: início da demonstração (roteiro do par de pistas, câmera e céu).
void demoStart(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 0559E
    cpu.r[0] = 0;
    bus.writeWord(cpu.ar[7] + kPendingObj, cpu.r[0]);
    cpu.r[4] = bus.readWord(kTrack) >> 1;
    bus.writeWord(cpu.ar[7] + kTrackPair, cpu.r[4]);
    loadDemoTrack(cpu, bus, subs);
    cpu.r[0] = 4;
    bus.writeWord(kCountdown, cpu.r[0]);
    spawn(cpu, bus, subs, kFrameProc, 0x500);
    cpu.r[0] = bus.readWord(0xC8C5) | 0x200u;
    bus.writeWord(0xC8C5, cpu.r[0]);
    cpu.r[0] = 0xFFFFFFFFu;
    bus.writeWord(cpu.ar[7] + kSnap, cpu.r[0]);
    cpu.r[0] = 1;
    for (uint32_t a : {0xC931u, 0xC946u, 0xE5EBu}) bus.writeWord(a, cpu.r[0]);
    sleep1(cpu, bus, subs, 0x55B4);
}

/// `0x55B4`: a câmera segue o carro 4.
void demoFocus(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 055B4  segue o carro 4
    cpu.ar[4] = bus.readWord(kFocusCar);
    cpu.ar[5] = bus.readWord(cpu.ar[4] + 0xA);
    cpu.ar[4] = bus.readWord(cpu.ar[4] + 9);
    cpu.ar[0] = bus.readWord(cpu.ar[5] + 0xA);
    bus.writeWord(kFocusCar, cpu.ar[4]);
    bus.writeWord(kFocusData, cpu.ar[0]);
    cpu.r[0] = 0x5A;
    bus.writeWord(0xE604, cpu.r[0]);
    cpu.r[0] = 1;
    bus.writeWord(cpu.ar[7] + kSnap, cpu.r[0]);
    subs.call(0x5620, cpu, bus);
    nextStep(cpu, bus, subs);
}

// ---------------------------------------------------------------------------------------------
// Moldura (0x55DA)

/// `0x55EE`: dorme um quadro (retoma em `0x55F0`).
void frameObjBody(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 055EE  dorme 1 quadro (retoma em 055F0)
    sleep1(cpu, bus, subs, 0x55F0);
}

/// `0x55F0`: um quadro do objeto animado da demonstração.
void frameObjResume(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t o = cpu.ar[4];
    const double frames = static_cast<double>(s(bus.readWord(kFrames)));
    cpu.r[6] = doubleToTms(frames);
    double r7 = tmsToDouble(cpu.r[7]);
    if (rf(bus, o + 2) != -85.0) {
        // 055F4  desliza para (−120, −85)
        double x = (-120.0 - rf(bus, o + 1)) * r7 * frames + rf(bus, o + 1);
        if (x < -120.0) x = -120.0;
        wf(bus, o + 1, x);
        double y = (-85.0 - rf(bus, o + 2)) * r7 * frames + rf(bus, o + 2);
        if (y < -85.0) y = -85.0;
        wf(bus, o + 2, y);
        r7 += shortFloat(0xA75C);
        cpu.r[7] = doubleToTms(r7);
    }
    // 05607  z −= 9632/125 por quadro, até 368
    cpu.r[0] = doubleToTms(-9632.0);
    cpu.r[1] = doubleToTms(125.0);
    subs.call(0xA509, cpu, bus);
    double z = tmsToDouble(cpu.r[0]) * frames + rf(bus, o + 3);
    if (z < 368.0) z = 368.0;
    wf(bus, o + 3, z);
    // 05611  ângulo X −= 18,85/125 por quadro, até 0
    cpu.r[0] = doubleToTms(shortFloat(0x416D));
    cpu.r[1] = doubleToTms(125.0);
    subs.call(0xA509, cpu, bus);
    double a = rf(bus, o + 0x14) - tmsToDouble(cpu.r[0]) * frames;
    if (a < 0.0) a = 0.0;
    cpu.r[2] = doubleToTms(a);
    bus.writeWord(o + 0x14, cpu.r[2]);
    cpu.ar[2] = o + 4;
    subs.call(0x93C4, cpu, bus);
    cpu.ar[5] -= bus.readWord(kFrames);
    if (s(cpu.ar[5]) > 0) {
        frameObjBody(cpu, bus, subs);
        return;
    }
    subs.call(0xA896, cpu, bus);  // BR $A896
}

/// `0x55DA`: cria o objeto animado; sem objeto livre, o processo morre.
void frameObjStart(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 055DA
    subs.call(0x7025, cpu, bus);
    if (cpu.carry()) {
        subs.call(0xA896, cpu, bus);
        return;
    }
    cpu.ar[4] = cpu.ar[0];
    cpu.r[0] = 0x00C2F8F3u;
    bus.writeWord(cpu.ar[0] + 0xD, cpu.r[0]);
    cpu.r[0] = 0xA3u | bus.readWord(cpu.ar[0] + 0xE);
    bus.writeWord(cpu.ar[0] + 0xE, cpu.r[0]);
    cpu.r[0] = 0x550;
    bus.writeWord(cpu.ar[0] + 0xF, cpu.r[0]);
    wf(bus, cpu.ar[0] + 3, 10000.0);
    cpu.ar[2] = cpu.ar[0];
    subs.call(0x7070, cpu, bus);
    wf(bus, cpu.ar[4] + 0x14, shortFloat(0x416D));
    cpu.r[7] = doubleToTms(0.0);
    cpu.ar[5] = 0x7D;
    frameObjBody(cpu, bus, subs);
}

/// `0x59AF`: cria os processos fixos e os carros da atração.
void spawnFixedAndCars(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 059AF
    spawn(cpu, bus, subs, 0xB0B2, 0x830);
    spawn(cpu, bus, subs, 0xB0B3, 0x830);
    spawn(cpu, bus, subs, 0xB0B4, 0x840);
    spawn(cpu, bus, subs, 0xB0B5, 0x830);
    cpu.r[0] = 0x33BB;
    bus.writeWord(0xE5EC, cpu.r[0]);
    static constexpr uint32_t kCars[][2] = {{0, 0xB0B6}, {1, 0xB0B7}, {2, 0xB0B8}, {3, 0xB0B9}, {4, 0xB0BA},
                                            {5, 0xB0BB}, {6, 0xB0BC}, {7, 0xB0BD}, {9, 0xB0BE}};
    for (const auto& car : kCars) {
        cpu.r[4] = car[0];
        spawn(cpu, bus, subs, car[1], 0x200);
        if (car[0] == 4) bus.writeWord(kFocusCar, cpu.ar[0]);
    }
}

/// 05968  [E3FD] = sen(ângulo X) · 512 + [AR7+2B]
void skyOffset(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[2] = bus.readWord(cpu.ar[7] + kCamAng);
    subs.call(0x92E2, cpu, bus);
    cpu.r[0] = doubleToTms(tmsToDouble(cpu.r[0]) * 512.0 + rf(bus, cpu.ar[7] + kSkyBase));
    bus.writeWord(kSkyOffset, cpu.r[0]);
}

} // namespace

void wrapPi(Tms32031State& cpu) {
    // 058E8
    double r0 = tmsToDouble(cpu.r[0]), r1 = 0.0;
    if (r0 > shortFloat(0x148F)) r1 = shortFloat(0x2B71);
    if (r0 < shortFloat(0x1B71)) r1 = shortFloat(0x248F);
    cpu.r[1] = doubleToTms(r1);
    cpu.r[0] = doubleToTms(r0 + r1);
}

void applyDemoCamera(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 05943  posição e ângulos
    cpu.ar[0] = bus.readWord(kCamPosPtr);
    for (uint32_t i = 0; i < 3; ++i) bus.writeWord(cpu.ar[0] + i, bus.readWord(cpu.ar[7] + kCamPos + i));
    for (uint32_t i = 0; i < 3; ++i) bus.writeWord(kCamAngles + i, bus.readWord(cpu.ar[7] + kCamAng + i));
    cpu.dp = 0;
    // 05952  matriz: direta ou suavizada (0,2 antiga + 0,8 nova)
    cpu.ar[0] = bus.readWord(kCamMatPtr);
    cpu.ar[1] = cpu.ar[7] + kCamMat;
    const bool snap = bus.readWord(cpu.ar[7] + kSnap) != 0;
    for (uint32_t i = 0; i < 9; ++i) {
        const double nv = rf(bus, cpu.ar[1] + i);
        const double v = snap ? nv : rf(bus, cpu.ar[0] + i) * shortFloat(0xD4CD) + nv * shortFloat(0xF4CD);
        wf(bus, cpu.ar[0] + i, v);
    }
    cpu.ar[0] += 9;
    cpu.ar[1] += 9;
    skyOffset(cpu, bus, subs);
    cpu.r[0] = 0;
    bus.writeWord(cpu.ar[7] + kSnap, cpu.r[0]);
}

void loadDemoTrack(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    // 0596F  música do atrato na pista 0
    if (bus.readWord(kTrack) == 0) {
        cpu.ar[2] = 0x1E;
        subs.call(0x99E9, cpu, bus);
        if (cpu.r[0] != 0) {
            cpu.ar[2] = 0x3F;
            subs.call(0x91E0, cpu, bus);
        }
    }
    cpu.r[0] = 0x42;
    bus.writeWord(0xC8C5, cpu.r[0]);
    cpu.ar[2] = bus.readWord(0xB0B0);
    subs.call(0x9DFE, cpu, bus);
    cpu.r[0] = 0;
    bus.writeWord(0xE47F, cpu.r[0]);
    // 0597D  [E480] / [E481] da tabela de pistas
    cpu.ar[1] = bus.readWord(kTrackTable) + bus.readWord(kTrack);
    bus.writeWord(0xE480, bus.readWord(cpu.ar[1]++));
    cpu.r[1] = bus.readWord(cpu.ar[1]);
    bus.writeWord(0xE481, cpu.r[1]);
    cpu.ar[0] = bus.readWord(0xE481) + bus.readWord(0x8C12);
    subs.call(bus.readWord(cpu.ar[0]), cpu, bus);
    bus.writeWord(cpu.ar[7] + kSkyBase, bus.readWord(kSkyOffset));
    for (uint32_t a : {0x6FEBu, 0x6754u, 0x8FF0u, 0x9019u, 0xAF55u}) subs.call(a, cpu, bus);
    cpu.ar[0] = bus.readWord(0xE481) + bus.readWord(0x3965);
    subs.call(bus.readWord(cpu.ar[0]), cpu, bus);
    bus.writeWord(0xE3FE, bus.readWord(0xE480));
    subs.call(0x3F8D, cpu, bus);
    // 05995  processo 0x5AA: +4 = 3 (sem o processo, grava no endereço 4, como no original)
    const uint32_t ar7 = cpu.ar[7];
    cpu.r[0] = 0x5AA;
    cpu.r[1] = 0xFFFFFFFFu;
    subs.call(0xA912, cpu, bus);
    cpu.ar[7] = cpu.ar[0];
    bus.writeWord(cpu.ar[7] + 4, 3);
    cpu.ar[7] = ar7;
    bus.writeWord(0xC938, bus.readWord(cpu.ar[7] + 0xC));
    cpu.r[0] = 1;
    bus.writeWord(0xC940, cpu.r[0]);
    bus.writeWord(0xC987, 0);
    spawn(cpu, bus, subs, 0xB0B1, 0x5C0);
    spawnFixedAndCars(cpu, bus, subs);
    cpu.r[0] = 0xB;
    bus.writeWord(0xE5F4, cpu.r[0]);
    cpu.r[0] = bus.readWord(kTrack) + 2;
    if (s(cpu.r[0]) > 0xE) cpu.r[0] = 0;
    bus.writeWord(kTrack, cpu.r[0]);
}

void registerAttractDemo(SubroutineDispatcher& d) {
    d.add(0x559E, demoStart);
    d.add(0x55B4, demoFocus);
    d.add(0x55C5, demoResume);
    d.add(0x55DA, frameObjStart);
    d.add(0x55F0, frameObjResume);
    d.add(0x5620, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.ar[6] = b.readWord(b.readWord(c.ar[7] + kTrackPair) + b.readWord(kScriptTable));
    });
    d.add(0x58E8, [](Tms32031State& c, MemoryBus&, ISubroutines&) { wrapPi(c); });
    // 058DD  R0 mod [9304] (2π) e depois 0x58E8
    d.add(0x58DD, [](Tms32031State& c, MemoryBus& b, ISubroutines& sub) {
        const double twoPi = rf(b, kTwoPi);
        const double r0 = tmsToDouble(c.r[0]);
        if (std::fabs(r0) >= twoPi) {
            c.r[1] = b.readWord(kTwoPi);
            sub.call(0xA509, c, b);
            const double turns = static_cast<double>(s(tmsFix(tmsToDouble(c.r[0])))) * twoPi;
            c.r[1] = doubleToTms(turns);
            c.r[0] = doubleToTms(r0 - turns);
        }
        wrapPi(c);
    });
    d.add(0x5943, applyDemoCamera);
    d.add(0x5968, skyOffset);
    d.add(0x596F, loadDemoTrack);
    d.add(0x59AF, spawnFixedAndCars);
}

} // namespace rea::core::game
