/**
 * @file hiscore_scene.cpp
 * @brief Tradução de `0x3738`–`0x3953`, `0x60BD`, `0x9AD2` e `0x9B06`.
 */
#include "core/game/hiscore_scene.hpp"

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }
/// Lê o bit C (carry) de ST.
inline bool carry(const Tms32031State& cpu) { return (cpu.st & Tms32031State::kStCarry) != 0; }

/// Contexto da cena dos recordes: CPU, barramento, sub-rotinas e atalhos.
struct Ctx {
    Tms32031State& cpu;  ///< Registradores da CPU TMS320C31.
    MemoryBus& bus;  ///< Barramento de memória simulado.
    ISubroutines& subs;  ///< Despachante das sub-rotinas traduzidas.
    /// Lê com endereçamento direto (página DP, deslocamento @p off).
    uint32_t rd(uint32_t off) { return bus.readWord(cpu.direct(off)); }
    /// Grava @p v com endereçamento direto (página DP, deslocamento @p off).
    void wr(uint32_t off, uint32_t v) { bus.writeWord(cpu.direct(off), v); }
    /// Lê a palavra no endereço @p a.
    uint32_t at(uint32_t a) { return bus.readWord(a); }
    /// Grava @p v no endereço @p a.
    void put(uint32_t a, uint32_t v) { bus.writeWord(a, v); }
    /// Lê o float TMS em @p a.
    double f(uint32_t a) { return tmsToDouble(bus.readWord(a)); }
    /// Grava o float @p v em @p a.
    void putf(uint32_t a, double v) { bus.writeWord(a, doubleToTms(v)); }
    /// Chama a sub-rotina traduzida no endereço da ROM indicado (o `CALL` original).
    void call(uint32_t a) { subs.call(a, cpu, bus); }
    /// Campo local do PCB atual (AR7).
    uint32_t pcb(uint32_t off) { return bus.readWord(cpu.ar[7] + off); }
    /// Grava @p v na palavra @p off do PCB.
    void setPcb(uint32_t off, uint32_t v) { bus.writeWord(cpu.ar[7] + off, v); }
    /// Dorme @p frames quadros (`0xA874`) e retoma em @p resume.
    void sleep(uint32_t frames, uint32_t resume) {
        cpu.ar[2] = frames;
        cpu.ret = resume;
        call(0xA874);
    }
};

/// @name Variáveis do PCB da cena dos recordes
/// @{
constexpr uint32_t kSceneIdx = 0x2D;  ///< Índice da cena (pista) na tabela `[3953]`.
constexpr uint32_t kTexSlot = 0x1E;   ///< Slot de textura da cena.
constexpr uint32_t kChild = 0x12;     ///< Objeto filho criado pela cena.
constexpr uint32_t kCmosPtr = 0x1C;   ///< Ponteiro do recorde atual na CMOS.
constexpr uint32_t kRows = 0x1D;      ///< Linha do recorde recém-inserido.
constexpr uint32_t kInitials = 0x0F;  ///< 3 palavras: iniciais da linha.
constexpr uint32_t kDigits = 0x20;    ///< Início do buffer dos dígitos do tempo.
/// @}

/// `0x9AD2`: AR2 = endereço CMOS da entrada R7 da tabela R6.
void hiscoreAddr(Ctx& c) {
    c.cpu.ar[2] = tmsMpyi(c.cpu.r[6], 0x50) + tmsMpyi(c.cpu.r[7], 8) + 0x3F8 + c.rd(0x9936);
}

/// `0x9A92`: R0 = linha do recorde novo da pista R1 (palavra 32 bits da CMOS em 0x9AD2(0xF, 0) + 4·R1); −1 = nenhum.
void newRecordRow(Ctx& c) {
    c.cpu.r[6] = 0xF;
    c.cpu.r[7] = 0;
    c.call(0x9AD2);
    c.cpu.r[1] = tmsMpyi(c.cpu.r[1], 4);
    c.cpu.ar[2] += c.cpu.r[1];
    c.call(0x99F9);
}

