/**
 * @file sound_commands.cpp
 * @brief Tradução de `0x90E6`–`0x92C7` (driver de comandos de som).
 */
#include "core/audio/sound_commands.hpp"

namespace rea::core::audio {

namespace {

using namespace snd_vars;

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

/// Desliga o GIE (início de seção crítica).
void gieOff(Tms32031State& cpu) { cpu.st &= ~Tms32031State::kStGie; }

/// Escreve as 3 palavras @p a, @p b, @p c na porta de saída.
void write3(MemoryBus& bus, uint32_t a, uint32_t b, uint32_t c) {
    bus.writeWord(kOutputPort, a);
    bus.writeWord(kOutputPort, b);
    bus.writeWord(kOutputPort, c);
}

/// Fase de endereço 0 do latch (`-0x100`, `-0x900`, `-0x100`).
void latchAddr0(MemoryBus& bus) { write3(bus, 0xFFFFFF00u, 0xFFFFF700u, 0xFFFFFF00u); }
/// Fase de dado com `LDI` (com sinal): `-0x100 | d`, `-0x500 | d`, `-0x100 | d`.
void latchData(MemoryBus& bus, uint32_t d) { write3(bus, 0xFFFFFF00u | d, 0xFFFFFB00u | d, 0xFFFFFF00u | d); }

/// Endereço da palavra 0 do canal @p ch.
uint32_t channelBase(MemoryBus& bus, uint32_t ch) { return ch * kChannelWords + bus.readWord(kChannelsPtr); }

/// 09120  envia R0 e (v << 8) | (~v & 0xFF), com v = R1 & 0xFF (o GIE só volta no fim)
void sendVolumePair(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[2] = 1;
    bus.writeWord(kHoldGie, cpu.ar[2]);
    cpu.ar[2] = cpu.r[0];
    subs.call(0x9211, cpu, bus);
    cpu.r[1] &= 0xFFu;
    cpu.ar[2] = 0;
    bus.writeWord(kHoldGie, cpu.ar[2]);
    cpu.r[0] = ~cpu.r[1] & 0xFFu;
    cpu.r[1] = (cpu.r[1] << 8) | cpu.r[0];
    cpu.ar[2] = cpu.r[1];
    subs.call(0x9211, cpu, bus);
}

/// 0910F  volume geral
void masterVolume(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t ar2 = cpu.ar[2], r0 = cpu.r[0], r1 = cpu.r[1];
    cpu.r[0] = 0x55AA;
    sendVolumePair(cpu, bus, subs);
    cpu.r[1] = r1;
    cpu.r[0] = r0;
    cpu.ar[2] = ar2;
}

/// 09114  volume do canal R0
void channelVolume(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t ar2 = cpu.ar[2], r0 = cpu.r[0], r1 = cpu.r[1];
    cpu.ar[2] = 1;
    bus.writeWord(kHoldGie, cpu.ar[2]);
    bus.writeWord(channelBase(bus, cpu.r[0]) + 5, cpu.r[1]);
    cpu.r[0] += 0x55AB;
    sendVolumePair(cpu, bus, subs);
    cpu.r[1] = r1;
    cpu.r[0] = r0;
    cpu.ar[2] = ar2;
}

/// 09176  zera os canais
void clearChannels(MemoryBus& bus) {
    const uint32_t base = bus.readWord(kChannelsPtr);
    for (uint32_t i = 0; i < kChannels * kChannelWords; ++i) bus.writeWord(base + i, 0);
}

/// 0918E  toca AR2 no canal 1 ou 2 com o volume R0 (já mascarado)
void playOnFreeChannel(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3], ar0 = cpu.ar[0];
    uint32_t pushedR0 = cpu.r[0];
    auto finish = [&](bool carry) {
        if (carry) cpu.st |= Tms32031State::kStCarry;
        else cpu.st &= ~Tms32031State::kStCarry;
        cpu.r[0] = pushedR0;
        cpu.ar[0] = ar0;
        cpu.r[3] = r3;
        cpu.r[2] = r2;
        cpu.r[1] = r1;
    };
    if ((bus.readWord(0xC8C5) & 0xFu) == 2) {
        finish(true);  // 091B8: atrato, sem som
        return;
    }
    // 09192  canal 1 ou 2, o de menor prioridade (empate: canal 1)
    cpu.ar[0] = 1;
    cpu.r[2] = bus.readWord(kChannel1);
    cpu.r[0] = cpu.r[2];
    cpu.r[2] &= 0xFFu;
    cpu.r[3] = bus.readWord(kChannel2);
    cpu.r[1] = cpu.r[3];
    cpu.r[3] &= 0xFFu;
    if (s(cpu.r[2]) > s(cpu.r[3])) {
        cpu.r[2] = cpu.r[3];
        cpu.r[0] = cpu.r[1];
        cpu.ar[0] = 2;
    }
    cpu.r[3] = cpu.ar[2];
    cpu.ar[2] += bus.readWord(kSoundTable);
    cpu.r[1] = bus.readWord(cpu.ar[2]) & 0xFFu;
    if (s(cpu.r[1]) < s(cpu.r[2]) || (cpu.r[1] == cpu.r[2] && (cpu.r[0] & 0x2000u)) || (cpu.r[0] & 0x1000u)) {
        finish(false);
        return;
    }
    // 091A8  ocupa o canal
    cpu.r[0] = cpu.ar[0];
    cpu.ar[0] = channelBase(bus, cpu.ar[0]);
    cpu.r[1] = pushedR0;  // POP R1
    if (bus.readWord(cpu.ar[0] + 5) != cpu.r[1]) channelVolume(cpu, bus, subs);
    cpu.r[2] = 1;
    bus.writeWord(cpu.ar[0] + 7, cpu.r[2]);
    bus.writeWord(cpu.ar[0] + 3, cpu.ar[2]);
    bus.writeWord(cpu.ar[0] + 4, cpu.ar[2]);
    bus.writeWord(cpu.ar[0] + 6, cpu.r[3]);
    cpu.r[0] -= 1;
    bus.writeWord(cpu.ar[0] + 8, cpu.r[0]);
    channelStep(cpu, bus, subs);
    pushedR0 = cpu.r[0];  // 091B7 PUSH R0
    finish(true);
}

/// 090F3  música [E916] e o processo [C6BA]
void startMusic(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, bool fromLink) {
    const uint32_t ar2 = cpu.ar[2], ar6 = cpu.ar[6];
    cpu.ar[6] = fromLink ? 1u : 0u;
    // 09108
    cpu.ar[2] = bus.readWord(kMusic);
    if (s(cpu.ar[2]) > 8) cpu.ar[2] = 0;
    cpu.ar[2] = bus.readWord(cpu.ar[2] + bus.readWord(kMusicTable));
    playSound(cpu, bus, subs);
    // 090F4  processo 0x5C6
    cpu.r[0] = 0x5C6;
    cpu.r[1] = 0xFFFFFFFFu;
    subs.call(0xA912, cpu, bus);
    if (cpu.ar[0] == 0) {
        cpu.ar[2] = bus.readWord(kMusicProc);
        cpu.r[2] = 0x5C6;
        subs.call(0xA844, cpu, bus);
    }
    cpu.r[0] = 0x28;
    bus.writeWord(kMusicTimer, cpu.r[0]);
    cpu.r[0] = 0;
    bus.writeWord(kMusicFlag, cpu.r[0]);
    if (cpu.ar[6] != 1) {
        cpu.r[0] = bus.readWord(0xC8E3);
        if (cpu.r[0] != 0) subs.call(0x7C07, cpu, bus);
    }
    cpu.ar[6] = ar6;
    cpu.ar[2] = ar2;
}

/// 09247  reinício da placa em etapas (uma por quadro)
void resetStep(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    gieOff(cpu);
    cpu.r[0] = bus.readWord(kResetStep) + 1;
    bus.writeWord(kResetStep, cpu.r[0]);
    if (cpu.r[0] == 2) {
        // 09254  reset em nível baixo, latch 0 ← 0x3C, solta o reset, latch 0 ← 0x1C
        cpu.r[0] = bus.readWord(kControl);
        cpu.r[1] = cpu.r[0];
        cpu.r[0] &= ~2u;
        bus.writeWord(kControl, cpu.r[0]);
        bus.writeWord(kControlPort, cpu.r[0]);
        latchAddr0(bus);
        latchData(bus, 0x3C);
        bus.writeWord(kControlPort, cpu.r[1]);
        bus.writeWord(kControl, cpu.r[1]);
        latchAddr0(bus);
        latchData(bus, 0x1C);
        cpu.r[0] = 0xFFFFFF1Cu;
        cpu.r[1] = 0xFFFFFB1Cu;
    } else if (cpu.r[0] == 4) {
        // 09293  palavra 0 pela porta de dados
        cpu.ar[2] = bus.readWord(kDataPort2Ptr);
        cpu.r[1] = 0xFFFFFD00u;
        cpu.r[0] = 0xFFFFFF00u;
        bus.writeWord(kOutputPort, cpu.r[0]);
        bus.writeWord(cpu.ar[2], cpu.r[0]);
        bus.writeWord(kOutputPort, cpu.r[1]);
        bus.writeWord(kOutputPort, cpu.r[0]);
    } else if (cpu.r[0] == 6) {
        // 092A2  pronto: volume do item 8 da CMOS
        cpu.r[0] = 0;
        bus.writeWord(kResetting, cpu.r[0]);
        cpu.ar[2] = 8;
        subs.call(0x99E9, cpu, bus);
        cpu.r[1] = cpu.r[0];
        subs.call(0x910F, cpu, bus);
    }
    cpu.dp = 0;
    subs.call(0x4C33, cpu, bus);
}

} // namespace

