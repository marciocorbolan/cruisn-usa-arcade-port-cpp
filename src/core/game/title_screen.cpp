/**
 * @file title_screen.cpp
 * @brief Tradução de `0xA93B`–`0xA995`, `0xA9C8`–`0xAA06`, `0x1DC3`–`0x1E16`, `0x59E3` e `0x8C14`.
 */
#include "core/game/title_screen.hpp"

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

using namespace title_vars;

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// Dorme um quadro pela `0xA874` e retoma o processo em @p resume.
void sleep1(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t resume) {
    cpu.ar[2] = 1;
    cpu.ret = resume;
    subs.call(0xA874, cpu, bus);
}

/// 059E3  carregador de recursos da pista [C8CF]
void loadTrackAssets(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[0] = bus.readWord(kTrack) + bus.readWord(kTrackTable);
    cpu.ar[0] = bus.readWord(cpu.ar[0] + 1);
    cpu.ar[0] += bus.readWord(kLoaderTable);
    cpu.r[0] = bus.readWord(cpu.ar[0]);
    subs.call(cpu.r[0], cpu, bus);  // CALLU R0
}

/// 01DC4  R6 −= 0,034912 × quadros; matriz Y em AR4 + 4; dorme 1 quadro (retoma em 01DCE → BU 01DC4)
void spinBody(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const double r0 = shortFloat(0xB0F0) * static_cast<double>(s(bus.readWord(kFrames)));
    cpu.r[0] = doubleToTms(r0);
    cpu.r[6] = doubleToTms(tmsToDouble(cpu.r[6]) - r0);
    cpu.r[2] = cpu.r[6];
    cpu.ar[2] = cpu.ar[4] + 4;
    subs.call(0x938A, cpu, bus);
    sleep1(cpu, bus, subs, 0x1DCE);
}

/// 01DCF  objeto girando em (0, 50, 368) e o seu processo
void spawnSpinner(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    subs.call(0x7025, cpu, bus);
    if (cpu.carry()) return;  // RETSLO: pool vazio
    cpu.r[0] = 0x00C2F984u;
    bus.writeWord(cpu.ar[0] + 0xD, cpu.r[0]);
    cpu.r[0] = 0xA3u | bus.readWord(cpu.ar[0] + 0xE);
    bus.writeWord(cpu.ar[0] + 0xE, cpu.r[0]);
    bus.writeWord(cpu.ar[0] + 1, doubleToTms(0.0));
    bus.writeWord(cpu.ar[0] + 2, doubleToTms(50.0));
    cpu.r[0] = doubleToTms(368.0);
    bus.writeWord(cpu.ar[0] + 3, cpu.r[0]);
    cpu.ar[2] = cpu.ar[0];
    subs.call(0x7070, cpu, bus);
    cpu.ar[4] = cpu.ar[2];
    const uint32_t ar0 = cpu.ar[0], ar2 = cpu.ar[2];
    cpu.ar[2] = bus.readWord(kSpinProcPtr);
    cpu.r[2] = 0x500;
    subs.call(0xA844, cpu, bus);
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
}

/// 0A974  ângulo Y do logotipo; matriz; dorme 1 quadro (retoma em 0A984)
void titleBody(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t obj = cpu.ar[4];
    double a = shortFloat(0xA0F0) * static_cast<double>(s(bus.readWord(kFrames)));
    a += tmsToDouble(bus.readWord(obj + 0x1E));
    bus.writeWord(obj + 0x1E, doubleToTms(a));
    a -= tmsToDouble(bus.readWord(kCamYaw));
    cpu.r[0] = doubleToTms(a);
    bus.writeWord(obj + 0x15, cpu.r[0]);
    cpu.ar[2] = obj + 4;
    cpu.r[2] = obj + 0x14;
    subs.call(0x9343, cpu, bus);
    sleep1(cpu, bus, subs, 0xA984);
}

