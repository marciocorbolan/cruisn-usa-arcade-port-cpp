/**
 * @file race_traffic.cpp
 * @brief Tradução de `0x5268`–`0x526E` e `0x659F`–`0x668B`.
 */
#include "core/game/race_traffic.hpp"

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

constexpr uint32_t kFocusCar   = 0xE876; ///< objeto do carro focado (jogador/câmera)
constexpr uint32_t kFocusData  = 0xE878; ///< dados D do carro focado
constexpr uint32_t kCarList    = 0xE404;  ///< Lista de carros (`0x6762`).
constexpr uint32_t kPosition   = 0xE607; ///< posição do focado (1 = líder)
constexpr uint32_t kNearCount  = 0xE5F1; ///< carros com distância em −5000..30000
constexpr uint32_t kGridLock   = 0xE5EC;  ///< 0x33BB = carros presos no grid; 0x5577 = largada.
constexpr uint32_t kCarNumber  = 0xE5F5;  ///< Número do carro (começa em 0xA).

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
int32_t si(uint32_t v) { return static_cast<int32_t>(v); }
/// Lê o float TMS em @p a e o converte para double.
double rf(MemoryBus& b, uint32_t a) { return tmsToDouble(b.readWord(a)); }

/// Dorme @p frames quadros (`0xA874`) e retoma em @p pc.
void sleep(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t frames, uint32_t pc) {
    c.ar[2] = frames;
    c.ret = pc;
    s.call(0xA874, c, b);
}

/// Inteiro aleatório em 0..@p n − 1 (`0x8EDB`).
uint32_t rnd(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t n) {
    c.ar[2] = n;
    s.call(0x8EDB, c, b);
    return c.r[0];
}

/// Sorteio pela posição: limiares (≤ 4, ≤ 7, demais) em %. Reproduz os casos de igualdade, que
/// caem no teste seguinte (`BLT`/`BGT` sem `BEQ`).
bool rollByPosition(int32_t pos, int32_t r, int32_t t4, int32_t t7, int32_t tElse) {
    if (pos <= 4) {
        if (r < t4) return true;
        if (r > t4) return false;
    }
    if (pos <= 7) {
        if (r < t7) return true;
        if (r > t7) return false;
    }
    return r <= tElse;
}

/// 05268  R0 = `[D+0x57]` do carro `[E592]`, R1 = do focado; o original devolve as flags de R1 − R0.
int32_t compareLaps(Tms32031State& c, MemoryBus& b) {
    c.ar[0] = b.readWord(0xE592);
    c.ar[1] = b.readWord(c.ar[0] + 0x1B);
    c.r[0] = b.readWord(c.ar[1] + 0x57);
    c.ar[1] = b.readWord(kFocusData);
    c.r[1] = b.readWord(c.ar[1] + 0x57);
    return si(c.r[1]) - si(c.r[0]);
}

/// 065A5  posição do focado e contagem de carros próximos.
void countPosition(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    uint32_t r7 = 1, r6 = 0;
    c.ar[6] = b.readWord(c.ar[5] + 0x37);
    const int32_t myIdx = si(b.readWord(c.ar[6] + 0x1E));
    c.r[1] = static_cast<uint32_t>(myIdx);
    for (uint32_t car = b.readWord(kCarList); car != 0; car = b.readWord(car + 0x20)) {
        c.ar[0] = car;
        const int32_t dist = si(b.readWord(car + 0x1C));
        if (dist <= 0x7530 && dist >= -0x1388) ++r6;
        if (b.readWord(car + 0xF) != 0x216) continue;
        uint32_t ahead = 0;
        uint32_t d = b.readWord(car + 0x1B);
        uint32_t node = 0;
        bool sameNode = false;
        if (b.readWord(d + 0x58) != 0) {
            const int32_t laps = si(b.readWord(d + 0x57));
            if (laps != myIdx) {
                ahead = laps > myIdx ? 1 : 0;
            } else {
                if (car == b.readWord(0xE592)) c.ir0 = 1;
                b.writeWord(d + 0x37, c.ar[6]);
                c.ar[2] = c.ar[6];
                const uint32_t ar4 = c.ar[4], ar5 = c.ar[5];
                c.ar[4] = car;
                c.ar[5] = d;
                s.call(0x67E3, c, b);
                b.writeWord(d + 0x46, c.r[0]);
                c.ar[5] = ar5;
                c.ar[4] = ar4;
                node = c.ar[6];
                sameNode = true;
            }
        } else {
            const int32_t flag = si(b.readWord(b.readWord(car + 0x17) + 0x25));
            if (flag != 0) {
                ahead = flag > 0 ? 1 : 0;
            } else {
                d = b.readWord(car + 0x1B);
                node = b.readWord(d + 0x37);
                const int32_t idx = si(b.readWord(node + 0x1E));
                if (idx != myIdx) ahead = idx > myIdx ? 1 : 0;
                else sameNode = true;
            }
        }
        if (sameNode) {
            // 065D1  distâncias² ao nó seguinte menos o quadrado da distância lateral
            const uint32_t next = b.readWord(node + 0x21);
            const double cx = rf(b, next + 1) - rf(b, car + 1), cz = rf(b, car + 3) - rf(b, next + 3);
            const double lat = rf(b, d + 0x46);
            const double r4 = cx * cx + cz * cz - lat * lat;
            const double px = rf(b, next + 1) - rf(b, c.ar[4] + 1), pz = rf(b, c.ar[4] + 3) - rf(b, next + 3);
            const double myLat = rf(b, c.ar[5] + 0x46);
            const double r3 = pz * pz + px * px - myLat * myLat;
            ahead = r3 > r4 ? 1 : 0;
        }
        r7 += ahead;
    }
    b.writeWord(kPosition, r7);
    b.writeWord(kNearCount, r6);
    c.r[7] = r7;
    c.r[6] = r6;
}