/// `0x38E5`: paleta R0 (id) em todos os objetos com +0xF == R4.
void paintRow(Ctx& c) {
    c.cpu.ar[2] = c.cpu.r[0];
    c.call(0x9EDB);
    c.cpu.ar[2] = c.cpu.r[4];
    c.call(0x70BB);
    if (!c.cpu.carry()) return;  // BHS: não achou
    do {
        c.put(c.cpu.ar[0] + 0x10, c.cpu.r[0]);
        c.cpu.r[1] = c.cpu.ar[2];
        c.call(0xAE27);
    } while (!c.cpu.carry());
}

/// `0x38D6`: próxima cor da tabela `[38CB]` (fim < 0 → recomeça); 20 % de chance de `[38CC]` (brilho); dorme 6 quadros.
void blinkStep(Ctx& c) {
    c.cpu.r[0] = c.at(c.cpu.ar[6]);
    const bool wrap = static_cast<int32_t>(c.cpu.r[0]) < 0;
    if (wrap) c.cpu.ar[6] = c.rd(0x38CB);          // LDILT ($38CB),AR6
    const uint32_t v = c.at(c.cpu.ar[6]++);         // LDILT *AR6++,R0: o AR6 avança sempre
    if (wrap) c.cpu.r[0] = v;
    const uint32_t r0 = c.cpu.r[0];
    c.cpu.ar[2] = 200;
    c.call(0x8EE9);
    c.cpu.r[0] = r0;
    if (c.cpu.carry()) c.cpu.r[0] = c.rd(0x38CC);  // LDILO
    paintRow(c);
    c.sleep(6, 0x38E1);
}

/// `0x38CD`: processo filho da cena de recordes; pisca a linha do recorde novo da pista R4.
void blinkStart(Ctx& c) {
    c.cpu.r[1] = c.cpu.r[4];
    c.call(0x9A92);
    if (c.cpu.r[0] == 0xFFFFFFFFu) {
        c.sleep(1, 0x38E4);  // 038E2: nenhum recorde novo, dorme para sempre
        return;
    }
    c.cpu.r[0] += 0x10;
    c.cpu.r[4] = tmsLsh(1, c.cpu.r[0]) | 0x200u;  // tipo dos objetos da linha
    c.cpu.ar[6] = c.rd(0x38CB);
    blinkStep(c);
}

/// `0x9B06`: R0 = tempo (32 bits); R1–R4 = bytes das iniciais; AR2 avança 8.
void readHiscore(Ctx& c) {
    c.call(0x99F9);
    const uint32_t r0 = c.cpu.r[0];
    c.call(0x993F);
    c.call(0x9953);
    c.cpu.r[1] = c.at(c.cpu.ar[2]++) >> 24;
    c.cpu.r[2] = c.at(c.cpu.ar[2]++) >> 24;
    c.cpu.r[3] = c.at(c.cpu.ar[2]++) >> 24;
    c.cpu.r[4] = c.at(c.cpu.ar[2]++) >> 24;
    const uint32_t r1 = c.cpu.r[1];
    c.call(0x994B);
    c.call(0x9945);
    c.cpu.r[1] = r1;
    c.cpu.r[0] = r0;
}