/// 0A93B  tela de título
void titleProcess(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[6] = 0;
    if (s(bus.readWord(kAttractCase)) == -2 && bus.readWord(kTrack) == 0) {
        cpu.ar[2] = 0x1E;
        subs.call(0x99E9, cpu, bus);
        if (cpu.r[0] != 0) {
            // 0A945  música do atrato: volume = item 8 da CMOS
            cpu.ar[2] = 8;
            subs.call(0x99E9, cpu, bus);
            cpu.r[1] = cpu.r[0];
            subs.call(0x910F, cpu, bus);
            cpu.ar[6] = kMusicFrames;
            cpu.ar[2] = 0x75;
            subs.call(0x91E0, cpu, bus);
        }
    }
    subs.call(0x1DCF, cpu, bus);
    cpu.r[0] = 0;
    bus.writeWord(cpu.ar[7] + kCounterSlot, cpu.r[0]);
    bus.writeWord(kBackdrop, cpu.r[0]);
    cpu.r[0] = doubleToTms(-2780.0);
    bus.writeWord(kCamY, cpu.r[0]);
    // 0A955  logotipo
    cpu.ar[2] = bus.readWord(kLogoModelPtr);
    subs.call(0x700A, cpu, bus);
    cpu.ar[4] = cpu.ar[0];
    const uint32_t obj = cpu.ar[4];
    bus.writeWord(obj + 1, doubleToTms(0.0));
    bus.writeWord(obj + 2, doubleToTms(-910.0));
    bus.writeWord(obj + 3, doubleToTms(5500.0));
    cpu.r[1] = 1u << 16;
    cpu.r[0] = bus.readWord(obj + 0xE) | 0x61u | cpu.r[1];
    bus.writeWord(obj + 0xE, cpu.r[0]);
    bus.writeWord(obj + 0x14, doubleToTms(0.0));
    cpu.r[2] = doubleToTms(shortFloat(0x2061) + shortFloat(0x0491));
    bus.writeWord(obj + 0x15, cpu.r[2]);
    cpu.r[2] = doubleToTms(shortFloat(0xC32C));
    bus.writeWord(obj + 0x16, cpu.r[2]);
    cpu.ar[2] = obj + 4;
    cpu.r[2] = obj + 0x14;
    subs.call(0x9343, cpu, bus);
    cpu.ar[2] = obj;
    subs.call(0x7070, cpu, bus);
    // 0A972  [+1E] = ângulo Y inicial
    bus.writeWord(obj + 0x1E, bus.readWord(obj + 0x15));
    titleBody(cpu, bus, subs);
}

/**
 * 0A986  tempo da tela: AR6 −= quadros. Com [E5CA] = 0 por mais de 3 quadros e AR6 ≤ 0,
 * zera [C8E9] e salta para 0x1E1C (POP BK descarta o retorno). @return true se saiu.
 */
bool screenTimer(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[6] -= bus.readWord(kFrames);
    cpu.r[0] = bus.readWord(kInputIdle);
    if (cpu.r[0] != 0) return false;
    cpu.r[0] = bus.readWord(cpu.ar[7] + kCounterSlot) + 1;
    bus.writeWord(cpu.ar[7] + kCounterSlot, cpu.r[0]);
    if (!(s(cpu.r[0]) > 3) || s(cpu.ar[6]) > 0) return false;
    cpu.r[0] = 0;
    bus.writeWord(kExitFlag, cpu.r[0]);
    subs.call(0x1E1C, cpu, bus);
    return true;
}

/// 0A984  CALL A986 (tempo da tela); BR A974
void titleResume(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    if (screenTimer(cpu, bus, subs)) return;
    titleBody(cpu, bus, subs);
}

/// 01DE9  três placas (modelos [B227], [B229], [B22B]) com paletas [B228], [B22A], [B22C]; nada com [C8CC] & 4
void spawnSigns(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = bus.readWord(0xC8CC);
    if (cpu.r[0] & 4u) return;
    struct Sign { uint32_t model, palette; int32_t y, z; };
    static constexpr Sign kSigns[3] = {{kSignModels, kSignModels + 1, -0xFA, 0x39E},
                                       {kSignModels + 2, kSignModels + 3, -0x82, 0x39E},
                                       {kSignModels + 4, kSignModels + 5, -0xBE, 0x39C}};
    for (const Sign& sg : kSigns) {
        cpu.ar[2] = bus.readWord(sg.model);
        cpu.r[2] = 0xE6;
        cpu.r[3] = static_cast<uint32_t>(sg.y);
        cpu.rc = static_cast<uint32_t>(sg.z);
        subs.call(0x7309, cpu, bus);
        if (cpu.carry()) return;  // RETSLO
        cpu.r[0] = 0x4A3u | bus.readWord(cpu.ar[0] + 0xE);
        bus.writeWord(cpu.ar[0] + 0xE, cpu.r[0]);
        cpu.ar[2] = cpu.ar[0];
        subs.call(0x7093, cpu, bus);
        cpu.ar[2] = bus.readWord(sg.palette);
        subs.call(0x9EDB, cpu, bus);
        bus.writeWord(cpu.ar[0] + 0x10, cpu.r[0]);
    }
}

/// 0A9F9  logotipo da pista: ângulo Y += 0,017998 × quadros; matriz; dorme 1 quadro (retoma em 0AA05)
void trackLogoBody(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t obj = cpu.ar[4];
    cpu.r[0] = doubleToTms(static_cast<double>(s(bus.readWord(kFrames))) * shortFloat(0xA137));
    cpu.r[2] = doubleToTms(tmsToDouble(bus.readWord(obj + 0x15)) + tmsToDouble(cpu.r[0]));
    bus.writeWord(obj + 0x15, cpu.r[2]);
    cpu.r[2] = obj + 0x14;
    cpu.ar[2] = obj + 4;
    subs.call(0x9343, cpu, bus);
    sleep1(cpu, bus, subs, 0xAA05);
}

