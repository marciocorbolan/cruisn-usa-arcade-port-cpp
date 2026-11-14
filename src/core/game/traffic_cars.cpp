/**
 * @file traffic_cars.cpp
 * @brief Tradução de `0x668C`–`0x6753`, `0x67B0`–`0x67E2`, `0x68F4`–`0x691A`, `0x7B25`–`0x7B31`,
 *        `0x9761`–`0x9935` (inclui os tipos de link 0x10/0x11 e a retomada `0x9903`), `0xA3FF`–`0xA508` e `0xA718`–`0xA72F`.
 */
#include "core/game/traffic_cars.hpp"

#include <cmath>

#include "core/tms_float.hpp"

namespace rea::core::game {

namespace {

constexpr uint32_t kFocusCar  = 0xE876;  ///< objeto do carro focado (jogador/câmera)
constexpr uint32_t kFocusData = 0xE878;  ///< dados D do carro focado
constexpr uint32_t kGridLock  = 0xE5EC;  ///< 0x33BB = carros presos no grid; 0x5577 = largada.
constexpr uint32_t kLink      = 0xC8E3;  ///< 1 = corrida com link em andamento (tipo 5); 0 nos tipos 6 e 8.
constexpr uint32_t kModels    = 0x961B; ///< `[961B]`: tabela de modelos (11 palavras cada).
constexpr uint32_t kMatA      = 0x005A; ///< `[0x5A]`: matriz de rascunho.
constexpr uint32_t kVecPtr    = 0x005D; ///< `[0x5D]`: vetor de rascunho.
constexpr uint32_t kLaneVec   = 0x809855; ///< `[0x5D]` lido por DP = 0x80 (`($9855)`..`($9857)`).
constexpr uint32_t kFirstNode = 0xE46E;  ///< Lista de trechos (`+0x1E` índice, `+0x21` próximo).
constexpr uint32_t kLastNode  = 0xE470;  ///< Último nó de pista carregado (fim da janela `[E46E]`..`[E470]`).

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
int32_t si(uint32_t v) { return static_cast<int32_t>(v); }
/// Máscara do bit C de ST para @p on.
uint32_t carryBit(bool on) { return on ? Tms32031State::kStCarry : 0u; }

/// `LDI n,Rx` num registrador que guarda um float: expoente antigo + bits 31..8 do inteiro.
uint32_t ldiIntoFloat(uint32_t old, uint32_t n) { return (old & 0xFF000000u) | ((n >> 8) & 0x00FFFFFFu); }

/// Contexto dos processos de tráfego: CPU, barramento, sub-rotinas e atalhos de campo.
struct Ctx {
    Tms32031State& c;  ///< Registradores da CPU TMS320C31.
    MemoryBus& b;  ///< Barramento de memória simulado.
    ISubroutines& s;  ///< Despachante das sub-rotinas traduzidas.
    /// Lê o float TMS em @p a e o converte para double.
    double f(uint32_t a) const { return tmsToDouble(b.readWord(a)); }
    /// Converte @p v para float TMS e grava em @p a.
    void wf(uint32_t a, double v) const { b.writeWord(a, doubleToTms(v)); }
    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) const { return b.readWord(a); }
    /// Grava a palavra @p v no endereço @p a.
    void wr(uint32_t a, uint32_t v) const { b.writeWord(a, v); }
    /// Float curto de 16 bits (imediato da instrução) na palavra @p pc da ROM.
    double K(uint32_t pc) const { return shortFloat(static_cast<uint16_t>(b.readWord(pc) & 0xFFFFu)); }
    /// Lê o registrador R@p i como float TMS.
    double R(int i) const { return tmsToDouble(c.r[i]); }
    /// Grava @p v, convertido para float TMS, no registrador R@p i.
    void setR(int i, double v) const { c.r[i] = doubleToTms(v); }
    /// Chama a sub-rotina traduzida no endereço da ROM indicado (o `CALL` original).
    void call(uint32_t a) const { s.call(a, c, b); }
    /// Endereço da variável @p o do processo (AR7).
    uint32_t P(uint32_t o) const { return c.ar[7] + o; }
    /// Endereço do campo @p o dos dados do carro (AR5).
    uint32_t D(uint32_t o) const { return c.ar[5] + o; }
    /// Endereço do campo @p o do objeto (AR4).
    uint32_t O(uint32_t o) const { return c.ar[4] + o; }
    /// Aleatório em [0, @p n) (`0x8EDB`).
    uint32_t rnd(uint32_t n) const {
        c.ar[2] = n;
        call(0x8EDB);
        return c.r[0];
    }
    /// Aleatório float em [0, @p range) (`0x8EC2`) ou ±@p range com @p signedRange (`0x8ED1`).
    double rndF(double range, bool signedRange) const {
        setR(0, range);
        call(signedRange ? 0x8ED1 : 0x8EC2);
        return R(0);
    }
    /// `0x8EE9`: true com chance @p n em 1000.
    bool chance(uint32_t n) const {
        c.ar[2] = n;
        call(0x8EE9);
        return c.carry();
    }
    /// Liga ou desliga o bit C de ST.
    void setCarry(bool on) const { c.st = (c.st & ~Tms32031State::kStCarry) | carryBit(on); }
    /// Dorme @p frames quadros (`0xA874`) e retoma em @p pc.
    void sleep(uint32_t frames, uint32_t pc) const {
        c.ar[2] = frames;
        c.ret = pc;
        call(0xA874);
    }
    /// Cria o processo `[ptr]` com o ID @p id (`0xA868`).
    void create(uint32_t ptr, uint32_t id) const {
        c.ar[2] = rd(ptr);
        c.r[2] = id;
        call(0xA868);
    }
};

// --- Navegação --------------------------------------------------------------------------

/// 066BC  R0 = distância xz de AR4 ao focado. Preserva R1, R2 e AR3.
void distToFocus(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t r1 = c.r[1], r2 = c.r[2], ar3 = c.ar[3];
    const uint32_t f = x.rd(kFocusCar);
    const double dx = x.f(x.O(1)) - x.f(f + 1), dz = x.f(x.O(3)) - x.f(f + 3);
    x.setR(2, dz * dz + dx * dx);
    x.call(0xA5F1);
    c.r[1] = r1; c.r[2] = r2; c.ar[3] = ar3;
}

/// 06708 / 0670F / 06739 / 06740  R2 = rumo do nó AR2 ao nó `+0x17`/`+0x21`; `[0x5D]` = rotY(rumo)·(d, 0, 0)
/// com d = faixa `[AR7+0xC]` da tabela `[6594]` (escolhida por `0x66A7`) ou `[AR7+0x1B]`.
/// Preserva AR0, R0, R3 e AR2.
void laneOffset(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t link, bool fromTable) {
    Ctx x{c, b, s};
    const uint32_t ar0 = c.ar[0], r0 = c.r[0], r3 = c.r[3], ar2 = c.ar[2];
    c.ar[0] = x.rd(ar2 + link);
    x.setR(2, x.f(c.ar[0] + 1) - x.f(ar2 + 1));
    x.setR(3, x.f(c.ar[0] + 3) - x.f(ar2 + 3));
    x.call(0x931A);
    const uint32_t heading = doubleToTms(x.R(0) - x.K(0x671B));
    c.r[2] = heading;
    double d;
    if (fromTable) {
        x.call(0x66A7); // R0 = 1 se o trecho está bloqueado
        const uint32_t blocked = c.r[0];
        c.ar[2] = x.rd(kMatA);
        c.r[2] = heading;
        x.call(0x938A);
        c.ar[0] = x.rd(x.P(0xC)) & 3u;
        c.ar[1] = x.rd(0x6594) + blocked;
        c.ar[0] += x.rd(c.ar[1]);
        d = x.f(c.ar[0]);
    } else {
        c.ar[2] = x.rd(kMatA);
        c.r[2] = heading;
        x.call(0x938A);
        d = x.f(x.P(0x1B));
    }
    // 06729
    c.ar[2] = x.rd(kVecPtr);
    x.wf(c.ar[2], d);
    x.wf(c.ar[2] + 1, 0.0);
    x.wf(c.ar[2] + 2, 0.0);
    c.r[3] = c.ar[2];
    c.r[2] = x.rd(kMatA);
    x.call(0x941E);
    c.ar[2] = ar2;
    c.r[2] = heading;
    c.r[3] = r3; c.r[0] = r0; c.ar[0] = ar0;
}

/// 066DB…066FA  `laneOffset` no nó `[AR7+0x10]` e R0 = distância de AR4 a esse nó. Preserva R1, R2, AR2.
void distToTarget(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t link, bool fromTable) {
    Ctx x{c, b, s};
    const uint32_t r1 = c.r[1], r2 = c.r[2], ar2 = c.ar[2];
    c.ar[2] = x.rd(x.P(0x10));
    laneOffset(c, b, s, link, fromTable);
    const double dx = x.f(c.ar[2] + 1) - x.f(x.O(1)), dz = x.f(c.ar[2] + 3) - x.f(x.O(3));
    x.setR(2, dx * dx + dz * dz);
    x.call(0xA5F1);
    c.ar[2] = ar2; c.r[2] = r2; c.r[1] = r1;
}

/// 067B0  carry = focado a ≤ 400 da reta de movimento do carro AR4/AR5 (e a ≤ 15000 dele).
void focusOnPath(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    distToFocus(c, b, s);
    if (x.R(0) > 15000.0) return x.setCarry(false);
    c.ar[3] = x.rd(kVecPtr);
    // MPYF ($C92F): o contador inteiro é lido como float (≈ 1,0), como no original
    x.wf(c.ar[3] + 2, x.f(x.D(0x26)) * x.f(0xC92F) * x.K(0x67B7));
    x.wf(c.ar[3], 0.0);
    x.wf(c.ar[3] + 1, 0.0);
    c.ar[2] = x.rd(kMatA);
    c.r[2] = x.rd(x.D(0x2D));
    x.call(0x938A);
    c.r[2] = c.ar[2];
    c.ar[2] = c.ar[3];
    c.r[3] = c.ar[3];
    x.call(0x941E);
    const double r6 = -x.f(kLaneVec + 2), r7 = x.f(kLaneVec);
    const double r5 = -(x.f(x.O(1)) * r6 + x.f(x.O(3)) * r7);
    x.setR(2, r6 * r6 + r7 * r7);
    x.call(0xA5F1);
    const double len = x.R(0);
    const uint32_t f = x.rd(kFocusCar);
    const double r2 = x.f(f + 1) * r6 + x.f(f + 3) * r7 + r5;
    x.setR(0, r2);
    x.setR(1, len);
    x.call(0xA509);
    x.setR(2, r2);
    x.setR(5, r5);
    x.setR(6, r6);
    x.setR(7, r7);
    x.setCarry(!(std::fabs(x.R(0)) > 400.0));
}

/// 068F4  carry = carro AR4/AR5 à frente do focado. Preserva AR0, AR1 e R0.
void aheadOfFocus(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t ar0 = c.ar[0], ar1 = c.ar[1], r0 = c.r[0];
    uint32_t node = x.rd(x.rd(kFocusData) + 0x37);
    const int32_t mine = si(x.rd(node + 0x1E)), car = si(x.rd(x.rd(x.D(0x37)) + 0x1E));
    bool ahead;
    if (mine != car) {
        ahead = mine < car;
    } else {
        node = x.rd(x.rd(node + 0x21) + 0x21);
        const double cx = x.f(node + 1) - x.f(x.O(1)), cz = x.f(node + 3) - x.f(x.O(3));
        const uint32_t f = x.rd(kFocusCar);
        const double fx = x.f(node + 1) - x.f(f + 1), fz = x.f(node + 3) - x.f(f + 3);
        const double dc = cx * cx + cz * cz, df = fz * fz + fx * fx;
        x.setR(1, df);
        x.setR(2, fx * fx);
        ahead = dc < df;
    }
    c.ar[0] = ar0; c.ar[1] = ar1; c.r[0] = r0;
    x.setCarry(ahead);
}

/// 09891  R0 = 1 se o índice `[AR7+0x13]` está dentro da janela de nós carregados.
void inWindow(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    c.ar[0] = x.rd(kFirstNode);
    c.r[0] = 0;
    if (x.rd(x.P(0x10)) == c.ar[0]) return;
    const int32_t idx = si(x.rd(x.P(0x13)));
    c.r[1] = static_cast<uint32_t>(idx);
    if (idx <= si(x.rd(c.ar[0] + 0x1E))) return;
    c.ar[0] = x.rd(kLastNode);
    c.r[0] = idx < si(x.rd(c.ar[0] + 0x1E)) ? 1u : 0u;
}

/// 0668C  textura aleatória da lista `[modelo+9]`. Preserva R0 e AR2.
void randomSkin(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t r0 = c.r[0], ar2 = c.ar[2];
    uint32_t list = x.rd(x.rd(x.P(0x19)) * 0xB + x.rd(kModels) + 9);
    const uint32_t n = x.rd(list++);
    if (n != 0) {
        const uint32_t k = x.rnd(n + 1);
        if (k != 0) {
            c.ar[2] = x.rd(list + k - 1);
            x.call(0x9EDB);
            if (c.carry()) {
                x.wr(x.O(0x10), c.r[0]);
                x.wr(x.O(0xE), x.rd(x.O(0xE)) | 0x400u);
            }
        }
    }
    c.r[0] = r0; c.ar[2] = ar2;
}

/// 07B25  com link, envia (0xF, R0, R5, `[AR4+0x10]` >> 8) ao outro gabinete.
void linkAnnounce(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    c.r[1] = x.rd(kLink);
    if (c.r[1] == 0) return;
    c.ar[2] = x.rd(0x7939);
    x.wr(c.ar[2], 0xF);
    x.wr(c.ar[2] + 1, c.r[0]);
    x.wr(c.ar[2] + 2, c.r[5]);
    x.wr(c.ar[2] + 3, x.rd(x.O(0x10)) >> 8);
    c.rc = 3;
    c.ar[2] = x.rd(0x7939);
    x.call(0x7998);
}

/// 09924  com link, envia (0x10, R0).
void linkRemove(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    c.ar[2] = x.rd(0x7939);
    x.wr(c.ar[2], 0x10);
    x.wr(c.ar[2] + 1, c.r[0]);
    c.rc = 1;
    x.call(0x7998);
}

// --- Carro parado (link) ----------------------------------------------------------------

/// Laço do carro parado durante o link: segue a pista sem dirigir.
void parkedLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    c.r[3] = x.rd(x.O(0x1B));
    const uint32_t ar4 = c.ar[4], ar5 = c.ar[5];
    x.call(0x2041);
    c.ar[5] = ar5; c.ar[4] = ar4;
    x.wf(x.D(0x2E), 0.0);
    x.wf(x.D(0x32), 0.0);
    x.wf(x.D(0x25), 0.0);
    c.r[0] = x.rd(x.D(0x2C));
    x.wr(x.O(0x15), c.r[0]);
    c.r[0] = ldiIntoFloat(c.r[0], 0); // 0A726 LDI 0,R0; STF R0: expoente do rumo, mantissa 0
    x.wr(x.D(0x31), c.r[0]);
    c.r[0] = 0;
    x.wr(x.D(0x5A), 0);
    x.sleep(1, 0xA72C);
}