/// `0x3788`: tempo R0 em dígitos 3D a partir da âncora `0x10 + AR5`.
void spawnTime(Ctx& c) {
    if (s(c.cpu.r[0]) < 0) c.cpu.r[0] = 0;
    c.cpu.r[1] = c.rd(0xB180);
    if (s(c.cpu.r[0]) > s(c.cpu.r[1])) c.cpu.r[0] = c.cpu.r[1];
    c.call(0x60BD);
    // mmsscc em decimal
    c.cpu.r[1] = tmsMpyi(c.cpu.r[1], 100);
    c.cpu.r[0] += c.cpu.r[1];
    c.cpu.r[2] = tmsMpyi(c.cpu.r[2], 10000);
    c.cpu.r[2] += c.cpu.r[0];
    c.cpu.ar[2] = c.cpu.ar[7] + kDigits;
    c.call(0xA762);
    c.call(0x37C2);
    c.cpu.ar[4] = c.cpu.ar[7] + kDigits;
    c.cpu.r[1] = 0x10 + c.cpu.ar[5];
    c.call(0x36B3);
    // 0379B  âncora recebe o 1º dígito (o menos significativo)
    c.cpu.ir0 = c.at(c.cpu.ar[4]++);
    c.cpu.ar[1] = c.rd(0xB16D);
    c.cpu.r[0] = c.at(c.cpu.ar[1] + c.cpu.ir0);
    c.put(c.cpu.ar[0] + 0x0D, c.cpu.r[0]);
    c.cpu.r[0] = 0x200;
    c.put(c.cpu.ar[0] + 0x0F, c.cpu.r[0]);
    c.cpu.ar[6] = 6;
    for (;;) {
        // 037A2  à esquerda do anterior: dígito (−145) ou separador `[B181]` (−82) nas posições 5 e 2
        c.cpu.ar[1] = c.cpu.ar[0];
        double dx = -145.0;
        if (c.cpu.ar[6] == 5 || c.cpu.ar[6] == 2) {
            c.cpu.ar[2] = c.rd(0xB181);
            c.call(0x700A);
            dx = -82.0;
        } else {
            c.cpu.ir0 = c.at(c.cpu.ar[4]++);
            c.cpu.ar[0] = c.rd(0xB16D);
            c.cpu.ar[2] = c.at(c.cpu.ar[0] + c.cpu.ir0);
            c.call(0x700A);
        }
        c.cpu.r[1] = doubleToTms(dx);
        c.cpu.r[0] = 0x200;
        c.put(c.cpu.ar[0] + 0x0F, c.cpu.r[0]);
        c.putf(c.cpu.ar[0] + 1, c.f(c.cpu.ar[1] + 1) + dx);
        c.put(c.cpu.ar[0] + 2, c.at(c.cpu.ar[1] + 2));
        c.put(c.cpu.ar[0] + 3, c.at(c.cpu.ar[1] + 3));
        c.cpu.r[0] = c.at(c.cpu.ar[1] + 3);
        c.cpu.r[2] = doubleToTms(0.0);
        c.put(c.cpu.ar[0] + 0x14, c.cpu.r[2]);
        c.cpu.ar[2] = c.cpu.ar[0] + 4;
        c.call(0x9377);
        c.cpu.ar[2] = c.cpu.ar[0];
        c.call(0x7093);
        // 037C0  DBU AR6
        const uint32_t res = (c.cpu.ar[6] - 1u) & 0x00FFFFFFu;
        c.cpu.ar[6] = (c.cpu.ar[6] & 0xFF000000u) | res;
        if (res & 0x00800000u) break;
    }
}

/// `0x37E6`: as 3 iniciais (`PCB[F..11]`) como letras 3D à direita da âncora AR0; tipo `(1 << (R1+16)) | 0x200`.
void spawnInitials(Ctx& c) {
    c.cpu.ar[3] = c.cpu.ar[7] + kInitials;
    c.cpu.r[1] += 0x10;
    c.cpu.r[2] = tmsLsh(1, c.cpu.r[1]) | 0x200u;
    c.cpu.ar[1] = c.cpu.ar[0];
    double r4 = 0.0;
    do {
        c.cpu.ar[2] = c.at(c.cpu.ar[3]++) & 0x7Fu;
        if (c.cpu.ar[2] == 0) return;
        c.call(0x3813);
        if (carry(c.cpu)) return;
        c.putf(c.cpu.ar[0] + 1, 120.0 * r4 + -120.0 + c.f(c.cpu.ar[1] + 1));
        c.putf(c.cpu.ar[0] + 2, -12.0 + c.f(c.cpu.ar[1] + 2));
        c.putf(c.cpu.ar[0] + 3, c.f(c.cpu.ar[1] + 3) - 1.0);
        c.put(c.cpu.ar[0] + 0x10, c.pcb(kTexSlot));
        c.put(c.cpu.ar[0] + 0x0F, c.cpu.r[2]);
        const uint32_t r2 = c.cpu.r[2];
        // espaço: gira π em X (a letra fica de costas)
        c.cpu.r[2] = c.at(c.cpu.ar[3] - 1) == 0x20 ? doubleToTms(-3.141602) : doubleToTms(0.0);
        c.put(c.cpu.ar[0] + 0x14, c.cpu.r[2]);
        c.cpu.ar[2] = c.cpu.ar[0] + 4;
        c.call(0x9377);
        c.cpu.r[2] = r2;
        c.cpu.ar[2] = c.cpu.ar[0];
        c.call(0x7093);
        r4 += 1.0;
    } while (r4 <= 2.0);
    c.cpu.r[4] = doubleToTms(r4);
}