/// 0659F  laço do processo da posição.
void positionProcess(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.ar[4] = b.readWord(kFocusCar);
    c.ar[5] = b.readWord(kFocusData);
    countPosition(c, b, s);
    sleep(c, b, s, 0xE, 0x659F);
}

// --- 0x65ED: gerador do processo [B12B] (ID 0x214) ----------------------------------------

void spawnA_cycle(Tms32031State& c, MemoryBus& b, ISubroutines& s);

/// 065F8  espera `[E5EB]`, o grid solto e a vez; sorteia e cria o carro.
void spawnA_wait(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    if (b.readWord(0xE5EB) == 0) return sleep(c, b, s, 1, 0x65F8);
    if (b.readWord(kGridLock) == 0x33BB) return spawnA_cycle(c, b, s);
    if (si(b.readWord(0xE5EE)) - 1 > si(b.readWord(0xE5F4))) return spawnA_cycle(c, b, s);
    const int32_t pos = si(b.readWord(kPosition));
    if (pos > 1) {
        const int32_t r = si(rnd(c, b, s, 0x64));
        if (!rollByPosition(pos, r, 0x46, 0x3C, 0x23)) return spawnA_cycle(c, b, s);
    }
    // 06613
    c.ar[2] = b.readWord(0xB12B);
    c.r[2] = 0x214;
    s.call(0xA844, c, b);
    if (c.st & Tms32031State::kStCarry) return spawnA_cycle(c, b, s);
    c.ar[5] = c.ar[0];
    sleep(c, b, s, 1, 0x661A);
}

/// 0661A  enquanto o carro criado estiver perto de `[E46D]`, espera.
void spawnA_follow(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.ar[4] = b.readWord(c.ar[5] + 9);
    if (b.readWord(c.ar[4] + 0xF) != 0x214) return spawnA_cycle(c, b, s);
    c.ar[0] = b.readWord(0xE46D);
    c.ar[2] = c.ar[0] + 1;
    c.r[2] = c.ar[4] + 1;
    s.call(0x4204, c, b);
    if (tmsToDouble(c.r[0]) < 25000.0) return sleep(c, b, s, 1, 0x661A);
    spawnA_cycle(c, b, s);
}

/// 065F2  espera 2·(`[E5F2]` + aleatório(`[E5F3]`)) quadros.
void spawnA_cycle(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    const uint32_t r = rnd(c, b, s, b.readWord(0xE5F3));
    sleep(c, b, s, (r + b.readWord(0xE5F2)) * 2, 0x65F8);
}

/// 065ED  entrada.
void spawnA(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    if (b.readWord(0xC8E3) != 0) return s.call(0xA896, c, b);
    sleep(c, b, s, 0x258, 0x65F2);
}

// --- 0x6639: gerador do processo [B12C] (ID 0x213) ----------------------------------------

/// 06680  R0 = 1 se pode criar.
void mayCreate(Tms32031State& c, MemoryBus& b, ISubroutines&) {
    if (b.readWord(0xC8E3) == 0) { c.r[0] = 1; return; }
    const int32_t cmp = compareLaps(c, b);
    if (cmp < 0) { c.r[0] = 0; return; }
    if (cmp > 0) { c.r[0] = 1; return; }
    c.r[0] = ((~b.readWord(0xC8CC)) & 1u) ? 1 : 0;
}