/// 0A718  `[D+0x58]` = 1 e fica parado.
void parked(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    b.writeWord(c.ar[5] + 0x58, 1);
    parkedLoop(c, b, s);
}

// --- Remoção e destroços ----------------------------------------------------------------

void removeCarQuiet(Tms32031State& c, MemoryBus& b, ISubroutines& s);
void sameLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s);
void wreckSameLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s);

/// 098A1  fim do carro: avisa o link, tira das listas, libera dados/peças/objeto e morre.
void removeCar(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    if (x.rd(kLink) != 0) {
        x.call(0x5268);
        if (si(c.r[1]) - si(c.r[0]) > 0) {
            // 098E4  pacote 0x11 e fica parado
            c.ar[2] = x.rd(0x7939);
            x.wr(c.ar[2], 0x11);
            x.wr(c.ar[2] + 1, x.rd(x.D(0x59)));
            x.wr(c.ar[2] + 2, x.rd(x.O(0xF)) & 0x20u);
            c.rc = 2;
            x.call(0x7998);
            c.r[0] = 1;
            x.wr(x.D(0x58), 1);
            return parkedLoop(c, b, s);
        }
        c.r[0] = x.rd(x.D(0x59));
        linkRemove(c, b, s);
    }
    removeCarQuiet(c, b, s);
}