/// `0x373C`: monta as 10 linhas da tabela. A linha `PCB[1D]` (recorde recém-inserido; fora de 0..9 na
/// cena de atração) só tem a âncora guardada em `PCB[1B]` e mostra o tempo `[E606]`, sem iniciais.
void spawnRows(Ctx& c) {
    const uint32_t ar5 = c.cpu.ar[5];
    c.cpu.r[6] = c.pcb(kSceneIdx);
    c.cpu.r[7] = 0;
    c.call(0x9AD2);
    c.setPcb(kCmosPtr, c.cpu.ar[2]);
    for (c.cpu.ar[5] = 0;;) {
        c.cpu.ar[2] = c.pcb(kCmosPtr);
        c.call(0x9B06);
        c.setPcb(kCmosPtr, c.cpu.ar[2]);
        if (c.cpu.ar[5] == c.pcb(kRows)) {
            // 03755  linha nova: âncora em PCB[1B], tempo [E606]
            c.cpu.r[1] = c.cpu.ar[5] + 0x20;
            c.call(0x36B3);
            c.setPcb(0x1B, c.cpu.ar[0]);
            c.cpu.r[0] = c.rd(0xE606);
            c.call(0x3788);
        } else {
            c.setPcb(kInitials + 0, c.cpu.r[1]);
            c.setPcb(kInitials + 1, c.cpu.r[2]);
            c.setPcb(kInitials + 2, c.cpu.r[3]);
            c.call(0x3788);
            c.cpu.r[1] = c.cpu.ar[5] + 0x20;
            c.call(0x36B3);
            c.cpu.r[1] = c.cpu.ar[5];
            c.call(0x37E6);
        }
        c.cpu.ar[5] += 1;                       // 03750
        if (s(c.cpu.ar[5]) > 9) break;
    }
    c.cpu.ar[5] = ar5;
}

/// `0x38F9`: afasta as linhas: âncora `0x20 + n` em z += −5000 − 500·n; o resto da linha em z − 1.
void pushRowsBack(Ctx& c) {
    const uint32_t ar5 = c.cpu.ar[5];
    double r0 = -5000.0;
    c.cpu.r[2] = 1u << 16;
    for (c.cpu.ar[5] = 0; s(c.cpu.ar[5]) <= 9; ++c.cpu.ar[5]) {
        c.cpu.r[1] = c.cpu.ar[5] + 0x20;
        c.call(0x36B3);
        const double z = c.f(c.cpu.ar[0] + 3) + r0;
        c.putf(c.cpu.ar[0] + 3, z);
        c.cpu.r[3] = doubleToTms(z - 1.0);
        c.cpu.r[1] = c.cpu.r[2];
        c.cpu.r[2] <<= 1;
        c.call(0x36DB);
        while (!carry(c.cpu)) {
            c.put(c.cpu.ar[0] + 3, c.cpu.r[3]);
            c.call(0x36EF);
        }
        r0 += -500.0;
    }
    c.cpu.r[0] = doubleToTms(r0);
    c.cpu.ar[5] = ar5;
}

/// `0x3913`: aproxima as linhas R0 em z (âncora limitada a 1000). R3 = z − 1 da última linha.
void pullRows(Ctx& c) {
    const uint32_t ar5 = c.cpu.ar[5];
    const double step = tmsToDouble(c.cpu.r[0]);
    c.cpu.r[2] = 1u << 16;
    for (c.cpu.ar[5] = 0; s(c.cpu.ar[5]) <= 9; ++c.cpu.ar[5]) {
        c.cpu.r[1] = c.cpu.ar[5] + 0x20;
        c.call(0x36B3);
        double z = c.f(c.cpu.ar[0] + 3) + step;
        if (z > 1000.0) z = 1000.0;
        c.putf(c.cpu.ar[0] + 3, z);
        c.cpu.r[3] = doubleToTms(z - 1.0);
        c.cpu.r[1] = c.cpu.r[2];
        c.cpu.r[2] <<= 1;
        c.call(0x36DB);
        while (!carry(c.cpu)) {
            c.put(c.cpu.ar[0] + 3, c.cpu.r[3]);
            c.call(0x36EF);
        }
    }
    c.cpu.ar[5] = ar5;
}

/// `0x38F3`: marca com 0x100 os tipos de `[B16C]` e retira esses objetos das listas.
void hideMarked(Ctx& c) {
    c.cpu.ar[3] = c.rd(0xB16C);
    c.cpu.r[2] = 0x100;
    c.call(0x3702);
    c.cpu.r[1] = 0x100;
    c.call(0x372E);
}