void sendWord(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.dp = 0;
    if (bus.readWord(kResetting) != 0) return;
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], ar3 = cpu.ar[3];
    cpu.ar[3] = bus.readWord(kDataPortPtr);
    gieOff(cpu);
    auto sendByte = [&](uint32_t b) {
        cpu.r[0] = b | 0xFF00u;
        cpu.r[1] = b | 0xFD00u;
        bus.writeWord(kOutputPort, cpu.r[0]);
        bus.writeWord(cpu.ar[3], cpu.r[0]);
        bus.writeWord(kOutputPort, cpu.r[1]);
        bus.writeWord(kOutputPort, cpu.r[0]);
    };
    sendByte(cpu.ar[2] >> 8);  // LSH −8: os bits acima de 15 também vão (o original não mascara)
    cpu.ar[2] &= 0xFFu;
    sendByte(cpu.ar[2]);
    cpu.dp = 0;
    if (bus.readWord(kHoldGie) == 0) subs.call(0x4C33, cpu, bus);
    cpu.r[1] = r1;
    cpu.r[0] = r0;
    cpu.ar[3] = ar3;
}

void playSound(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t r0 = cpu.r[0], r1 = cpu.r[1], r2 = cpu.r[2], r3 = cpu.r[3], r4 = cpu.r[4], ar0 = cpu.ar[0];
    cpu.r[4] = cpu.ar[2];
    cpu.ar[2] += bus.readWord(kSoundTable);
    cpu.r[0] = bus.readWord(cpu.ar[2]);
    cpu.ar[0] = channelBase(bus, (cpu.r[0] >> 8) & 0xFu);
    cpu.r[1] = bus.readWord(cpu.ar[0]);
    bool play = false;
    if (!(cpu.r[1] & 0x1000u)) {
        cpu.r[2] = cpu.r[1];
        cpu.r[1] &= 0xFFu;
        cpu.r[3] = cpu.r[0] & 0xFFu;
        play = s(cpu.r[1]) < s(cpu.r[3]) || (cpu.r[1] == cpu.r[3] && !(cpu.r[2] & 0x2000u));
    }
    if (play) {
        // 091FA  volume volta a 0xFF; ocupa o canal
        if (bus.readWord(cpu.ar[0] + 5) != 0xFFu) {
            cpu.r[0] = (bus.readWord(cpu.ar[2]) >> 8) & 0xFu;
            cpu.r[1] = 0xFF;
            channelVolume(cpu, bus, subs);
        }
        cpu.r[2] = 1;
        bus.writeWord(cpu.ar[0] + 7, cpu.r[2]);
        bus.writeWord(cpu.ar[0] + 3, cpu.ar[2]);
        bus.writeWord(cpu.ar[0] + 4, cpu.ar[2]);
        cpu.r[0] = 0;
        bus.writeWord(cpu.ar[0] + 8, cpu.r[0]);
        bus.writeWord(cpu.ar[0] + 6, cpu.r[4]);
        channelStep(cpu, bus, subs);
    }
    cpu.ar[0] = ar0;
    cpu.r[4] = r4;
    cpu.r[3] = r3;
    cpu.r[2] = r2;
    cpu.r[1] = r1;
    cpu.r[0] = r0;
}

