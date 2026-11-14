/**
 * @file attract.cpp
 * @brief Tradução de `0x3859`–`0x3888`.
 */
#include "core/game/attract.hpp"

namespace rea::core::game {

namespace {

using namespace attract_vars;

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// `DBU ARn`: decremento de 24 bits; desvia se o resultado não for negativo.
inline bool decrementAr(uint32_t& ar) {
    const uint32_t res = (ar - 1u) & 0x00FFFFFFu;
    ar = (ar & 0xFF000000u) | res;
    return (res & 0x00800000u) == 0;
}

/// Dorme um quadro pela `0xA874` e retoma o processo em @p resume.
void sleep1(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t resume) {
    cpu.ar[2] = 1;
    cpu.ret = resume;
    subs.call(0xA874, cpu, bus);
}

/// Trecho traduzido da ROM em `0x3869`.
void startScene(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[0] = 0;
    cpu.ar[6] = cpu.ar[5];
    cpu.ar[2] = bus.readWord(cpu.direct(kScenePcPtr));
    cpu.r[2] = kSceneId;
    subs.call(0xA844, cpu, bus);
    cpu.ar[4] = cpu.ar[0];
    cpu.ar[6] = kSceneFrames;
    sleep1(cpu, bus, subs, 0x3872);  // 03870
}

/// Trecho traduzido da ROM em `0x3859`.
void controller(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    for (uint32_t i = 0; i < 5; ++i) {
        cpu.ar[2] = bus.readWord(cpu.direct(kLoadPtrs + i));
        subs.call(0x9F2F, cpu, bus);
    }
    cpu.ar[2] = bus.readWord(cpu.direct(kLoad9DF6Ptr));
    subs.call(0x9DF6, cpu, bus);
    cpu.r[0] = 0;
    bus.writeWord(cpu.direct(kInputLatch), cpu.r[0]);
    bus.writeWord(cpu.direct(kInputSeen), cpu.r[0]);
    cpu.ar[5] = bus.readWord(cpu.direct(kStartScene));
    startScene(cpu, bus, subs);
}

/// Trecho traduzido da ROM em `0x3872`.
void waitScene(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = bus.readWord(cpu.direct(kInputLatch));
    if (cpu.r[0] == bus.readWord(cpu.direct(kInputSeen))) {
        // 03879  DBU AR6,$3870
        if (decrementAr(cpu.ar[6])) {
            sleep1(cpu, bus, subs, 0x3872);
            return;
        }
        subs.call(0x6FEB, cpu, bus);
        cpu.ar[5] += 1;
        if (s(cpu.ar[5]) >= static_cast<int32_t>(kSceneCount) || (cpu.ar[5] & 1u) == 0) {
            subs.call(0x1E1C, cpu, bus);  // BR $1E1C
            return;
        }
        startScene(cpu, bus, subs);  // BNE $3869
        return;
    }
    // 03875  [E5FE] = R0; R0 == −1 → AR5 = −1
    bus.writeWord(cpu.direct(kInputSeen), cpu.r[0]);
    if (cpu.r[0] == 0xFFFFFFFFu) cpu.ar[5] = 0xFFFFFFFFu;
    // 03881
    cpu.ar[5] += 1;
    if (s(cpu.ar[5]) >= static_cast<int32_t>(kSceneCount)) {
        subs.call(0x1E1C, cpu, bus);
        return;
    }
    cpu.r[0] = kSceneId;
    cpu.r[1] = 0x0FF0;
    subs.call(0xA8CF, cpu, bus);
    subs.call(0x6FEB, cpu, bus);
    startScene(cpu, bus, subs);
}

} // namespace

void registerAttract(SubroutineDispatcher& d) {
    d.add(0x3859, controller);
    d.add(0x3869, startScene);
    d.add(0x3872, waitScene);
}

} // namespace rea::core::game