/// `0x3931`: título `[[3953] + PCB[2D]]` em (−4700 centro, R3, −301).
void spawnTitleAt(Ctx& c) {
    c.cpu.ar[2] = c.at(c.rd(0x3953) + c.pcb(kSceneIdx));
    c.cpu.r[2] = doubleToTms(-4700.0);
    c.cpu.r[4] = doubleToTms(-301.0);
    c.cpu.r[6] = 0;
    c.call(0x3824);
}

/// `0x392E`: título na altura −910 (cena de atração).
void spawnTitle(Ctx& c) {
    c.cpu.r[3] = doubleToTms(-910.0);
    spawnTitleAt(c);
}

/// `0x3930`: título na altura −2910 (tabela após a entrada de iniciais, `0x31A6`).
void spawnTitleHigh(Ctx& c) {
    c.cpu.r[3] = doubleToTms(-2910.0);
    spawnTitleAt(c);
}

// --- processo 0x3889 ---------------------------------------------------------------
/// Trecho traduzido da ROM em `0x3889`.
void sceneStart(Ctx& c) {
    c.setPcb(kSceneIdx, c.cpu.ar[6]);
    c.cpu.ar[2] = c.rd(0xB189);
    c.call(0x9EDB);
    c.setPcb(kTexSlot, c.cpu.r[0]);
    c.cpu.r[0] = 0;
    c.wr(0xC940, c.cpu.r[0]);
    c.bus.writeWord(0x809800, doubleToTms(-4700.0));
    c.bus.writeWord(0x809801, doubleToTms(650.0));
    c.bus.writeWord(0x809802, doubleToTms(-4200.0));
    c.cpu.r[2] = doubleToTms(0.0);
    c.bus.writeWord(0x809807, c.cpu.r[2]);
    c.cpu.dp = 0;
    c.cpu.ar[2] = c.rd(0x0047);
    c.call(0x938A);
    c.cpu.ar[2] = c.rd(0xB18A);
    c.call(0x440C);
    c.call(0x38F3);
    c.call(0x3738);
    c.call(0x38F9);
    c.cpu.r[2] = doubleToTms(120.0);
    c.cpu.r[1] = 0x200;
    c.call(0x371E);
    c.cpu.r[1] = 9;
    c.call(0x36B3);
    c.cpu.ar[2] = c.cpu.ar[0];
    c.call(0x715F);
    c.call(0x392E);
    c.call(0x7298);
    // 038A9  filho [B18B] (ID 0x561) logo depois deste processo
    c.cpu.r[4] = c.pcb(kSceneIdx);
    c.cpu.ar[2] = c.rd(0xB18B);
    c.cpu.r[2] = 0x561;
    c.call(0xA868);
    c.setPcb(kChild, c.cpu.ar[0]);
    c.sleep(1, 0x38B0);
}

/// Trecho traduzido da ROM em `0x38B0`.
void scenePull(Ctx& c) {
    c.cpu.r[0] = doubleToTms(250.0);
    c.call(0x3913);
    c.cpu.r[1] = doubleToTms(998.0);
    if (tmsToDouble(c.cpu.r[3]) < 998.0) {
        c.sleep(1, 0x38B0);
        return;
    }
    c.sleep(0x3C, 0x38B7);
}

/// Trecho traduzido da ROM em `0x38B7`.
void sceneCamStart(Ctx& c) {
    c.cpu.ar[6] = 0x27;
    c.sleep(1, 0x38BA);
}

/// Trecho traduzido da ROM em `0x38BA`.
void sceneCam(Ctx& c) {
    c.cpu.r[1] = doubleToTms(-21.0);
    c.bus.writeWord(0x809801, doubleToTms(tmsToDouble(c.bus.readWord(0x809801)) - 21.0));
    c.cpu.r[1] = doubleToTms(56.0);
    c.cpu.r[0] = doubleToTms(tmsToDouble(c.bus.readWord(0x809802)) + 56.0);
    c.bus.writeWord(0x809802, c.cpu.r[0]);
    c.cpu.dp = 0;
    const uint32_t res = (c.cpu.ar[6] - 1u) & 0x00FFFFFFu;
    c.cpu.ar[6] = (c.cpu.ar[6] & 0xFF000000u) | res;
    if (!(res & 0x00800000u)) {
        c.sleep(1, 0x38BA);
        return;
    }
    c.sleep(0x3C, 0x38C8);
}