void channelStep(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t ar1 = cpu.ar[1];
    cpu.ar[1] = bus.readWord(cpu.ar[0] + 4);
    cpu.r[0] = bus.readWord(cpu.ar[1]++);
    if (cpu.r[0] != 0) {
        bus.writeWord(cpu.ar[0], cpu.r[0]);
        cpu.r[0] = bus.readWord(cpu.ar[1]);
        cpu.ar[2] = cpu.r[0] & 0x7FFFu;
        if (bus.readWord(cpu.ar[0] + 8) != 0) cpu.ar[2] += 1;
        subs.call(0x9211, cpu, bus);
        cpu.r[0] = bus.readWord(cpu.ar[1]++) >> 16;
        bus.writeWord(cpu.ar[0] + 2, cpu.r[0]);
        bus.writeWord(cpu.ar[0] + 4, cpu.ar[1]);
        if (s(cpu.r[0]) > 0) {
            cpu.ar[1] = ar1;
            return;
        }
    }
    // 092D9  fim da sequência
    cpu.r[0] = 0;
    bus.writeWord(cpu.ar[0], cpu.r[0]);
    bus.writeWord(cpu.ar[0] + 3, cpu.r[0]);
    bus.writeWord(cpu.ar[0] + 8, cpu.r[0]);
    cpu.ar[1] = ar1;
}