/// 098A9  tira das listas, libera dados/peças/objeto e morre (também a retomada pedida pelo tipo 0x10).
void removeCarQuiet(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    x.call(0x676B);
    c.ar[2] = c.ar[5];
    x.call(0x9032);
    x.wr(x.O(0xE), x.rd(x.O(0xE)) & ~(1u << 23));
    if (x.rd(x.O(0xE)) & 0x800u) {
        c.ar[2] = x.rd(x.O(0x16));
        x.call(0xA8AC);
    }
    c.ar[2] = c.ar[4];
    x.call(0x7100);
    x.call(0xA896);
}

/// 098C6  destroço do 0x213: fumaça por 10 quadros, parado até sair da janela.
void wreckSameLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    if (x.rd(kGridLock) != 0x33BB) {
        c.ar[6] -= 1;
        if (si(c.ar[6]) >= 0) x.create(0xC627, 0x800);
        x.wr(x.P(0x10), x.rd(x.D(0x37)));
        c.ar[1] = x.rd(x.D(0x37));
        c.ar[0] = x.rd(kFirstNode);
        if (c.ar[1] == c.ar[0] || si(x.rd(c.ar[1] + 0x1E)) < si(x.rd(c.ar[0] + 0x1E))) return removeCar(c, b, s);
        x.wf(x.P(0xF), 0.0);
        x.wf(x.D(0x24), 0.0);
        x.wf(x.P(0xD), 0.0);
        x.call(0x2C18);
        x.call(0x2EFD);
    }
    x.sleep(1, 0x98E1);
}