void spawnB_cycle(Tms32031State& c, MemoryBus& b, ISubroutines& s);

/// 0665D  mesmas condições do outro gerador, com limiares 75/60/35 e no máximo 8 carros próximos.
void spawnB_wait(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    if (b.readWord(0xE5EB) == 0) return sleep(c, b, s, 1, 0x665D);
    if (b.readWord(kGridLock) == 0x33BB) return spawnB_cycle(c, b, s);
    if (si(b.readWord(0xE5EE)) > si(b.readWord(0xE5F4))) return spawnB_cycle(c, b, s);
    const int32_t pos = si(b.readWord(kPosition));
    if (pos > 2) {
        const int32_t r = si(rnd(c, b, s, 0x64));
        if (!rollByPosition(pos, r, 0x4B, 0x3C, 0x23)) return spawnB_cycle(c, b, s);
    }
    // 06677
    if (si(b.readWord(kNearCount)) > 8) return spawnB_cycle(c, b, s);
    mayCreate(c, b, s);
    if (c.r[0] != 0) {
        c.ar[2] = b.readWord(0xB12C);
        c.r[2] = 0x213;
        s.call(0xA844, c, b);
    }
    spawnB_cycle(c, b, s);
}

/// 06653  espera 50 quadros (posição ≤ 2) ou `[E5F2]` + aleatório(`[E5F3]`).
void spawnB_cycle(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    uint32_t frames = 0x32;
    if (si(b.readWord(kPosition)) > 2) frames = rnd(c, b, s, b.readWord(0xE5F3)) + b.readWord(0xE5F2);
    sleep(c, b, s, frames, 0x665D);
}

/// 06645  fora do modo 2, espera o focado se afastar 11000 da posição inicial (R6, R7).
void spawnB_distance(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    const double dx = tmsToDouble(c.r[6]) - rf(b, c.ar[4] + 1);
    const double dz = tmsToDouble(c.r[7]) - rf(b, c.ar[4] + 3);
    c.r[2] = doubleToTms(dx * dx + dz * dz);
    s.call(0xA5F1, c, b);
    if (tmsToDouble(c.r[0]) > 11000.0) return spawnB_cycle(c, b, s);
    sleep(c, b, s, 1, 0x6645);
}

/// 06641
void spawnB_start(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    if ((b.readWord(0xC8C5) & 0xFu) == 2) return spawnB_cycle(c, b, s);
    spawnB_distance(c, b, s);
}

/// 0663C  guarda a posição inicial do focado.
void spawnB_origin(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    c.ar[4] = b.readWord(kFocusCar);
    c.r[6] = b.readWord(c.ar[4] + 1);
    c.r[7] = b.readWord(c.ar[4] + 3);
    sleep(c, b, s, 0x258, 0x6641);
}

/// 06636
void resetCarNumber(Tms32031State& c, MemoryBus& b, ISubroutines&) {
    c.r[0] = 0xA;
    b.writeWord(kCarNumber, c.r[0]);
}

/// 06639  entrada.
void spawnB(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    resetCarNumber(c, b, s);
    sleep(c, b, s, 0x1E, 0x663C);
}

/// 06628  numera o carro AR5.
void numberCar(Tms32031State& c, MemoryBus& b, ISubroutines&) {
    b.writeWord(c.ar[5] + 0x58, 0);
    uint32_t n = b.readWord(kCarNumber) + 1;
    if (si(n) > 0x7F) n = 0xA;
    b.writeWord(kCarNumber, n);
    c.r[1] = (b.readWord(0xC8CC) & 1u) == 0 ? 0x80u : 0u;
    c.r[0] = n | c.r[1];
    b.writeWord(c.ar[5] + 0x59, c.r[0]);
}

} // namespace

void registerRaceTraffic(SubroutineDispatcher& d) {
    d.add(0x5268, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { compareLaps(c, b); });
    d.add(0x659F, positionProcess);
    d.add(0x65A5, countPosition);
    d.add(0x65ED, spawnA);
    d.add(0x65F2, spawnA_cycle);
    d.add(0x65F8, spawnA_wait);
    d.add(0x661A, spawnA_follow);
    d.add(0x6628, numberCar);
    d.add(0x6636, resetCarNumber);
    d.add(0x6639, spawnB);
    d.add(0x663C, spawnB_origin);
    d.add(0x6641, spawnB_start);
    d.add(0x6645, spawnB_distance);
    d.add(0x6653, spawnB_cycle);
    d.add(0x665D, spawnB_wait);
    d.add(0x6680, mayCreate);
}

} // namespace rea::core::game