/// 0A9C8  caso −3 do atrato: moldura, placas e o logotipo girando da pista [C8CF]
void trackLogoProcess(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = 0;
    bus.writeWord(cpu.ar[7] + kCounterSlot, cpu.r[0]);
    cpu.ar[2] = kFrameModel;
    subs.call(0x700A, cpu, bus);
    cpu.r[0] = 0xA3u | bus.readWord(cpu.ar[0] + 0xE);
    bus.writeWord(cpu.ar[0] + 0xE, cpu.r[0]);
    bus.writeWord(cpu.ar[0] + 1, doubleToTms(-150.0));
    bus.writeWord(cpu.ar[0] + 2, doubleToTms(-110.0));
    cpu.r[0] = doubleToTms(436.0);
    bus.writeWord(cpu.ar[0] + 3, cpu.r[0]);
    cpu.ar[2] = cpu.ar[0];
    subs.call(0x7093, cpu, bus);
    subs.call(0x1DE9, cpu, bus);
    // 0A9DA  modelo [[A9C7] + ((([C8CF] >> 1) − 1) & 3)]
    cpu.ar[2] = ((bus.readWord(kTrack) >> 1) - 1u) & 3u;
    cpu.ar[2] += bus.readWord(kTrackLogoTable);
    cpu.ar[2] = bus.readWord(cpu.ar[2]);
    subs.call(0x700A, cpu, bus);
    cpu.r[0] = 0xA3u | bus.readWord(cpu.ar[0] + 0xE);
    bus.writeWord(cpu.ar[0] + 0xE, cpu.r[0]);
    cpu.r[0] = doubleToTms(1368.0);
    bus.writeWord(cpu.ar[0] + 3, cpu.r[0]);
    cpu.ar[4] = cpu.ar[0];
    const uint32_t obj = cpu.ar[4];
    cpu.r[2] = doubleToTms(shortFloat(0xDB75));
    bus.writeWord(obj + 0x14, cpu.r[2]);
    cpu.r[2] = doubleToTms(shortFloat(0x1623));
    bus.writeWord(obj + 0x15, cpu.r[2]);
    cpu.ar[2] = obj + 4;
    cpu.r[2] = obj + 0x14;
    subs.call(0x9343, cpu, bus);
    cpu.ar[2] = obj;
    subs.call(0x7070, cpu, bus);
    // 0A9F2  espera 16 quadros
    cpu.ar[6] = 0x10;
    sleep1(cpu, bus, subs, 0xA9F5);
}

} // namespace

void registerTitleScreen(SubroutineDispatcher& d) {
    d.add(0x59E3, loadTrackAssets);
    d.add(0x8C14, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[2] = b.readWord(0xC650);
        s.call(0xA3C2, c, b);
        c.ar[2] = b.readWord(0xC651);
        s.call(0xA3C2, c, b);
    });
    d.add(0x1DCF, spawnSpinner);
    d.add(0x1DC3, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.r[6] = doubleToTms(0.0);
        spinBody(c, b, s);
    });
    d.add(0x1DCE, spinBody);
    // 0A3D7  processo: espera [E5CA] == 0 e carrega a textura LZW AR4 (0xA3C2); morre
    auto lzwWait = [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[2] = 1;
        c.ret = 0xA3D9;
        s.call(0xA874, c, b);
    };
    d.add(0xA3D7, lzwWait);
    d.add(0xA3D9, [lzwWait](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.r[0] = b.readWord(kInputIdle);
        if (c.r[0] != 0) {
            lzwWait(c, b, s);
            return;
        }
        c.ar[2] = c.ar[4];
        s.call(0xA3C2, c, b);
        s.call(0xA896, c, b);  // BR $A896
    });
    d.add(0xA93B, titleProcess);
    d.add(0xA984, titleResume);
    d.add(0x1DE9, spawnSigns);
    d.add(0xA9C8, trackLogoProcess);
    // 0A9F5  AR6 −= 1; > 0 → dorme mais 1 quadro; senão AR6 = 0x226 e o laço do logotipo
    d.add(0xA9F5, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[6] -= 1;
        if (static_cast<int32_t>(c.ar[6]) > 0) {
            sleep1(c, b, s, 0xA9F5);
            return;
        }
        c.ar[6] = kTrackLogoFrames;
        trackLogoBody(c, b, s);
    });
    // 0AA05  CALL A986; BU A9F9
    d.add(0xAA05, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        if (screenTimer(c, b, s)) return;
        trackLogoBody(c, b, s);
    });
}

} // namespace rea::core::game