/// 09903  retomada pedida pelo tipo 0x11 (`0x98F2`): o carro volta a este gabinete. R4 = flags do outro
/// (`+F` & 0x20 em bits 16–23, via PCB+5): 0 → laço normal (`0x97D3`), senão destroço (`0x98C6`).
void handBack(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    x.wr(x.D(0x58), 0);
    c.r[2] = x.rd(x.D(0x57));
    x.wr(x.P(0x13), c.r[2]);
    // fora da janela [[E46E]+1E]..[[E470]+1E]: 098A7 (avisa com o tipo 0x10 e remove)
    if (si(c.r[2]) > si(x.rd(x.rd(kLastNode) + 0x1E)) || si(c.r[2]) < si(x.rd(x.rd(kFirstNode) + 0x1E))) {
        c.r[0] = x.rd(x.D(0x59));
        linkRemove(c, b, s);
        return removeCarQuiet(c, b, s);
    }
    x.call(0x5360);
    x.wr(x.P(0x10), c.ar[2]);
    x.wf(x.D(0x2E), 0.0);
    x.wf(x.D(0x32), 0.0);
    x.wf(x.D(0x25), 0.0);
    x.wr(x.D(0x31), 0);
    if (c.r[4] == 0) return sameLoop(c, b, s);
    // 09919
    c.ar[6] = 0;
    c.r[0] = (x.rd(x.O(0xF)) & ~0xF0u) | 0x20u;
    x.wr(x.O(0xF), c.r[0]);
    x.wr(x.D(0x40), c.r[0]);
    x.wr(x.P(3), c.r[0]);
    x.call(0x2EFD);
    x.wf(x.P(0xD), 0.0);
    wreckSameLoop(c, b, s);
}

/// 0992B  tipo 0x10 recebido: o carro (procurado por `0x7B34`) retoma em `[9935]` = 0x98A9.
void receiveRemove(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    if (x.rd(0xE593) != 0) {
        c.ar[2] += 1;
        return;
    }
    x.call(0x7B34);
    if (!c.zero()) return;
    c.ar[7] = x.rd(c.ar[0] + 0x17);
    c.r[2] = x.rd(0x9935);
    x.wr(x.P(2), c.r[2]);
}

/// 098F2  tipo 0x11 recebido: o carro retoma em `[9902]` = 0x9903 com PCB+5 (R4) = a palavra de flags.
void receiveHandBack(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    if (x.rd(0xE593) != 0) {
        c.ar[2] += 2;
        return;
    }
    x.call(0x7B34);
    if (!c.zero()) {
        c.ar[2] += 1;
        return;
    }
    c.ar[4] = c.ar[0];
    c.ar[5] = x.rd(x.O(0x1B));
    c.ar[7] = x.rd(x.O(0x17));
    c.r[2] = x.rd(0x9902);
    x.wr(x.P(2), c.r[2]);
    c.r[0] = x.rd(c.ar[2]++);  // palavra inteira (byte em bits 16–23), como no original
    x.wr(x.P(5), c.r[0]);
}

