/**
 * @file track_stream.cpp
 * @brief Tradução de `0x3F6F`–`0x4058` e `0x405A`–`0x4224`.
 */
#include "core/track/track_stream.hpp"

#include "core/tms_float.hpp"

namespace rea::core::track {

namespace {

constexpr uint32_t kWindow    = 0x3F69; ///< `[3F69]` = 0xE408: janela de trechos.
constexpr uint32_t kSortedPtr = 0x3F68; ///< `[3F68]` = 0xE407: nós do trecho em carga.
constexpr uint32_t kLoadDist  = 0x3F6C; ///< Distância para carregar.
constexpr uint32_t kFreeDist  = 0x3F6D; ///< Distância para liberar.
constexpr uint32_t kFreeDist2 = 0x3F6E; ///< Idem, no modo 2.
constexpr uint32_t kHandlers  = 0x4500; ///< `[4500]`: (tipo, tratador)…, fim = 0.

/// Lê o float TMS em @p a e o converte para double.
double rf(MemoryBus& b, uint32_t a) { return tmsToDouble(b.readWord(a)); }
/// Converte @p v para float TMS e grava em @p a.
void wf(MemoryBus& b, uint32_t a, double v) { b.writeWord(a, doubleToTms(v)); }
/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
int32_t si(uint32_t v) { return static_cast<int32_t>(v); }

/// Tamanho de um registro de trecho (6 + opcionais).
uint32_t recordEnd(MemoryBus& b, uint32_t rec) {
    const uint32_t f = b.readWord(rec);
    uint32_t p = rec + 6;
    if (f & 1u) p += 1;
    if (f & 8u) p += 4;
    if (f & 0x1000u) p += 1;
    return p;
}

/// Dorme @p n quadros pela `0xA874` e retoma em @p resume.
void sleepFrames(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t n, uint32_t resume) {
    cpu.ar[2] = n;
    cpu.ret = resume;
    subs.call(0xA874, cpu, bus);
}

/// 03F6F  posiciona no trecho [E3FE].
void seekStart(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    cpu.ar[1] = bus.readWord(0xA103);
    cpu.r[0] = bus.readWord(0xE3FE);
    for (uint32_t i = 0; i < cpu.r[0]; ++i) cpu.ar[1] = recordEnd(bus, cpu.ar[1]);
    bus.writeWord(0xE475, cpu.ar[1]);
    bus.writeWord(0xE46D, cpu.ar[1]);
    bus.writeWord(0xE474, cpu.r[0]);
    bus.writeWord(0xE477, cpu.r[0]);
    for (uint32_t i = 0; i < 4; ++i) bus.writeWord(0xE3FF + i, bus.readWord(++cpu.ar[1]));
    cpu.r[2] = bus.readWord(cpu.ar[1]);
}

/// Grava a entrada `slot` da janela com o resultado de 0x405A (R0 = marca, AR0 = registro).
void storeEntry(Tms32031State& cpu, MemoryBus& bus, uint32_t entry) {
    bus.writeWord(entry, cpu.r[0]);
    bus.writeWord(entry + 1, cpu.ar[0]);
    bus.writeWord(entry + 2, bus.readWord(0xE473));
    bus.writeWord(entry + 4, bus.readWord(0xE474));
    bus.writeWord(entry + 3, bus.readWord(0xE47A));
    bus.writeWord(0xE46C, bus.readWord(0xE46C) + 1);
}

/// 03F8D  início do fluxo: 5 trechos e o processo 0x3FE3.
void streamStart(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.r[0] = 0;
    for (uint32_t a : {0xE46Cu, 0xE470u, 0xE46Fu, 0xE405u, 0xE477u}) bus.writeWord(a, 0);
    subs.call(0x3F6F, cpu, bus);
    // 03FA0  1º trecho
    cpu.ar[2] = bus.readWord(0xE46D);
    cpu.ar[0] = cpu.ar[2];
    subs.call(0x405A, cpu, bus);
    cpu.ar[1] = bus.readWord(kWindow);
    storeEntry(cpu, bus, cpu.ar[1]);
    bus.writeWord(0xE477, bus.readWord(0xE474));
    cpu.ar[0] = bus.readWord(0xE46D);
    bus.writeWord(0xE476, cpu.ar[0]);
    bus.writeWord(0xE46D, recordEnd(bus, cpu.ar[0]));
    // 03FBD  mais 4
    const uint32_t ar6 = cpu.ar[6];
    cpu.ar[1] = bus.readWord(kWindow);
    for (int i = 0; i < 4; ++i) {
        cpu.ar[2] = bus.readWord(0xE46D);
        cpu.ar[0] = cpu.ar[2];
        subs.call(0x405A, cpu, bus);
        storeEntry(cpu, bus, cpu.ar[1] + 5);
        cpu.ar[0] = bus.readWord(0xE46D);
        bus.writeWord(0xE46D, recordEnd(bus, cpu.ar[0]));
        cpu.ar[1] += 5;
    }
    cpu.ar[6] = ar6;
    cpu.ar[2] = bus.readWord(0xB0C2);
    cpu.r[2] = 0x5AA;
    subs.call(0xA844, cpu, bus);
}

/// 03FE3  processo do fluxo (dorme 1 quadro fora dos modos 2/4; 3 quadros por passo).
void streamProcess(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t mode = bus.readWord(0xC8C5) & 0xFu;
    if (mode != 4 && mode != 2) {
        sleepFrames(cpu, bus, subs, 1, 0x3FE3);
        return;
    }
    if (mode == 4) {
        // 03FED  o carro de foco passou do trecho [E477]?
        cpu.ar[0] = bus.readWord(bus.readWord(0xE878) + 0x37);
        cpu.r[0] = bus.readWord(cpu.ar[0] + 0x1E);
        cpu.r[1] = bus.readWord(0xE477) << 8;
        if (si(cpu.r[0]) >= si(cpu.r[1])) {
            cpu.ar[2] = bus.readWord(0xE476);
            cpu.ar[0] = (bus.readWord(cpu.ar[2]) >> 16) & 0xFFu;
            cpu.r[0] = cpu.ar[0];
            if (cpu.r[0] != 0) subs.call(0xACCA, cpu, bus);
            bus.writeWord(0xE477, bus.readWord(0xE477) + 1);
            bus.writeWord(0xE476, recordEnd(bus, bus.readWord(0xE476)));
        }
    }
    // 04009  carrega o próximo trecho se estiver perto
    cpu.ar[0] = bus.readWord(0xE46D);
    cpu.ar[2] = cpu.ar[0] + 1;
    cpu.r[2] = bus.readWord(0x45);
    subs.call(0x4204, cpu, bus);
    if (!(tmsToDouble(cpu.r[0]) > rf(bus, kLoadDist))) {
        cpu.ar[2] = cpu.ar[0];
        subs.call(0x405A, cpu, bus);
        cpu.ar[1] = bus.readWord(0xE46C) * 5 + bus.readWord(kWindow);
        storeEntry(cpu, bus, cpu.ar[1]);
        bus.writeWord(0xE46D, recordEnd(bus, cpu.ar[0]));
    }
    // 0402B  libera o 1º trecho quando a câmera se afastou do seu primeiro ponto
    cpu.ar[2] = bus.readWord(0xE409) + 1;
    cpu.r[2] = bus.readWord(0x45);
    subs.call(0x4204, cpu, bus);
    cpu.ar[0] = bus.readWord(bus.readWord(0xE409) + 5);
    const double d = tmsToDouble(cpu.r[0]) - rf(bus, cpu.ar[0]);
    cpu.r[0] = doubleToTms(d);
    const double limit = (bus.readWord(0xC8C5) & 0xFu) == 2 ? rf(bus, kFreeDist2) : rf(bus, kFreeDist);
    if (!(d < limit)) {
        cpu.ar[2] = bus.readWord(0xE408);
        subs.call(0x41D1, cpu, bus);
        cpu.ar[2] = bus.readWord(0xE40F);
        bus.writeWord(0xE46F, cpu.ar[2]);
        bus.writeWord(0xE46E, cpu.ar[2]);
        cpu.r[0] = 0;
        bus.writeWord(cpu.ar[2] + 0x17, 0);
        // 04042  desloca a janela uma entrada (com RC < 0, o corpo roda uma vez sem RPTB)
        cpu.rc = bus.readWord(0xE46C) - 2;
        const int32_t times = si(cpu.rc) < 0 ? 1 : si(cpu.rc) + 1;
        cpu.ar[0] = bus.readWord(kWindow);
        cpu.ar[1] = cpu.ar[0] + 5;
        for (int32_t i = 0; i < times; ++i)
            for (int k = 0; k < 5; ++k) bus.writeWord(cpu.ar[0]++, bus.readWord(cpu.ar[1]++));
        bus.writeWord(0xE46C, bus.readWord(0xE46C) - 1);
    }
    sleepFrames(cpu, bus, subs, 3, 0x3FE3);
}

/// 040A7..0413B  objetos de uma lista de pontos (R4 = n − 1, AR5 = pontos, AR7 = registro).
void buildPoints(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, uint32_t tag) {
    do {
        cpu.ar[2] = bus.readWord(cpu.ar[5]++);
        subs.call(0x700A, cpu, bus);
        if (cpu.carry()) return;  // 040A9  sem objetos livres
        cpu.ar[4] = cpu.ar[0];
        const uint32_t o = cpu.ar[4];
        bus.writeWord(o + 0x1F, tag);
        for (uint32_t i = 0; i < 3; ++i)
            wf(bus, o + 1 + i, static_cast<double>(si(bus.readWord(cpu.ar[5]++))));
        const uint32_t flags = bus.readWord(0xE47A);
        if (flags & 8u) {
            // 040B7  deslocamento +7..+9 (uma palavra antes sem flags & 1)
            const uint32_t base = (flags & 1u) ? cpu.ar[7] : cpu.ar[7] - 1;
            for (uint32_t i = 0; i < 3; ++i) wf(bus, o + 1 + i, rf(bus, base + 7 + i) + rf(bus, o + 1 + i));
        }
        // 040C5 / 040DB  gira por [0x5A] e translada pela origem do registro
        cpu.r[2] = bus.readWord(0x5A);
        cpu.ar[2] = o + 1;
        cpu.r[3] = bus.readWord(0x5D);
        subs.call(0x941E, cpu, bus);
        cpu.ar[0] = bus.readWord(0x5D);
        for (uint32_t i = 0; i < 3; ++i) {
            cpu.r[1] = doubleToTms(rf(bus, cpu.ar[0]++) + rf(bus, cpu.ar[7] + 1 + i));
            bus.writeWord(o + 1 + i, cpu.r[1]);
        }
        cpu.r[2] = doubleToTms(rf(bus, cpu.ar[5]++) + rf(bus, 0xE47C));
        bus.writeWord(o + 0x15, cpu.r[2]);
        cpu.ar[2] = o + 4;
        subs.call(0x939D, cpu, bus);
        // 040F1  tipo e flags
        cpu.r[1] = bus.readWord(cpu.ar[5]++);
        cpu.r[2] = cpu.r[1];
        cpu.r[1] &= 0xFFFu;
        bus.writeWord(o + 0xF, cpu.r[1]);
        if (cpu.r[2] & 0x2000u) subs.call(0x4212, cpu, bus);
        cpu.r[0] = cpu.r[2];
        cpu.r[2] = (cpu.r[2] >> 16) & 0x3Bu;
        if (cpu.r[0] & 0x1000u) cpu.r[2] |= 1u << 26;
        cpu.r[2] |= bus.readWord(o + 0xE);
        bus.writeWord(o + 0xE, cpu.r[2]);
        cpu.ar[2] = o;
        subs.call(0x7093, cpu, bus);
        cpu.r[0] = bus.readWord(o + 0xF) & 0xF00u;
        auto link = [&](uint32_t head, uint32_t bit) {
            bus.writeWord(o + 0x20, bus.readWord(head));
            bus.writeWord(head, o);
            cpu.r[1] = (1u << bit) | bus.readWord(o + 0xE);
            bus.writeWord(o + 0xE, cpu.r[1]);
        };
        if (cpu.r[0] == 0x300u) {
            link(0xE403, 28);
        } else if (cpu.r[0] == 0x900u) {
            link(0xE406, 21);
        } else if (cpu.r[0] == 0x600u) {
            link(0xE405, 31);
        } else if (cpu.r[0] == 0xB00u) {
            // 04125  nó da pista
            cpu.r[1] = bus.readWord(0xE474) << 8;
            cpu.r[0] = bus.readWord(o + 0xF) & 0xFFu;
            if (bus.readWord(0xE47A) & 8u) cpu.r[0] = 0xFFu - cpu.r[0];
            cpu.r[0] |= cpu.r[1];
            bus.writeWord(o + 0x1E, cpu.r[0]);
            bus.writeWord(o + 0xF, 0x300);
            link(0xE403, 28);
            subs.call(0x4197, cpu, bus);
        }
        cpu.r[4] -= 1;
    } while (si(cpu.r[4]) >= 0);
}

/// 0405A  carrega o trecho [E475]. R0 = marca. Preserva R4, R5, AR0–AR2, AR4, AR5, AR7.
void buildSegment(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const auto saved = cpu;
    cpu.r[0] = bus.readWord(0xE474) + 1;
    bus.writeWord(0xE474, cpu.r[0]);
    if (cpu.r[0] == 4) {
        // 04067  placa no 4º trecho
        const uint32_t ar4 = cpu.ar[4];
        cpu.ar[2] = bus.readWord(0xB0C3);
        cpu.r[2] = 0x1A2C;
        cpu.r[3] = static_cast<uint32_t>(-0x109A);
        cpu.rc = 0x7F58 + bus.readWord(0xB0C4);
        subs.call(0x7309, cpu, bus);
        cpu.ar[4] = cpu.ar[0];
        cpu.r[2] = doubleToTms(shortFloat(0xDB33));
        cpu.ar[2] = cpu.ar[4] + 4;
        subs.call(0x938A, cpu, bus);
        bus.writeWord(cpu.ar[4] + 0xE, bus.readWord(cpu.ar[4] + 0xE) | 0x400u);
        cpu.ar[2] = bus.readWord(0xB0C5);
        subs.call(0x9EDB, cpu, bus);
        bus.writeWord(cpu.ar[4] + 0x10, cpu.r[0]);
        cpu.ar[2] = cpu.ar[4];
        subs.call(0x7093, cpu, bus);
        bus.writeWord(cpu.ar[4] + 0x1F, (bus.readWord(0xE474) << 8) | 0xAAu);
        cpu.ar[4] = ar4;
    }
    // 04080
    bus.writeWord(0xE471, 0xFFFFFFFFu);
    bus.writeWord(0xE47B, 0);
    bus.writeWord(0xE407, 0);
    cpu.ar[2] = bus.readWord(0xE475);
    cpu.ar[7] = cpu.ar[2];
    const uint32_t rec = cpu.ar[7];
    const uint32_t flags = bus.readWord(rec);
    bus.writeWord(0xE47A, flags);
    bus.writeWord(0xE475, recordEnd(bus, rec));
    bus.writeWord(0xE47C, bus.readWord(rec + 4));
    cpu.ar[2] = bus.readWord(0x5A);
    cpu.r[2] = bus.readWord(0xE47C);
    subs.call(0x939D, cpu, bus);
    const uint32_t tag = (bus.readWord(0xE474) << 8) | 0xAAu;
    bus.writeWord(0xE478, tag);
    // listas de pontos: principal (+5), depois +6 (flags & 1) ou a alternativa (flags & 0x1000)
    auto list = [&](uint32_t ptr) {
        cpu.ar[5] = ptr + 1;
        cpu.r[4] = bus.readWord(cpu.ar[5]++) - 1;
        buildPoints(cpu, bus, subs, tag);
    };
    auto alt = [&](bool hasOne) {
        // 0414E / 0416C
        bus.writeWord(0xE47B, 2);
        const uint32_t f = bus.readWord(rec);
        if (!(f & 0x1000u)) return false;
        const uint32_t ir = (f & 8u) ? (hasOne ? 0xB : 0xA) : (hasOne ? 7 : 6);
        const uint32_t ptr = bus.readWord(rec + ir);
        if (f & 8u) bus.writeWord(0xE47C, bus.readWord(rec + ((f & 1u) ? 0xA : 9)));
        bus.writeWord(0xE47A, bus.readWord(0xE47A) & ~8u);
        list(ptr);
        return true;
    };
    list(bus.readWord(rec + 5));
    // 0413D
    if (bus.readWord(0xE47B) == 0) {
        bus.writeWord(0xE47B, 1);
        if (bus.readWord(rec) & 1u) {
            list(bus.readWord(rec + 6));
            if (bus.readWord(0xE47B) != 2) alt(true);  // 0414C: [E47B] = 1 → alternativa
        } else if (bus.readWord(rec) != 2) {
            alt(false);  // 0416A
        }
    } else if (bus.readWord(0xE47B) != 2) {
        alt(true);
    }
    // 04188
    subs.call(0x41B2, cpu, bus);
    subs.call(0x41BE, cpu, bus);
    cpu.r[0] = bus.readWord(0xE478);
    for (int i : {4, 5}) cpu.r[i] = saved.r[i];
    for (int i : {0, 1, 2, 4, 5, 7}) cpu.ar[i] = saved.ar[i];
}

/// 04197  insere AR4 na lista ordenada [[3F68]] pelo índice +0x1E (próximo +0x21, anterior +0x17).
void sortedNode(Tms32031State& cpu, MemoryBus& bus, ISubroutines&) {
    cpu.ar[1] = bus.readWord(kSortedPtr);
    cpu.r[0] = bus.readWord(cpu.ar[1]);
    if (cpu.r[0] == 0) {
        bus.writeWord(cpu.ar[1], cpu.ar[4]);
        cpu.r[0] = 0;
        bus.writeWord(cpu.ar[4] + 0x21, 0);
        bus.writeWord(cpu.ar[4] + 0x17, 0);
        return;
    }
    cpu.ar[1] -= 0x21;
    cpu.ar[2] = cpu.ar[1];
    cpu.r[0] = bus.readWord(cpu.ar[4] + 0x1E);
    for (;;) {
        cpu.ar[0] = cpu.ar[1];
        cpu.r[1] = bus.readWord(cpu.ar[1] + 0x21);
        if (cpu.r[1] == 0) break;
        cpu.ar[1] = cpu.r[1];
        if (!(si(cpu.r[0]) > si(bus.readWord(cpu.ar[1] + 0x1E)))) break;
    }
    bus.writeWord(cpu.ar[4] + 0x21, cpu.r[1]);
    bus.writeWord(cpu.ar[0] + 0x21, cpu.ar[4]);
    if (cpu.ar[0] == cpu.ar[2]) cpu.ar[0] = 0;
    bus.writeWord(cpu.ar[4] + 0x17, cpu.ar[0]);
    if (cpu.r[1] != 0) bus.writeWord(cpu.r[1] + 0x17, cpu.ar[4]);
}

/// 041DE  libera da lista *R1 os objetos com marca AR2 (+0x1F).
void freeTagged(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    for (;;) {
        cpu.ar[1] = cpu.r[1];
        cpu.r[1] = bus.readWord(cpu.ar[1]);
        if (cpu.r[1] == 0) return;
        cpu.ar[0] = cpu.r[1];
        if (bus.readWord(cpu.ar[0] + 0x1F) != cpu.ar[2]) continue;
        bus.writeWord(cpu.ar[1], bus.readWord(cpu.ar[0]));
        const uint32_t ar2 = cpu.ar[2];
        cpu.ar[2] = cpu.ar[0];
        // bits de +0xE: 22 → 0xAF66, 23 → 0x70FB, 28 → 0x70E5, 21 → 0x70CA, 31 → 0x70CF
        static constexpr uint32_t kBits[][2] = {
            {22, 0xAF66}, {23, 0x70FB}, {28, 0x70E5}, {21, 0x70CA}, {31, 0x70CF}};
        for (const auto& b : kBits) {
            cpu.r[0] = bus.readWord(cpu.ar[2] + 0xE);
            if ((cpu.r[0] >> b[0]) & 1u) subs.call(b[1], cpu, bus);
        }
        cpu.ar[2] = ar2;
        cpu.r[0] = 0;
        bus.writeWord(cpu.ar[0] + 0x1F, 0);
        bus.writeWord(cpu.ar[0] + 0xE, 0);
        bus.writeWord(cpu.ar[0], bus.readWord(0xC983));
        bus.writeWord(0xC983, cpu.ar[0]);
        bus.writeWord(0xC989, bus.readWord(0xC989) + 1);
        cpu.r[1] = cpu.ar[1];
    }
}

} // namespace

void registerTrackStream(SubroutineDispatcher& d) {
    d.add(0x3F6F, seekStart);
    d.add(0x3F8D, streamStart);
    d.add(0x3FE3, streamProcess);
    d.add(0x405A, buildSegment);
    d.add(0x4197, sortedNode);
    // 041B2  [E471] = 1º nó do trecho ([E407]); [E472] = último. Preserva R1 e AR0.
    d.add(0x41B2, [](Tms32031State&, MemoryBus& b, ISubroutines&) {
        uint32_t n = b.readWord(0xE407);
        b.writeWord(0xE471, n);
        for (uint32_t next; (next = b.readWord(n + 0x21)) != 0;) n = next;
        b.writeWord(0xE472, n);
    });
    // 041BE  encadeia os nós do trecho no fim da lista global ([E46E] … [E470])
    d.add(0x41BE, [](Tms32031State& c, MemoryBus& b, ISubroutines&) {
        c.r[0] = b.readWord(0xE470);
        if (c.r[0] != 0) {
            c.ar[0] = c.r[0];
            c.r[0] = b.readWord(0xE471);
            b.writeWord(c.ar[0] + 0x21, c.r[0]);
            c.ar[1] = b.readWord(0xE472);
            b.writeWord(0xE470, c.ar[1]);
            c.ar[1] = c.r[0];
            b.writeWord(c.ar[1] + 0x17, c.ar[0]);
            b.writeWord(0xE473, c.r[0]);
            return;
        }
        c.r[0] = b.readWord(0xE472);
        b.writeWord(0xE470, c.r[0]);
        c.r[0] = b.readWord(0xE471);
        b.writeWord(0xE46E, c.r[0]);
        b.writeWord(0xE46F, c.r[0]);
        b.writeWord(0xE473, c.r[0]);
    });
    // 041D1  libera os objetos do trecho AR2 nas listas [0x40] e [0x41]. Preserva R0, R1, AR0, AR1.
    d.add(0x41D1, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t r0 = c.r[0], r1 = c.r[1], ar0 = c.ar[0], ar1 = c.ar[1];
        for (uint32_t head : {0x40u, 0x41u}) {
            c.r[1] = b.readWord(head);
            freeTagged(c, b, s);
        }
        c.r[0] = r0;
        c.r[1] = r1;
        c.ar[0] = ar0;
        c.ar[1] = ar1;
    });
    // 04204  R0 = distância no plano XZ entre [R2] e [AR2]; AR2 += 3. Preserva AR0 e R3.
    d.add(0x4204, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t ar0 = c.ar[0], r3 = c.r[3];
        const double dx = rf(b, c.r[2]) - rf(b, c.ar[2]);
        const double dz = rf(b, c.r[2] + 2) - rf(b, c.ar[2] + 2);
        c.ar[2] += 3;
        c.r[2] = doubleToTms(dx * dx + dz * dz);
        s.call(0xA5F1, c, b);
        c.ar[0] = ar0;
        c.r[3] = r3;
    });
    // 04212  tratador do tipo [AR4+0xF] na tabela [4500]. Preserva R0, R1 e AR0.
    d.add(0x4212, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        const uint32_t r0 = c.r[0], r1 = c.r[1], ar0 = c.ar[0];
        const uint32_t type = b.readWord(c.ar[4] + 0xF);
        for (uint32_t p = b.readWord(kHandlers);; p += 2) {
            if (b.readWord(p) == type) {
                s.call(b.readWord(p + 1), c, b);
                break;
            }
            if (b.readWord(p) == 0) break;
        }
        c.r[0] = r0;
        c.r[1] = r1;
        c.ar[0] = ar0;
    });
}

} // namespace rea::core::track
