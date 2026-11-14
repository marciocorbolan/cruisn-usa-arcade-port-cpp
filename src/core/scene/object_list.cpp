/**
 * @file object_list.cpp
 * @brief Tradução da ordenação/reagrupamento da lista de objetos (`0x7187`–`0x7251`, `0x87B8`–`0x87D6`,
 *        `0xAE05`–`0xAE39` e `0xAF75`–`0xAFC1`).
 */
#include "core/scene/object_list.hpp"

#include <cmath>

#include "core/tms_float.hpp"

namespace rea::core::scene {

using namespace objlist;

namespace {
/// Comparação com sinal (CMPI + BGE/BGT/BLT do TMS).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
} // namespace

void sortObjects(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    auto rd = [&](uint32_t a) { return bus.readWord(a); };
    // 07187  [C93F] = 1 (pede a troca de página)
    cpu.r[0] = 1;
    bus.writeWord(cpu.direct(kFlipRequest), cpu.r[0]);

    uint32_t budget = kSortStepBudget;
    for (;;) {
        // 07189  nova passada: R6 = 0 (houve troca?)
        cpu.r[6] = 0;
        // 0718A  AR0 = célula da cabeça; AR1 = 1º; AR2 = 2º (menos de 2 → 0x71A9)
        cpu.ar[0] = rd(cpu.direct(kHeadCellPtr));
        cpu.ar[1] = rd(cpu.ar[0]);
        cpu.r[1] = cpu.ar[1];
        if (cpu.r[1] == 0) break;
        cpu.ar[2] = rd(cpu.ar[1]);
        cpu.r[1] = cpu.ar[2];
        if (cpu.r[1] == 0) break;

        bool flipped = false;
        for (;;) {
            // 07191  LDI @($C93F),R0 / BEQD $71A9; atrasadas: R0 = AR1.chave, R1 = AR2.chave
            flipped = rd(cpu.direct(kFlipRequest)) == 0 || budget-- == 0;
            cpu.r[0] = rd(cpu.ar[1] + kKey);
            cpu.r[1] = rd(cpu.ar[2] + kKey);
            if (flipped) break;
            if (s(cpu.r[0]) >= s(cpu.r[1])) {
                // 071A3  em ordem: avança. R1 = AR2.next; BNED $7191;
                //        atrasadas: AR0 = AR1, AR1 = AR2, AR2 = R1
                cpu.r[1] = rd(cpu.ar[2]);
                cpu.ar[0] = cpu.ar[1];
                cpu.ar[1] = cpu.ar[2];
                cpu.ar[2] = cpu.r[1];
                if (cpu.r[1] != 0) continue;
                break;  // 071A8  BR $7189
            }
            // 07197  troca AR1 ↔ AR2: R6 = 1; *AR0 = AR2; AR1.next = AR2.next; AR2.next = AR1
            cpu.r[6] = 1;
            bus.writeWord(cpu.ar[0], cpu.ar[2]);
            cpu.r[1] = rd(cpu.ar[2]);
            bus.writeWord(cpu.ar[1], cpu.r[1]);
            bus.writeWord(cpu.ar[2], cpu.ar[1]);
            // 0719C  BNED $7191; atrasadas: AR0 = AR2, AR2 = R1 (AR1 já é o seguinte)
            cpu.ar[0] = cpu.ar[2];
            cpu.ar[2] = cpu.r[1];
            if (cpu.r[1] != 0) continue;
            break;  // 071A0  R6 = 1 aqui → BU $7189
        }
        if (flipped) break;
        // Port: passada sem trocas = lista estável. O hardware repetiria passadas
        // idênticas até a INT0 zerar [C93F]; seguimos direto para 0x71A9.
        if (cpu.r[6] == 0) break;
    }

    // 071A9  se [C8C5] & 0x20: RETS
    cpu.r[0] = rd(cpu.direct(kGameMode)) & 0x0020u;
    if (cpu.r[0] != 0) return;
    // 071AC
    for (uint32_t addr : {0x71B3u, 0x71E4u, 0xAF75u, 0x71BFu, 0x87B8u, 0xAE05u}) subs.call(addr, cpu, bus);
}

void detachType100(Tms32031State& cpu, MemoryBus& bus) {
    // 071B3  AR1 = célula da cabeça
    cpu.ar[1] = bus.readWord(cpu.direct(kHeadCellPtr));
    for (;;) {
        // 071B4  R0 = *AR1; BEQD $71BD; atrasadas: AR0 = AR1, AR1 = R0, R1 = AR1.tipo
        cpu.r[0] = bus.readWord(cpu.ar[1]);
        cpu.ar[0] = cpu.ar[1];
        cpu.ar[1] = cpu.r[0];
        cpu.r[1] = bus.readWord(cpu.ar[1] + kType);
        if (cpu.r[0] == 0) break;
        // 071B9  tipo == 0x100: *AR0 = AR1.next (desliga da lista)
        if (cpu.r[1] == 0x0100) {
            cpu.r[0] = bus.readWord(cpu.ar[1]);
            bus.writeWord(cpu.ar[0], cpu.r[0]);
            break;
        }
    }
    // 071BD  [C98C] = AR1 (0 se não achou)
    bus.writeWord(cpu.direct(kDetached), cpu.ar[1]);
}

void reattachDetached(Tms32031State& cpu, MemoryBus& bus) {
    auto rd = [&](uint32_t a) { return bus.readWord(a); };
    // 071BF  R0 = AR5 = [C98C]; BEQD $71E3; atrasadas: R2 = chave, R3 = coord
    cpu.r[0] = rd(cpu.direct(kDetached));
    cpu.ar[5] = cpu.r[0];
    cpu.r[2] = rd(cpu.ar[5] + kKey);
    cpu.r[3] = rd(cpu.ar[5] + kCoord);
    if (cpu.r[0] == 0) return;
    // 071C4  AR5.next = 0; AR1 = célula da cabeça
    cpu.r[0] = 0;
    bus.writeWord(cpu.ar[5], cpu.r[0]);
    cpu.ar[1] = rd(cpu.direct(kHeadCellPtr));
    for (;;) {
        // 071DD  R0 = *AR1; BNED $71C8; atrasadas: AR0 = AR1, AR1 = R0, R1 = AR1.tipo
        cpu.r[0] = rd(cpu.ar[1]);
        cpu.ar[0] = cpu.ar[1];
        cpu.ar[1] = cpu.r[0];
        cpu.r[1] = rd(cpu.ar[1] + kType);
        if (cpu.r[0] == 0) break;
        // 071C8  tipo & 0xFF0 == 0x210?
        cpu.r[1] &= 0x0FF0u;
        if (cpu.r[1] != 0x0210) continue;
        // 071CB  CMPI *+AR1(1C),R2 / BLT: pula se chave_C98C < chave
        if (s(cpu.r[2]) < s(rd(cpu.ar[1] + kKey))) continue;
        // 071CD  R1 = FIX(|coord − R3|); pula se > 2000
        const double diff = tmsToDouble(rd(cpu.ar[1] + kCoord)) - tmsToDouble(cpu.r[3]);
        cpu.r[1] = tmsFix(std::fabs(diff));
        if (s(cpu.r[1]) > 0x07D0) continue;
        // 071D3  AR2 = último da cadeia de AR5
        cpu.ar[2] = cpu.ar[5];
        for (;;) {
            cpu.r[1] = rd(cpu.ar[2]);
            if (cpu.r[1] == 0) break;
            cpu.ar[2] = cpu.r[1];
        }
        // 071D8  desliga AR1 da lista e o anexa à cadeia: *AR0 = AR1.next; *AR2 = AR1; AR1.next = 0
        cpu.r[0] = rd(cpu.ar[1]);
        bus.writeWord(cpu.ar[0], cpu.r[0]);
        bus.writeWord(cpu.ar[2], cpu.ar[1]);
        bus.writeWord(cpu.ar[1], cpu.r[1]);
        // 071DC  AR1 = AR0 (continua do predecessor)
        cpu.ar[1] = cpu.ar[0];
    }
    // 071E2  *AR0 = AR5: a cadeia vai para o fim da lista
    bus.writeWord(cpu.ar[0], cpu.ar[5]);
}

void regroupType200(Tms32031State& cpu, MemoryBus& bus) {
    auto rd = [&](uint32_t a) { return bus.readWord(a); };
    // 071E4  BUD $71F0; atrasadas: AR4 = 0 (pilha), AR1 = célula da cabeça
    cpu.ar[4] = 0;
    cpu.ar[1] = rd(cpu.direct(kHeadCellPtr));
    for (;;) {
        // 071F0  R0 = *AR1; BNED $71E8; atrasadas: AR0 = AR1, AR1 = R0, R1 = AR1.tipo
        cpu.r[0] = rd(cpu.ar[1]);
        cpu.ar[0] = cpu.ar[1];
        cpu.ar[1] = cpu.r[0];
        cpu.r[1] = rd(cpu.ar[1] + kType);
        if (cpu.r[0] == 0) break;
        // 071E8  tipo & 0xF00 == 0x200: desliga e empilha em AR4
        cpu.r[1] &= 0x0F00u;
        if (cpu.r[1] != 0x0200) continue;
        cpu.r[0] = rd(cpu.ar[1]);
        bus.writeWord(cpu.ar[0], cpu.r[0]);
        bus.writeWord(cpu.ar[1], cpu.ar[4]);
        cpu.ar[4] = cpu.ar[1];
        cpu.ar[1] = cpu.ar[0];
    }
    // 071F5  pilha vazia: RETS
    cpu.r[0] = cpu.ar[4];
    if (cpu.r[0] == 0) return;

    do {
        // 071F7  AR5 = objeto; AR4 = resto da pilha; PUSH AR4, AR5
        cpu.ar[5] = cpu.ar[4];
        cpu.ar[4] = rd(cpu.ar[4]);
        const uint32_t saved_ar4 = cpu.ar[4], saved_ar5 = cpu.ar[5];
        // 071FB  AR0 = [+1B]; AR1..AR5 = vizinhos; R1..R5 = chaves
        cpu.ar[0] = rd(cpu.ar[5] + kNeighbors);
        constexpr uint32_t kSlot[5] = {0x05, 0x0B, 0x11, 0x17, 0x1D};
        for (int i = 0; i < 5; ++i) cpu.ar[1 + i] = rd(cpu.ar[0] + kSlot[i]);
        for (int i = 0; i < 5; ++i) cpu.r[1 + i] = rd(cpu.ar[1 + i] + kKey);
        // 07206  R6 = 0x7FFF0000; vizinho nulo → chave R6 (LDIEQ)
        cpu.r[6] = 0x7FFFu << 16;
        for (int i = 0; i < 5; ++i) {
            cpu.r[0] = cpu.ar[1 + i];
            if (cpu.r[0] == 0) cpu.r[1 + i] = cpu.r[6];
        }
        // 07212  mínimo em cascata (LDIGT): o resultado fica em AR5
        if (s(cpu.r[2]) > s(cpu.r[1])) { cpu.ar[2] = cpu.ar[1]; cpu.r[2] = cpu.r[1]; }
        if (s(cpu.r[3]) > s(cpu.r[2])) { cpu.ar[3] = cpu.ar[2]; cpu.r[3] = cpu.r[2]; }
        if (s(cpu.r[4]) > s(cpu.r[3])) { cpu.ar[4] = cpu.ar[3]; cpu.r[4] = cpu.r[3]; }
        if (s(cpu.r[5]) > s(cpu.r[4])) { cpu.ar[5] = cpu.ar[4]; }
        // 0721D  AR3 = vizinho escolhido; POP AR5, AR4
        cpu.ar[3] = cpu.ar[5];
        cpu.ar[5] = saved_ar5;
        cpu.ar[4] = saved_ar4;

        // 07220  procura AR3 na lista a partir da cabeça
        cpu.ar[1] = rd(cpu.direct(kHeadCellPtr));
        cpu.r[3] = cpu.ar[3];
        bool found = false;
        if (cpu.r[3] != 0) {
            for (;;) {
                cpu.r[0] = rd(cpu.ar[1]);            // 07223
                if (cpu.r[0] == 0) break;
                cpu.ar[1] = cpu.r[0];                // 0722B
                if (cpu.r[0] == cpu.r[3]) { found = true; break; }
            }
        }
        if (!found) {
            // 07225  não achou: recomeça da célula da cabeça. Com [0x40] = 0xC982
            //        (≠ 0) o BNE $722E sempre desvia; 0x7228–0x722A não são alcançados.
            cpu.ar[1] = rd(cpu.direct(kHeadCellPtr));
            cpu.r[1] = cpu.ar[1];
        }

        // 0722E  1º vizinho do tipo 0x300?
        cpu.ar[3] = rd(cpu.ar[0] + 0x05);
        cpu.r[0] = rd(cpu.ar[3] + kType);
        cpu.r[1] = rd(cpu.ar[5] + kKey);
        if (cpu.r[0] == 0x0300) {
            // 07232  R2 = 0x310; R3 = 0xF00
            cpu.r[2] = 0x0310;
            cpu.r[3] = 0x0F00;
            for (;;) {
                // 0723D  R0 = *AR1; BNED $7236; atrasadas: AR2 = R0, R4 = AR2.tipo
                cpu.r[0] = rd(cpu.ar[1]);
                cpu.ar[2] = cpu.r[0];
                cpu.r[4] = rd(cpu.ar[2] + kType);
                if (cpu.r[0] == 0) break;           // 07242  BU $724C
                // 07236  tipo < 0x310 ou tipo & 0xF00 == 0x600 → testa a chave
                bool eligible = s(cpu.r[2]) > s(cpu.r[4]);
                if (!eligible) {
                    cpu.r[5] = cpu.r[4] & cpu.r[3];
                    eligible = cpu.r[5] == 0x0600;
                }
                // 0723A  chave ≥ AR2.chave → insere antes de AR2
                if (eligible && s(cpu.r[1]) >= s(rd(cpu.ar[2] + kKey))) break;
                cpu.ar[1] = cpu.ar[2];               // 0723C
            }
        } else {
            for (;;) {
                // 07247  R0 = *AR1; BNED $7245; atrasadas: AR2 = R0, CMPI AR2.chave,R1
                cpu.r[0] = rd(cpu.ar[1]);
                cpu.ar[2] = cpu.r[0];
                const uint32_t key = rd(cpu.ar[2] + kKey);
                if (cpu.r[0] == 0) break;
                if (s(cpu.r[1]) >= s(key)) break;    // 07245  BGE $724C
                cpu.ar[1] = cpu.ar[2];               // 07246
            }
        }
        // 0724C  insere AR5 depois de AR1
        cpu.r[0] = rd(cpu.ar[1]);
        bus.writeWord(cpu.ar[1], cpu.ar[5]);
        bus.writeWord(cpu.ar[5], cpu.r[0]);
        // 0724F  próximo da pilha
        cpu.r[0] = cpu.ar[4];
    } while (cpu.r[0] != 0);
}

namespace {
/// Move o nó AR1 (predecessor AR6) para o início da lista cuja célula está em AR5; AR1 volta ao predecessor.
void moveToList(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[0] = bus.readWord(cpu.ar[1]);
    bus.writeWord(cpu.ar[6], cpu.r[0]);
    cpu.r[0] = bus.readWord(cpu.ar[1] + cull::kListFlags) ^ 0x3000u;
    bus.writeWord(cpu.ar[1] + cull::kListFlags, cpu.r[0]);
    cpu.r[0] = bus.readWord(cpu.ar[5]);
    bus.writeWord(cpu.ar[1], cpu.r[0]);
    bus.writeWord(cpu.ar[5], cpu.ar[1]);
    cpu.ar[1] = cpu.ar[6];
}
} // namespace

void promoteNear(Tms32031State& cpu, MemoryBus& bus) {
    auto f = [&](uint32_t a) { return tmsToDouble(bus.readWord(a)); };
    // 0726F  R3 = −5000,0; R4 = [7252]; AR4 = câmera + 1; AR1 = distante; AR5 = visível; AR3 = matriz + 7
    const double r3 = static_cast<double>(cull::kBehind);
    cpu.r[3] = doubleToTms(r3);
    cpu.r[4] = bus.readWord(cpu.direct(cull::kNearRangePtr));
    cpu.ar[4] = bus.readWord(cpu.direct(cull::kCamPosPtr)) + 1;
    cpu.ar[1] = bus.readWord(cpu.direct(cull::kFarHeadPtr));
    cpu.ar[5] = bus.readWord(cpu.direct(objlist::kHeadCellPtr));
    cpu.ar[3] = bus.readWord(cpu.direct(cull::kCamMatrixPtr)) + 7;
    cpu.ir0 = 2;
    cpu.ir1 = 3;
    double r5 = 0, r6 = 0, r7 = 0, r2 = 0;
    for (;;) {
        // 07292  R0 = próximo; AR6 = AR1; AR1 = R0; R5 = x − cam.x (slots do BNED)
        cpu.r[0] = bus.readWord(cpu.ar[1]);
        cpu.ar[6] = cpu.ar[1];
        cpu.ar[1] = cpu.r[0];
        r5 = f(cpu.ar[1] + 1) - f(cpu.ar[4] - 1);
        if (cpu.r[0] == 0) break;
        // 0727A  profundidade = (pos − câmera) · eixo
        r6 = f(cpu.ar[1] + cpu.ir0) - f(cpu.ar[4]);
        r7 = f(cpu.ar[1] + cpu.ir1) - f(cpu.ar[4] + 1);
        const double d0 = r5 * f(cpu.ar[3] - 1);
        const double d1 = r6 * f(cpu.ar[3]);
        r2 = r7 * f(cpu.ar[3] + 1);
        const double depth = d0 + d1 + r2;
        cpu.r[0] = doubleToTms(depth);
        // 07282  BLED: FIX e STI executam nos slots
        cpu.r[1] = tmsFix(depth);
        bus.writeWord(cpu.ar[1] + objlist::kKey, cpu.r[1]);
        if (depth <= r3) continue;
        cpu.r[1] -= cpu.r[4];
        cpu.r[1] -= bus.readWord(cpu.ar[1] + cull::kRadius);
        if (s(cpu.r[1]) > 0) continue;
        moveToList(cpu, bus);  // 07289
    }
    cpu.r[5] = doubleToTms(r5);
    cpu.r[6] = doubleToTms(r6);
    cpu.r[7] = doubleToTms(r7);
    cpu.r[2] = doubleToTms(r2);
}

void demoteFar(Tms32031State& cpu, MemoryBus& bus) {
    // 07254  R4 = [7253]; AR5 = distante; AR1 = visível
    cpu.r[4] = bus.readWord(cpu.direct(cull::kFarRangePtr));
    cpu.ar[5] = bus.readWord(cpu.direct(cull::kFarHeadPtr));
    cpu.ar[1] = bus.readWord(cpu.direct(objlist::kHeadCellPtr));
    for (;;) {
        // 07269  R0 = próximo; AR6 = AR1; AR1 = R0
        cpu.r[0] = bus.readWord(cpu.ar[1]);
        cpu.ar[6] = cpu.ar[1];
        cpu.ar[1] = cpu.r[0];
        if (cpu.r[0] == 0) break;
        // 07258  profundidade ≤ −5000 → rebaixa; senão fica se prof − [7253] − raio ≤ 0
        cpu.r[0] = bus.readWord(cpu.ar[1] + objlist::kKey);
        if (s(cpu.r[0]) > cull::kBehind) {
            cpu.r[0] -= cpu.r[4];
            if (s(cpu.r[0]) <= 0) continue;
            cpu.r[0] -= bus.readWord(cpu.ar[1] + cull::kRadius);
            if (s(cpu.r[0]) <= 0) continue;
        }
        moveToList(cpu, bus);  // 07260
    }
}

void sortVisibleFull(Tms32031State& cpu, MemoryBus& bus) {
    uint32_t budget = kSortStepBudget;
    do {
        // 072D2  R2 = 0 (houve troca?)
        cpu.r[2] = 0;
        cpu.ar[0] = bus.readWord(cpu.direct(objlist::kHeadCellPtr));
        cpu.ar[1] = bus.readWord(cpu.ar[0]);
        cpu.r[1] = cpu.ar[1];
        if (cpu.r[1] == 0) break;
        cpu.ar[2] = bus.readWord(cpu.ar[1]);
        cpu.r[1] = cpu.ar[2];
        if (cpu.r[1] == 0) break;
        for (;;) {
            if (--budget == 0) return;
            // 072DB  a.chave < b.chave → troca (AR1 continua o mesmo nó, agora uma posição adiante)
            cpu.r[0] = bus.readWord(cpu.ar[1] + kKey);
            cpu.r[1] = bus.readWord(cpu.ar[2] + kKey);
            if (s(cpu.r[0] - cpu.r[1]) < 0) {
                cpu.r[2] = 1;
                bus.writeWord(cpu.ar[0], cpu.ar[2]);
                cpu.r[1] = bus.readWord(cpu.ar[2]);
                bus.writeWord(cpu.ar[1], cpu.r[1]);
                bus.writeWord(cpu.ar[2], cpu.ar[1]);
                cpu.ar[0] = cpu.ar[2];
                cpu.ar[2] = cpu.r[1];
            } else {
                // 072E8  avança
                cpu.r[0] = cpu.r[1];
                cpu.ar[0] = cpu.ar[1];
                cpu.ar[1] = cpu.ar[2];
                cpu.r[1] = bus.readWord(cpu.ar[2]);
                cpu.ar[2] = cpu.r[1];
            }
            if (cpu.r[1] == 0) break;
        }
    } while (cpu.r[2] != 0);
}

void rebuildVisibility(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    const uint32_t ar3 = cpu.ar[3], ar4 = cpu.ar[4], ar5 = cpu.ar[5], ar6 = cpu.ar[6];
    cpu.ar[5] = bus.readWord(cpu.direct(cull::kFarHeadPtr));
    cpu.ar[1] = bus.readWord(cpu.direct(objlist::kHeadCellPtr));
    cpu.r[0] = bus.readWord(cpu.ar[1]);
    if (cpu.r[0] != 0) {
        uint32_t guard = 0;
        do {
            cpu.ar[2] = cpu.r[0];
            cpu.r[0] = bus.readWord(cpu.ar[2] + cull::kListFlags) ^ 0x3000u;
            bus.writeWord(cpu.ar[2] + cull::kListFlags, cpu.r[0]);
            cpu.r[0] = bus.readWord(cpu.ar[2]);
        } while (cpu.r[0] != 0 && ++guard < kSortStepBudget);
        // 072A6  último → antiga cabeça distante; distante = visível; visível = vazia
        cpu.r[0] = bus.readWord(cpu.ar[5]);
        bus.writeWord(cpu.ar[2], cpu.r[0]);
        cpu.r[0] = bus.readWord(cpu.ar[1]);
        bus.writeWord(cpu.ar[5], cpu.r[0]);
        cpu.r[0] = 0;
        bus.writeWord(cpu.ar[1], cpu.r[0]);
    }
    subs.call(0x726F, cpu, bus);
    subs.call(0x72D2, cpu, bus);
    cpu.ar[6] = ar6;
    cpu.ar[5] = ar5;
    cpu.ar[4] = ar4;
    cpu.ar[3] = ar3;
}

void resetObjectPool(Tms32031State& cpu, MemoryBus& bus) {
    // 06FEB  PUSH R0, AR0, AR1
    const uint32_t r0 = cpu.r[0], ar0 = cpu.ar[0], ar1 = cpu.ar[1];
    cpu.r[0] = pool::kCount;
    bus.writeWord(cpu.direct(pool::kFreeCount), cpu.r[0]);
    cpu.r[0] = 0;
    for (uint32_t a : {0xC982u, 0xC984u, 0xC985u, 0xC988u, 0xC98Au, 0xC986u, 0xC987u, 0xE403u, 0xE404u, 0xE405u,
                       0xE406u, 0xC98Bu})
        bus.writeWord(cpu.direct(a), cpu.r[0]);
    // 06FFD  encadeia (RPTB, RC = 0x44B)
    cpu.ar[0] = bus.readWord(cpu.direct(pool::kFreeCellPtr));
    cpu.ar[1] = bus.readWord(cpu.direct(pool::kFirstPtr));
    cpu.rc = pool::kCount - 1;
    for (;;) {
        bus.writeWord(cpu.ar[0], cpu.ar[1]);
        cpu.ar[0] = cpu.ar[1];
        cpu.ar[1] += pool::kStride;
        if (s(--cpu.rc) < 0) break;
    }
    cpu.r[0] = 0;
    bus.writeWord(cpu.ar[0], cpu.r[0]);
    cpu.ar[1] = ar1;
    cpu.ar[0] = ar0;
    cpu.r[0] = r0;
}

namespace {

constexpr uint32_t kHeadCell = 0xC982; ///< `($C982)`: cabeça da lista visível (= `[[0x40]]`).

/// Liga ou desliga o bit C (carry) de ST, que a rotina original usa como retorno de sucesso ou falha.
void setCarry(Tms32031State& c, bool on) {
    if (on) c.st |= Tms32031State::kStCarry;
    else c.st &= ~Tms32031State::kStCarry;
}

/// 0AF75  objetos do tipo 0x7xx saem da lista visível e voltam logo depois do nó de pista mais próximo
/// (lista `[E46F]` por `+0x21`), herdando os bits de lista `+0xE & 0x3000` do nó. No modo 5 entram
/// depois dos objetos 0x310/0x400/0x40A que seguem o nó.
void attachToNodes(Tms32031State& cpu, MemoryBus& bus) {
    auto rd = [&](uint32_t a) { return bus.readWord(a); };
    auto f = [&](uint32_t a) { return tmsToDouble(bus.readWord(a)); };
    // 0AF75  AR4 = lista dos retirados; AR0 = anterior, AR1 = atual
    cpu.ar[4] = 0;
    cpu.ar[1] = rd(cpu.direct(kHeadCellPtr));
    for (;;) {
        cpu.r[0] = rd(cpu.ar[1]);
        cpu.ar[0] = cpu.ar[1];
        cpu.ar[1] = cpu.r[0];
        cpu.r[1] = rd(cpu.ar[1] + kType); // slot atrasado: lê mesmo com AR1 = 0
        if (cpu.r[0] == 0) break;
        if ((cpu.r[1] & 0x0F00u) != 0x0700u) continue;
        // 0AF7C  desliga AR1 e empilha em AR4
        cpu.r[0] = rd(cpu.ar[1]);
        bus.writeWord(cpu.ar[0], cpu.r[0]);
        bus.writeWord(cpu.ar[1], cpu.ar[4]);
        cpu.ar[4] = cpu.ar[1];
        cpu.ar[1] = cpu.ar[0];
    }
    // 0AF86
    cpu.r[5] = rd(0xE46F);
    cpu.ir0 = 3;
    cpu.r[0] = cpu.ar[4];
    if (cpu.r[5] == 0) return;
    while (cpu.ar[4] != 0) {
        // 0AFBE (atrasadas)
        cpu.ar[2] = cpu.r[5];
        cpu.ar[5] = cpu.r[5];
        double best = f(0xAF74);
        // 0AF8C  nó já atribuído e com bits de lista? mantém
        cpu.r[0] = rd(cpu.ar[4] + 0x1E);
        cpu.ar[5] = cpu.r[0];
        const bool keep = cpu.r[0] != 0 && (rd(cpu.ar[5] + 0xE) & 0x3000u) != 0;
        if (!keep) {
            // 0AF92  nó mais próximo no plano xz
            const double x = f(cpu.ar[4] + 1), z = f(cpu.ar[4] + 3);
            for (;;) {
                const double dx = x - f(cpu.ar[2] + 1), dz = z - f(cpu.ar[2] + 3);
                const double d2 = dz * dz + dx * dx;
                if (best > d2) {
                    cpu.ar[5] = cpu.ar[2];
                    best = d2;
                }
                cpu.r[0] = rd(cpu.ar[2] + 0x21);
                cpu.ar[2] = cpu.r[0];
                if (cpu.r[0] == 0) break;
            }
            bus.writeWord(cpu.ar[4] + 0x1E, cpu.ar[5]);
        }
        // 0AFA4  copia os bits de lista do nó
        cpu.r[1] = rd(cpu.ar[5] + 0xE) & 0x3000u;
        cpu.r[2] = rd(cpu.ar[4] + 0xE) & ~0x3000u;
        cpu.r[1] |= cpu.r[2];
        bus.writeWord(cpu.ar[4] + 0xE, cpu.r[1]);
        // 0AFA8  modo 5: pula os objetos 0x310/0x400/0x40A que seguem o nó
        if ((rd(cpu.direct(kGameMode)) & 0xFu) == 5) {
            for (cpu.ar[1] = cpu.ar[5];;) {
                cpu.ar[5] = cpu.ar[1];
                cpu.r[0] = rd(cpu.ar[5]);
                cpu.ar[1] = cpu.r[0];
                if (cpu.r[0] == 0) break;
                const uint32_t t = rd(cpu.ar[1] + kType);
                if (t != 0x310 && t != 0x400 && t != 0x40A) break;
            }
        }
        // 0AFB8  insere AR4 depois de AR5
        cpu.r[1] = rd(cpu.ar[5]);
        cpu.r[0] = rd(cpu.ar[4]);
        bus.writeWord(cpu.ar[5], cpu.ar[4]);
        bus.writeWord(cpu.ar[4], cpu.r[1]);
        cpu.ar[4] = cpu.r[0];
    }
    cpu.ar[2] = cpu.r[5];
    cpu.ar[5] = cpu.r[5];
    cpu.r[2] = rd(0xAF74);
}

/// 087B8  objetos 0x102 da lista visível passam para logo depois do objeto 0x100 (em ordem).
void gatherType102(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    auto rd = [&](uint32_t a) { return bus.readWord(a); };
    cpu.ar[2] = 0x100;
    subs.call(0x70BB, cpu, bus);
    if (!cpu.carry()) return;
    cpu.ar[5] = cpu.ar[0];
    cpu.ar[6] = cpu.ar[5];
    cpu.ar[4] = rd(kHeadCell);
    cpu.ar[2] = cpu.ar[4];
    while (cpu.ar[4] != cpu.ar[6] && cpu.ar[4] != 0) {
        if (rd(cpu.ar[4] + kType) != 0x102) {
            cpu.ar[2] = cpu.ar[4];
            cpu.ar[4] = rd(cpu.ar[4]);
            continue;
        }
        cpu.r[0] = rd(cpu.ar[4]);
        if (cpu.ar[4] == rd(kHeadCell)) bus.writeWord(kHeadCell, cpu.r[0]);
        else bus.writeWord(cpu.ar[2], cpu.r[0]);
        cpu.r[1] = rd(cpu.ar[5]);
        bus.writeWord(cpu.ar[5], cpu.ar[4]);
        bus.writeWord(cpu.ar[4], cpu.r[1]);
        cpu.ar[5] = cpu.ar[4];
        cpu.ar[4] = cpu.r[0];
    }
}

/// 0AE27  AR0 = próximo objeto depois de AR0 com tipo R1 (carry = 1 se não achou). Preserva R0 e AR5.
void nextOfType(Tms32031State& cpu, MemoryBus& bus) {
    uint32_t next = bus.readWord(cpu.ar[0]);
    for (;;) {
        cpu.ar[0] = next;
        if (next == 0) break;
        next = bus.readWord(cpu.ar[0]);
        if (bus.readWord(cpu.ar[0] + kType) == cpu.r[1]) break;
    }
    setCarry(cpu, cpu.ar[0] == 0);
}

/// 0AE05  os dois primeiros objetos 0x101 saem das suas listas (`0x715F`, mantendo `+0xE`) e entram,
/// em par, logo depois do objeto 0x100.
void pairType101(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    cpu.ar[0] = bus.readWord(kHeadCell);
    if (cpu.ar[0] == 0) return;
    cpu.r[1] = 0x101;
    nextOfType(cpu, bus);
    if (cpu.carry()) return;
    cpu.ar[4] = cpu.ar[0];
    nextOfType(cpu, bus);
    if (cpu.carry()) return;
    cpu.ar[5] = cpu.ar[0];
    cpu.r[0] = bus.readWord(cpu.ar[4] + 0xE);
    cpu.r[1] = bus.readWord(cpu.ar[5] + 0xE);
    cpu.ar[2] = cpu.ar[4];
    subs.call(0x715F, cpu, bus);
    cpu.ar[2] = cpu.ar[5];
    subs.call(0x715F, cpu, bus);
    bus.writeWord(cpu.ar[4] + 0xE, cpu.r[0]);
    bus.writeWord(cpu.ar[5] + 0xE, cpu.r[1]);
    cpu.ar[2] = 0x100;
    subs.call(0x70BB, cpu, bus);
    if (!cpu.carry()) return;
    bus.writeWord(cpu.ar[4], cpu.ar[5]);
    cpu.r[0] = bus.readWord(cpu.ar[0]);
    bus.writeWord(cpu.ar[0], cpu.ar[4]);
    bus.writeWord(cpu.ar[5], cpu.r[0]);
}

/// 0AE1F  AR0 = matriz (+7) da peça de tipo 0 (carroceria) do carro AR2.
void bodyMatrix(Tms32031State& cpu, MemoryBus& bus) {
    cpu.r[0] = bus.readWord(cpu.ar[2] + 0x1A);
    for (;;) {
        cpu.ar[0] = cpu.r[0];
        cpu.r[1] = bus.readWord(cpu.ar[0] + 0x12);
        if (cpu.r[1] == 0) break;
        cpu.r[0] = bus.readWord(cpu.ar[0]);
    }
    cpu.ar[0] += 7;
}

} // namespace

void registerRoutines(SubroutineDispatcher& d) {
    d.add(0x72D2, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { sortVisibleFull(c, b); });
    d.add(0x7298, rebuildVisibility);
    d.add(0x6FEB, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { resetObjectPool(c, b); });
    d.add(0x726F, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { promoteNear(c, b); });
    d.add(0x7254, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { demoteFar(c, b); });
    d.add(0x7187, sortObjects);
    d.add(0x71B3, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { detachType100(c, b); });
    d.add(0x71E4, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { regroupType200(c, b); });
    d.add(0x71BF, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { reattachDetached(c, b); });
    d.add(0xAF75, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { attachToNodes(c, b); });
    d.add(0x87B8, gatherType102);
    d.add(0xAE05, pairType101);
    d.add(0xAE27, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { nextOfType(c, b); });
    d.add(0xAE1F, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { bodyMatrix(c, b); });
}

} // namespace rea::core::scene