/// 098B9  bateu: tipo vira 0x2x, cria os efeitos [C625]/[C626].
void wreckSame(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    const uint32_t t = (x.rd(x.O(0xF)) & ~0xF0u) | 0x20u;
    x.wr(x.O(0xF), t);
    x.wr(x.D(0x40), t);
    x.wr(x.P(3), t);
    x.create(0xC625, 0x800);
    x.create(0xC626, 0x800);
    c.ar[6] = 0xA;
    wreckSameLoop(c, b, s);
}

/// 0A4F2  destroço do 0x214.
void wreckOncomingLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    if (x.rd(kGridLock) != 0x33BB) {
        c.ar[6] -= 1;
        if (si(c.ar[6]) >= 0) x.create(0xC6B8, 2);
        c.ar[0] = x.rd(x.D(0x37));
        if (si(x.rd(c.ar[0] + 0x1E) >> 8) < si(x.rd(0xE40C))) return removeCar(c, b, s);
        x.wf(x.D(0x24), 0.0);
        x.call(0x2C18);
        x.call(0x2EFD);
    }
    x.sleep(1, 0xA507);
}

// --- Criação comum ----------------------------------------------------------------------

/// Monta o carro do modelo AR2 com o tipo @p type; nasce no penúltimo nó com o deslocamento de
/// `laneOffset` (`link`, tabela de faixas). Devolve false se o objeto não foi criado (morreu).
bool spawn(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t type, bool typeBeforeSetup,
           uint32_t link) {
    Ctx x{c, b, s};
    x.wr(x.P(0x19), c.ar[2]);
    c.ar[2] = x.rd(x.rd(kModels) + c.ar[2] * 0xB);
    x.wr(x.P(0xE), c.r[4]);
    x.call(0x700A);
    if (c.carry()) {
        x.call(0xA896);
        return false;
    }
    c.ar[4] = c.ar[0];
    if (typeBeforeSetup) x.wr(x.O(0xF), type);
    c.ar[2] = x.rd(x.P(0x19));
    x.call(0x8F3E);
    x.call(0xAE3A);
    if (typeBeforeSetup) {
        x.wf(x.D(0x23), x.K(0x9785));
        x.wr(x.D(0x40), x.rd(x.O(0xF)));
        x.wr(x.P(3), x.rd(x.O(0xF)));
    } else {
        x.wr(x.D(0x40), type);
        x.wr(x.O(0xF), type);
        x.wr(x.P(3), type);
    }
    randomSkin(c, b, s);
    x.wr(x.P(0xC), x.rnd(2) + 2);
    c.ar[2] = x.rd(x.rd(x.rd(kLastNode) + 0x17) + 0x17);
    x.wr(x.P(0x10), c.ar[2]);
    x.wr(x.P(0x13), x.rd(c.ar[2] + 0x1E));
    laneOffset(c, b, s, link, true);
    x.wf(x.O(1), x.f(c.ar[2] + 1) + x.f(kLaneVec));
    x.wf(x.O(2), x.f(c.ar[2] + 2) - x.f(x.D(0x48)) + x.f(kLaneVec + 1));
    x.wf(x.O(3), x.f(c.ar[2] + 3) + x.f(kLaneVec + 2));
    x.wr(x.O(0x15), c.r[2]);
    x.wr(x.P(0xD), c.r[2]);
    x.wr(x.D(0x2C), c.r[2]);
    x.wr(x.D(0x2D), c.r[2]);
    c.ar[2] = c.ar[4] + 4;
    x.call(0x938A);
    c.r[0] = 0;
    x.wr(x.P(0x20), 0);
    x.wr(x.P(0x21), 0);
    x.wr(x.P(0x15), 0);
    return true;
}

/// Esterço até o nó `[AR7+0x10]` (com o deslocamento `[0x5D]`) dividido pelos quadros até lá.
void steerToTarget(Tms32031State& c, MemoryBus& b, ISubroutines& s, double dist) {
    Ctx x{c, b, s};
    double v = x.f(x.D(0x26));
    if (v <= 0.0) v = 30.0; // LDF; LDFLE 30,0 (flags da própria carga)
    x.setR(0, dist);
    x.setR(1, v * static_cast<double>(si(x.rd(0xC92F))));
    x.call(0xA509);
    c.r[7] = static_cast<uint32_t>(static_cast<int32_t>(std::floor(x.R(0))));
    c.ar[2] = x.rd(x.P(0x10));
    x.setR(2, x.f(c.ar[2] + 1) - x.f(x.O(1)) + x.f(kLaneVec));
    x.setR(3, x.f(c.ar[2] + 3) - x.f(x.O(3)) + x.f(kLaneVec + 2));
    x.call(0x931A);
    x.setR(0, x.R(0) - x.K(0x9847));
    c.r[2] = x.rd(x.O(0x15));
    x.call(0x94A7);
    const double frames = static_cast<double>(si(c.r[7])) - 1.0;
    if (frames != 0.0) {
        x.setR(1, frames);
        x.call(0xA509);
    }
    x.wr(x.P(0xD), c.r[0]);
}