/// Trecho traduzido da ROM em `0x38C8`.
void sceneEnd(Ctx& c) {
    c.cpu.ar[2] = c.pcb(kChild);
    c.call(0xA8AC);
    c.call(0xA896);  // BR $A896
}

} // namespace

void splitTime(Tms32031State& cpu, MemoryBus& bus) {
    auto f = [&](uint32_t a) { return tmsToDouble(bus.readWord(cpu.direct(a))); };
    auto clamp = [](uint32_t v, int32_t hi) { return s(v) > hi ? static_cast<uint32_t>(hi) : (s(v) < 0 ? 0u : v); };
    const uint32_t r3 = cpu.r[3];
    cpu.r[2] = clamp(tmsFix(static_cast<double>(s(cpu.r[0])) * f(0x60BA)), 0x63);
    cpu.r[0] -= tmsMpyi(cpu.r[2], 0x0CE4);
    if (s(cpu.r[0]) < 0) cpu.r[0] = 0;
    cpu.r[1] = clamp(tmsFix(static_cast<double>(s(cpu.r[0])) * f(0x60BB)), 0x3B);
    cpu.r[0] -= tmsMpyi(cpu.r[1], 0x37);
    if (s(cpu.r[0]) < 0) cpu.r[0] = 0;
    cpu.r[0] = clamp(tmsFix(static_cast<double>(s(cpu.r[0])) * f(0x60BC)), 0x63);
    cpu.r[3] = r3;
}

void textToDigits(Tms32031State& cpu, MemoryBus& bus) {
    std::vector<uint32_t> stack;
    cpu.r[1] = 0;
    cpu.r[2] = 0;
    cpu.ar[1] = cpu.ar[2];
    do {
        if (cpu.r[2] == 0) cpu.r[0] = bus.readWord(cpu.ar[2]++);
        cpu.r[3] = tmsLsh(cpu.r[0], cpu.r[2]);
        cpu.r[2] -= 8;
        if (s(cpu.r[2]) < -24) cpu.r[2] = 0;
        cpu.r[3] &= 0xFFu;
        if (cpu.r[3] == 0x3E) continue;  // '>' (sinal de menos) é pulado
        if (cpu.r[3] == 0) break;
        cpu.r[3] -= 0x30;
        stack.push_back(cpu.r[3]);
        cpu.r[1] += 1;
    } while (s(cpu.r[1]) <= 6);
    // 037D7  desempilha (ordem inversa) e completa com zeros
    cpu.r[1] -= 1;
    for (int32_t i = 0; i <= s(cpu.r[1]) && !stack.empty(); ++i) {
        cpu.r[0] = stack.back();
        stack.pop_back();
        bus.writeWord(cpu.ar[1]++, cpu.r[0]);
    }
    cpu.rc = 0xFFFFFFFFu;
    if (cpu.r[1] == 6) return;
    cpu.r[0] = 0;
    cpu.r[1] = 6 - cpu.r[1];
    if (s(cpu.r[1]) < 0) return;
    for (int32_t i = 0; i <= s(cpu.r[1]); ++i) bus.writeWord(cpu.ar[1]++, cpu.r[0]);
}

void allocCharObject(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    uint32_t a = cpu.ar[2];
    if (s(a) < 0x30) a = 0x30;
    if (s(a) <= 0x39) {
        a += 0x2C;  // dígitos depois das 26 letras e do 27º símbolo
    } else if (s(a) > 0x5A) {
        a = 0x5A;
    }
    a -= 0x41;
    if (s(a) < 0) a = 0;
    if (s(a) > 0x25) a = 0x25;
    cpu.ar[2] = bus.readWord(a + bus.readWord(cpu.direct(0xB168)));
    subs.call(0x700A, cpu, bus);
}