void soundFrame(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.dp = 0;
    cpu.r[0] = bus.readWord(kResetting);
    if (cpu.r[0] == 1) {
        resetStep(cpu, bus, subs);
        return;
    }
    const uint32_t r0 = cpu.r[0], r4 = cpu.r[4];
    cpu.ar[0] = bus.readWord(kChannelsPtr);
    for (cpu.r[4] = 3; s(cpu.r[4]) >= 0; --cpu.r[4]) {
        cpu.r[0] = bus.readWord(cpu.ar[0] + 2);
        if (cpu.r[0] != 0) {
            cpu.r[0] -= 1;
            bus.writeWord(cpu.ar[0] + 2, cpu.r[0]);
            if (cpu.r[0] == 0) channelStep(cpu, bus, subs);
        }
        cpu.ar[0] += kChannelWords;
    }
    cpu.r[4] = r4;
    cpu.r[0] = r0;
}

void registerSoundCommands(SubroutineDispatcher& d) {
    d.add(0x9211, sendWord);
    d.add(0x91E0, playSound);
    d.add(0x92C7, channelStep);
    d.add(0x92B3, soundFrame);
    d.add(0x910F, masterVolume);
    d.add(0x9114, channelVolume);
    d.add(0x9176, [](Tms32031State&, MemoryBus& b, ISubroutines&) { clearChannels(b); });
    // 09131  reinício completo da placa (com a espera longa) e canais zerados
    d.add(0x9131, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.r[0] = b.readWord(kControl);
        c.r[1] = c.r[0];
        c.r[0] &= ~2u;
        b.writeWord(kControl, c.r[0]);
        b.writeWord(kControlPort, c.r[0]);
        b.writeWord(kControlPort, c.r[1]);
        b.writeWord(kControl, c.r[1]);
        latchAddr0(b);
        latchData(b, 0x3C);
        latchData(b, 0x1C);  // sem a fase de endereço, como no original
        c.rc = 0xFFFFFFFFu;
        c.r[0] = 0xFFFFFF1Cu;
        c.r[1] = 0xFFFFFB1Cu;
        c.dp = 0;
        clearChannels(b);
    });
    // 09166  canais zerados; 0, 0x4E8, 0x55CC, 0
    d.add(0x9166, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t ar2 = c.ar[2];
        clearChannels(b);
        for (uint32_t w : {0u, 0x4E8u}) {
            c.ar[2] = w;
            s.call(0x9211, c, b);
        }
        c.ar[2] = 1;
        b.writeWord(kHoldGie, c.ar[2]);
        c.ar[2] = 0x55CC;
        s.call(0x9211, c, b);
        c.ar[2] = 0;
        b.writeWord(kHoldGie, c.ar[2]);
        s.call(0x9211, c, b);
        c.ar[2] = ar2;
    });
    // 0917F  R0 = prioridade do canal do som AR0
    d.add(0x917F, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.ar[0] = channelBase(b, (c.ar[0] >> 8) & 0xFu);
        c.r[0] = b.readWord(c.ar[0]) & 0xFFu;
    });
    d.add(0x9186, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.r[0] &= 0xFFu;
        playOnFreeChannel(c, b, s);
    });
    d.add(0x9188, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.r[0] = 0xFF;
        playOnFreeChannel(c, b, s);
    });
    // 091C1  troca o som que está no canal 1/2 pelo seu final (0x264/0x267)
    d.add(0x91C1, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t ar2 = c.ar[2];
        if (c.ar[2] == b.readWord(kChannel1Id)) {
            c.ar[2] = 0x264;
            s.call(0x91E0, c, b);
        } else if (c.ar[2] == b.readWord(kChannel2Id)) {
            c.ar[2] = 0x267;
            s.call(0x91E0, c, b);
            c.st |= Tms32031State::kStCarry;
        } else {
            c.st &= ~Tms32031State::kStCarry;
        }
        c.ar[2] = ar2;
    });
    // 091D0  0x55CC e R1 | R0 << 8
    d.add(0x91D0, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.r[0] <<= 8;
        c.r[1] |= c.r[0];
        c.r[0] = 1;
        b.writeWord(kHoldGie, c.r[0]);
        c.ar[2] = 0x55CC;
        s.call(0x9211, c, b);
        c.r[0] = 0;
        b.writeWord(kHoldGie, c.r[0]);
        c.ar[2] = c.r[1];
        s.call(0x9211, c, b);
    });
    // 091DA  16 bits baixos de [[90A6] + AR2 + 1]
    d.add(0x91DA, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        c.ar[2] = b.readWord(c.ar[2] + b.readWord(kSoundTable) + 1) & 0xFFFFu;
        s.call(0x9211, c, b);
    });
    d.add(0x9108, [](Tms32031State& c, MemoryBus& b, ISubroutines& sub) {
        c.ar[2] = b.readWord(kMusic);
        if (s(c.ar[2]) > 8) c.ar[2] = 0;
        c.ar[2] = b.readWord(c.ar[2] + b.readWord(kMusicTable));
        sub.call(0x91E0, c, b);
    });
    d.add(0x90E6, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        b.writeWord(kMusic, c.ar[2]);
        startMusic(c, b, s, true);
    });
    d.add(0x90EB, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        b.writeWord(kMusic, c.ar[2]);
        startMusic(c, b, s, false);
    });
    d.add(0x90F0, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { startMusic(c, b, s, false); });
    // 092AD  pede o reinício
    d.add(0x92AD, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.r[0] = 1;
        b.writeWord(kResetStep, c.r[0]);
        b.writeWord(kResetting, c.r[0]);
        c.dp = 0;
    });
}

} // namespace rea::core::audio