/// Buzina do modelo (`[modelo+7]`).
void modelHorn(Ctx& x) {
    x.c.ar[2] = x.rd(x.rd(x.P(0x19)) * 0xB + x.rd(kModels) + 7);
    if (x.c.ar[2] != 0) x.call(0x9188);
}

// --- 0x9765: mesmo sentido (ID 0x213) ---------------------------------------------------

void sameLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s);

/// 0988A  fim do quadro.
void sameEndFrame(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    if (x.rd(kLink) != 0) x.call(0x7ACF);
    inWindow(c, b, s);
    if (c.r[0] == 0) return removeCar(c, b, s);
    x.sleep(1, 0x9890);
}

/// 0981B…09889  navegação, esterço, buzina e acelerador.
void sameDrive(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    for (;;) {
        // 0981B
        inWindow(c, b, s);
        if (c.r[0] == 0) return removeCar(c, b, s);
        double dist;
        for (;;) {
            // 0981D
            c.r[1] = x.rd(0xE474) - x.rd(0xE46C);
            if (!(si(x.rd(x.P(0x13)) >> 8) > si(c.r[1]))) return x.call(0x9823); // BU $9823 (trava)
            c.ar[2] = x.rd(x.P(0x10));
            if (x.rd(c.ar[2] + 0x17) == 0) return removeCar(c, b, s);
            // 09827  ziguezague: 0x66DB (deslocamento [AR7+0x1B]); senão 0x66E3 (faixa da tabela)
            distToTarget(c, b, s, 0x17, x.rd(x.P(0x15)) != 1);
            dist = x.R(0);
            if (dist > 5000.0) break;
            const uint32_t next = x.rd(c.ar[2] + 0x17);
            x.wr(x.P(0x10), next);
            x.wr(x.P(0x13), x.rd(next + 0x1E));
        }
        steerToTarget(c, b, s, dist);
        // 0984F
        bool throttleUp = true;
        if (x.rd(x.P(0x21)) == 0) {
            focusOnPath(c, b, s);
            if (c.carry()) {
                x.call(0x67EC);
                if (!c.carry() && x.rd(x.P(0x14)) != 0) {
                    x.wr(x.P(0x21), 1);
                    if (!(si(x.rnd(0xA)) > 7)) {
                        // 0985D  buzina do sorteio e volta ao início do quadro
                        c.ar[2] = x.rd(x.rd(x.P(0x22)) * 3 + x.rd(0x972D) + 2);
                        x.call(0x9188);
                        x.wr(x.P(0x21), c.carry() ? 1u : 0u);
                        continue;
                    }
                    // 0986F  som aleatório de [9726] e freia
                    c.ar[2] = x.rd(x.rnd(6) + x.rd(0x9726));
                    x.call(0x9188);
                    x.wr(x.P(0x21), c.carry() ? 1u : 0u);
                    x.wf(x.D(0x24), x.f(x.P(0xF)) * x.K(0x9879));
                    throttleUp = false;
                }
            }
        }
        if (throttleUp) {
            // 0987C
            x.wf(x.D(0x2B), 0.0);
            double t = x.f(x.P(0xF)) * x.K(0x987F);
            if (t < x.K(0x9880)) t = x.K(0x9881);
            if (t > x.K(0x9882)) t = x.K(0x9883);
            x.wf(x.P(0xF), t);
            x.wf(x.D(0x24), t);
        }
        // 09886
        x.setR(2, x.f(x.P(0xD)) * x.K(0x9887));
        x.call(0x2C18);
        x.call(0x2EFD);
        return sameEndFrame(c, b, s);
    }
}

/// 097D3  laço por quadro.
void sameLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    if (x.rd(kGridLock) == 0x33BB) return sameEndFrame(c, b, s);
    aheadOfFocus(c, b, s);
    x.wr(x.P(0x14), c.carry() ? 1u : 0u);
    if (x.rd(x.P(0x15)) == 1) {
        bool weave = false;
        if (!(si(x.rd(0xE607)) > 5)) {
            c.ar[2] = x.rd(x.P(0x10));
            x.call(0x66A7);
            weave = c.r[0] == 1;
        }
        if (!weave) {
            x.wr(x.P(0x15), 0);
            x.wr(x.P(0xC), x.rnd(2) + 2);
        } else {
            // 097EB  ziguezague: ajusta passo/amplitude e centro ao acaso
            if (x.chance(0x64)) {
                x.wf(x.P(0x24), x.rndF(x.K(0x97EE), false) + x.K(0x97F0));
                x.wf(x.P(0x25), x.rndF(520.0, false) + 2600.0);
            }
            if (x.chance(0xC8)) x.wf(x.P(0x26), x.rndF(576.0, true));
            x.setR(2, x.f(x.P(0x23)) + x.f(x.P(0x24)));
            x.call(0x9313);
            x.wr(x.P(0x23), c.r[2]);
            x.call(0x92E2);
            x.wf(x.P(0x1B), x.R(0) * x.f(x.P(0x25)) + x.f(x.P(0x26)));
        }
    }
    // 09805
    if (x.rd(x.D(0x33)) != 0) return wreckSame(c, b, s);
    if (x.rd(x.P(0x20)) == 0) {
        x.wr(x.P(0x18), x.rd(x.P(0x17)));
        distToFocus(c, b, s);
        x.wr(x.P(0x17), c.r[0]);
        c.r[3] = x.rd(x.P(0x14));
        if (c.r[3] != 0 && !(x.R(0) > 7500.0)) {
            x.wr(x.P(0x20), 1);
            modelHorn(x);
        }
    }
    sameDrive(c, b, s);
}