void spawnText(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
    std::vector<uint32_t> stack;
    cpu.ar[1] = cpu.ar[2];
    cpu.r[1] = 0;
    stack.push_back(cpu.r[1]);
    const double r5 = 40.0;
    cpu.r[5] = doubleToTms(r5);
    double x = tmsToDouble(cpu.r[2]);
    do {
        cpu.r[0] = tmsLsh(bus.readWord(cpu.ar[1]), cpu.r[1]);
        cpu.r[1] -= 8;
        if (s(cpu.r[1]) < -24) {
            cpu.r[1] = 0;
            ++cpu.ar[1];
        }
        cpu.r[0] &= 0x7Fu;
        stack.push_back(cpu.r[0]);
        cpu.ir0 += 1;
        x += r5;
    } while (cpu.r[0] != 0);
    x -= r5;
    x -= r5;
    stack.pop_back();  // o terminador
    for (;;) {
        cpu.ar[0] = stack.back();
        stack.pop_back();
        if (cpu.ar[0] == 0) break;
        if (cpu.ar[0] != 0x20) {
            if (cpu.ar[0] == 0x30) cpu.ar[0] = 0x4F;  // '0' → 'O'
            if (cpu.ar[0] == 0x31) cpu.ar[0] = 0x49;  // '1' → 'I'
            cpu.ar[0] -= 0x41;
            if (s(cpu.ar[0]) < 0) cpu.ar[0] = 0;
            if (s(cpu.ar[0]) > 0x1A) cpu.ar[0] = 0x1A;
            cpu.ar[0] += bus.readWord(cpu.direct(0xB169));
            cpu.ar[2] = bus.readWord(cpu.ar[0]);
            subs.call(0x700A, cpu, bus);
            cpu.r[2] = doubleToTms(x);
            bus.writeWord(cpu.ar[0] + 1, cpu.r[2]);
            bus.writeWord(cpu.ar[0] + 2, cpu.r[3]);
            bus.writeWord(cpu.ar[0] + 3, cpu.r[4]);
            bus.writeWord(cpu.ar[0] + 0x0F, cpu.r[6]);
            cpu.r[2] = doubleToTms(0.0);
            cpu.ar[2] = cpu.ar[0] + 4;
            subs.call(0x938A, cpu, bus);
            cpu.ar[2] = cpu.ar[0];
            subs.call(0x7093, cpu, bus);
        }
        // 03855  x −= 80
        cpu.r[0] = doubleToTms(80.0);
        x -= 80.0;
    }
    cpu.r[2] = doubleToTms(x);
}

void registerHiscoreScene(SubroutineDispatcher& d) {
    auto wrap = [](void (*fn)(Ctx&)) {
        return [fn](Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs) {
            Ctx c{cpu, bus, subs};
            fn(c);
        };
    };
    d.add(0x3889, wrap(sceneStart));
    d.add(0x38B0, wrap(scenePull));
    d.add(0x38AE, wrap([](Ctx& c) { c.sleep(1, 0x38B0); }));
    d.add(0x38B7, wrap(sceneCamStart));
    d.add(0x38BA, wrap(sceneCam));
    d.add(0x38C8, wrap(sceneEnd));
    d.add(0x3738, wrap([](Ctx& c) {
        c.cpu.r[0] = 0x0F;
        c.setPcb(kRows, c.cpu.r[0]);
        c.call(0x373C);
    }));
    d.add(0x373C, wrap(spawnRows));
    d.add(0x3788, wrap(spawnTime));
    d.add(0x37E6, wrap(spawnInitials));
    d.add(0x38F3, wrap(hideMarked));
    d.add(0x38F9, wrap(pushRowsBack));
    d.add(0x3913, wrap(pullRows));
    d.add(0x392E, wrap(spawnTitle));
    d.add(0x3930, wrap(spawnTitleHigh));
    d.add(0x9AD2, wrap(hiscoreAddr));
    d.add(0x9A92, wrap(newRecordRow));
    d.add(0x38CD, wrap(blinkStart));
    d.add(0x38E1, wrap(blinkStep));                                  // BR $38D6
    d.add(0x38E4, wrap([](Ctx& c) { c.sleep(1, 0x38E4); }));         // BR $38E2
    d.add(0x38E5, wrap(paintRow));
    d.add(0x9B06, wrap(readHiscore));
    d.add(0x60BD, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { splitTime(c, b); });
    d.add(0x37C2, [](Tms32031State& c, MemoryBus& b, ISubroutines&) { textToDigits(c, b); });
    d.add(0x3813, allocCharObject);
    d.add(0x3824, spawnText);
}

} // namespace rea::core::game