/// 09769  sorteio do modelo (tabela `[972D]`: modelo, flags, som); com link evita os de flag 2.
void sameCreate(Tms32031State& c, MemoryBus& b, ISubroutines& s, uint32_t pick) {
    Ctx x{c, b, s};
    for (;;) {
        c.r[4] = 0;
        c.r[5] = pick;
        x.wr(x.P(0x22), pick);
        c.ar[2] = pick * 3 + x.rd(0x972D);
        if (x.rd(kLink) != 0 && (x.rd(c.ar[2] + 1) & 2u)) {
            pick = 5;
            continue;
        }
        break;
    }
    c.ar[2] = x.rd(c.ar[2]);
    if (!spawn(c, b, s, 0x213, true, 0x17)) return;
    // 097AD  modelos com flag 1 ziguezagueiam depois de 1,1 minuto
    c.ar[2] = x.rd(x.P(0x22)) * 3 + x.rd(0x972D);
    if ((x.rd(c.ar[2] + 1) & 1u) && !(x.f(0xC93A) < x.K(0x97B4))) {
        x.wr(x.P(0x15), 1);
        x.wf(x.P(0x1B), 576.0);
        x.wr(x.P(0x14), 0x1E);
        x.wf(x.P(0x24), x.rndF(x.K(0x97BC), false) + x.K(0x97BE));
        x.wf(x.P(0x25), x.rndF(520.0, false) + 2600.0);
        x.wf(x.P(0x26), x.rndF(576.0, true));
    }
    x.setR(6, 0.0);
    const uint32_t mirrored = x.rd(0xE8E4);
    if (mirrored != 0) {
        x.wr(x.D(0x59), mirrored);
        x.wr(x.O(0x10), x.rd(0xE8E5));
        x.wr(x.D(0x58), 1);
        return parkedLoop(c, b, s);
    }
    x.call(0x6628);
    linkAnnounce(c, b, s);
    sameLoop(c, b, s);
}

/// 09765  entrada local.
void sameEntry(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    c.r[5] = 0;
    x.wr(0xE8E4, 0);
    sameCreate(c, b, s, x.rnd(0x11));
}

/// 09761  entrada pelo link: AR6 = textura, R4 = número, R5 = sorteio do modelo.
void sameEntryLink(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    b.writeWord(0xE8E5, c.ar[6]);
    b.writeWord(0xE8E4, c.r[4]);
    sameCreate(c, b, s, c.r[5]);
}

// --- 0xA3FF: contramão (ID 0x214) -------------------------------------------------------

void oncomingLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s);

/// 0A4EB
void oncomingEndFrame(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    x.sleep(1, 0xA4ED);
}

/// 0A49F…0A4EA  navegação pelos nós `+0x21`.
void oncomingDrive(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    c.ar[2] = x.rd(x.P(0x10));
    c.ar[0] = x.rd(c.ar[2] + 0x21);
    if (c.ar[0] == 0) return oncomingEndFrame(c, b, s);
    if (x.rd(x.P(0x10)) == x.rd(kFirstNode)) return removeCar(c, b, s);
    if (si(x.rd(x.P(0x13))) < si(x.rd(c.ar[2] + 0x1E))) return removeCar(c, b, s);
    double dist;
    for (;;) {
        // 0A4AB
        c.r[1] = x.rd(0xE474) - x.rd(0xE46C);
        if (!(si(x.rd(x.P(0x13)) >> 8) > si(c.r[1]))) return removeCar(c, b, s);
        c.ar[2] = x.rd(x.P(0x10));
        if (x.rd(c.ar[2] + 0x21) == 0) return oncomingEndFrame(c, b, s);
        distToTarget(c, b, s, 0x21, true);
        dist = x.R(0);
        if (dist > 5000.0) break;
        const uint32_t next = x.rd(c.ar[2] + 0x21);
        x.wr(x.P(0x10), next);
        x.wr(x.P(0x13), x.rd(next + 0x1E));
    }
    steerToTarget(c, b, s, dist);
    // 0A4D7  freia diante da trajetória do focado
    focusOnPath(c, b, s);
    if (c.carry()) {
        x.wf(x.D(0x24), x.f(x.P(0xF)) * x.K(0xA4DA));
    } else {
        double t = x.f(x.P(0xF)) * x.K(0xA4DE);
        if (t < x.K(0xA4DF)) t = x.K(0xA4E0);
        if (t > x.K(0xA4E1)) t = x.K(0xA4E2);
        x.wf(x.P(0xF), t);
        x.wf(x.D(0x24), t);
    }
    x.setR(2, x.f(x.P(0xD)) * x.K(0xA4E6));
    x.call(0x67E3);
    x.wr(x.D(0x46), c.r[0]);
    x.call(0x2C18);
    x.call(0x2EFD);
    oncomingEndFrame(c, b, s);
}

/// 0A451  laço por quadro.
void oncomingLoop(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    if (x.rd(kGridLock) == 0x33BB) return oncomingEndFrame(c, b, s);
    aheadOfFocus(c, b, s);
    x.wr(x.P(0x14), c.carry() ? 1u : 0u);
    if (x.rd(x.P(0x15)) == 1) {
        const int32_t t = si(x.rd(x.P(0x14))) - 1;
        x.wr(x.P(0x14), static_cast<uint32_t>(t));
        if (!(t > 0)) {
            x.wr(x.P(0xC), x.rd(x.P(0xC)) == 2 ? 3u : 2u);
            x.wr(x.P(0x14), x.rnd(0x32) + 0x1E);
        }
    }
    // 0A469  algum ponto de contato sobre a estrada (tipo 0x300)?
    bool onRoad = false;
    for (uint32_t o : {0xBu, 0x11u, 0x1Du, 0x17u}) {
        c.ar[0] = x.rd(x.D(o));
        if (x.rd(c.ar[0] + 0xF) == 0x300) {
            onRoad = true;
            break;
        }
    }
    if (onRoad) {
        x.wr(x.P(0x20), 1);
    } else if (x.rd(x.P(0x20)) != 0) {
        // 0A4EE  saiu da estrada: destroço
        x.create(0xC6B7, 2);
        c.ar[6] = 0xA;
        return wreckOncomingLoop(c, b, s);
    }
    // 0A47E
    if (x.rd(x.D(0x33)) != 0) {
        x.wr(x.D(0x33), 0);
        x.create(0xC6B6, 0x800);
    }
    if (x.rd(x.P(0x21)) == 0 && x.rd(x.P(0x14)) != 0) {
        distToFocus(c, b, s);
        const double prev = x.f(x.P(0x17));
        x.wr(x.P(0x18), x.rd(x.P(0x17)));
        x.wr(x.P(0x17), c.r[0]);
        const double now = x.R(0);
        if (!(now > 5000.0) && !(prev < now) && !(x.f(x.rd(kFocusData) + 0x26) < 127.0)) {
            x.wr(x.P(0x21), 1);
            modelHorn(x);
        }
    }
    oncomingDrive(c, b, s);
}

/// 0A3FF  espera `[E5F4]`, sorteia o modelo (tabela `[A3DE]`: modelo, flags) e cria o carro.
void oncomingEntry(Tms32031State& c, MemoryBus& b, ISubroutines& s) {
    Ctx x{c, b, s};
    if (x.rd(0xE5F4) == 0) return x.sleep(1, 0xA3FF);
    uint32_t pick = x.rnd(0x10);
    for (;;) {
        x.wr(x.P(0x22), pick);
        c.ar[2] = x.rd(0xA3DE) + pick * 2;
        c.r[0] = x.rd(c.ar[2] + 1);
        x.wr(x.P(0x23), c.r[0]);
        c.ar[2] = x.rd(c.ar[2]);
        if (x.rd(kLink) != 0 && (c.r[0] & 2u)) {
            pick = 0;
            continue;
        }
        break;
    }
    if (!spawn(c, b, s, 0x214, false, 0x21)) return;
    if (x.rnd(0xA) == 0) {
        x.wr(x.P(0x15), 1);
        x.wr(x.P(0x14), x.rnd(0x32) + 0x14);
    }
    oncomingLoop(c, b, s);
}

} // namespace

void registerTrafficCars(SubroutineDispatcher& d) {
    d.add(0x668C, randomSkin);
    d.add(0x66BC, distToFocus);
    d.add(0x66DB, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { distToTarget(c, b, s, 0x17, false); });
    d.add(0x66E3, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { distToTarget(c, b, s, 0x17, true); });
    d.add(0x66EB, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { distToTarget(c, b, s, 0x21, false); });
    d.add(0x66F3, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { distToTarget(c, b, s, 0x21, true); });
    d.add(0x6708, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { laneOffset(c, b, s, 0x17, true); });
    d.add(0x670F, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { laneOffset(c, b, s, 0x21, true); });
    d.add(0x6739, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { laneOffset(c, b, s, 0x17, false); });
    d.add(0x6740, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) { laneOffset(c, b, s, 0x21, false); });
    d.add(0x67B0, focusOnPath);
    d.add(0x68F4, aheadOfFocus);
    d.add(0x7B25, linkAnnounce);
    d.add(0x9891, inWindow);
    d.add(0x9924, linkRemove);
    d.add(0x98A9, removeCarQuiet);
    d.add(0x9903, handBack);
    d.add(0x992B, receiveRemove);
    d.add(0x98F2, receiveHandBack);
    d.add(0x9761, sameEntryLink);
    d.add(0x9765, sameEntry);
    d.add(0x9890, sameLoop);
    d.add(0x98E1, [](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        if (b.readWord(kLink) != 0) s.call(0x7ACF, c, b);
        wreckSameLoop(c, b, s);
    });
    d.add(0xA3FF, oncomingEntry);
    d.add(0xA4ED, oncomingLoop);
    d.add(0xA507, wreckOncomingLoop);
    d.add(0xA718, parked);
    d.add(0xA72C, parkedLoop);
}

} // namespace rea::core::game
