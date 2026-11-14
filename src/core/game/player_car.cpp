/**
 * @file player_car.cpp
 * @brief Tradução de `0x29B8`–`0x2B92` (processo do carro do jogador), `0x2976`–`0x2986`,
 *        `0x2B93`–`0x2C17`, `0x2C4C`–`0x2C91`, `0x2CD3`–`0x2CEA`, `0x2D4A`–`0x2D61`,
 *        `0x2E0C`–`0x2E72`, `0x2E88`–`0x2EFC`, `0x2F17`–`0x2F2B`, `0x304A`–`0x3084`,
 *        `0x3097`–`0x3158`, `0x318C`–`0x31A5`, `0x25D6`–`0x25E7`, `0x5567`–`0x559D`,
 *        `0x3C71`–`0x3C79`, `0x1D59`–`0x1D7E` e a guarda de `0x472A`.
 */
#include "core/game/player_car.hpp"

#include <cmath>
#include <cstdlib>

#include "core/game/process_ctx.hpp"

namespace rea::core::game {

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s(uint32_t v) { return static_cast<int32_t>(v); }

constexpr uint32_t kCarObj = 0xE876;   ///< Objeto do carro do jogador.
constexpr uint32_t kCarData = 0xE878;  ///< Dados D do carro do jogador.
constexpr uint32_t kView = 0xE871;     ///< Vista: 0 cabine, 1 atrás, 2 longe.
constexpr uint32_t kSpin = 0xE873;     ///< Rodopio em curso.
constexpr uint32_t kCamYaw = 0x809807; ///< Rumo da câmera (página 0x80 do DP).
constexpr uint32_t kCamTilt = 0x809806;  ///< Inclinação da câmera (página 0x80 do DP).

/// Contexto de processo com atalhos para D (AR5), objeto (AR4) e registradores float.
struct Ctx : ProcCtx {
    /// Endereço do campo @p o dos dados do carro (AR5).
    uint32_t D(uint32_t o) const { return cpu.ar[5] + o; }
    /// Endereço do campo @p o do objeto (AR4).
    uint32_t O(uint32_t o) const { return cpu.ar[4] + o; }
    /// Float no campo @p o dos dados do carro (AR5).
    double d(uint32_t o) const { return rf(D(o)); }
    /// Grava o float @p v no campo @p o dos dados do carro (AR5).
    void setD(uint32_t o, double v) { wf(D(o), v); }
    /// Lê o registrador R@p i como float TMS.
    double R(int i) const { return tmsToDouble(cpu.r[i]); }
    /// Grava @p v, convertido para float TMS, no registrador R@p i.
    void setR(int i, double v) { cpu.r[i] = doubleToTms(v); }
    /// Float curto de 16 bits (imediato da instrução) na palavra @p pc da ROM.
    double K(uint32_t pc) const { return k(pc); }
    /// Quadros da última iteração (`[C92F]`), como double.
    double fc92f() const { return static_cast<double>(s(rd(0xC92F))); }
    /// `0x9308` sobre @p v (−π..π), preservando R2.
    double wrap(double v) {
        const uint32_t r2 = cpu.r[2];
        setR(2, v);
        call(0x9308);
        const double out = R(2);
        cpu.r[2] = r2;
        return out;
    }
    /// `0x938A`: rotY(R2) em AR2.
    void rotY(uint32_t mat, uint32_t angleBits) {
        cpu.ar[2] = mat;
        cpu.r[2] = angleBits;
        call(0x938A);
    }
    /// `LDI m,R2 / LDI R2,R3 (ou dst) / CALL $9494`: dst = AR2 × m.
    void mulInto(uint32_t m, uint32_t dst) {
        cpu.r[2] = m;
        cpu.r[3] = dst;
        call(0x9494);
    }
    /// `LDI tbl,AR2 / LDI id,R2 / CALL fn` (cria processo).
    void spawn(uint32_t fn, uint32_t tablePtr, uint32_t id = 0x500) {
        cpu.ar[2] = rd(tablePtr);
        cpu.r[2] = id;
        call(fn);
    }
};

/// Registra @p f no endereço @p a do despachante, com um Ctx montado a cada chamada.
template <typename F>
void addCtx(SubroutineDispatcher& d, uint32_t a, F f) {
    d.add(a, [f](Tms32031State& c, MemoryBus& b, ISubroutines& s) {
        Ctx x{{c, b, s}};
        f(x);
    });
}

/// true na cabine Slave com o bit 4 das DIPs desligado (o carro dela larga 1152 à frente).
bool rightCabinet(Ctx& x) {
    const uint32_t f = x.rd(0xC8CC);
    return (f & 4u) == 0 && (f & 1u) != 0;
}

/// `[AR3+i]` = `[obj+1+i]` + `[AR2+i]` (posição + deslocamento).
void addOffset(Ctx& x, uint32_t dst, uint32_t off) {
    for (uint32_t i = 0; i < 3; ++i) x.wf(dst + i, x.rf(x.O(1 + i)) + x.rf(off + i));
}

/// `0x9414` + (0, −960, −960) girado pela matriz @p mat → `[0x45]` = posição + deslocamento.
void cameraBehindStart(Ctx& x, uint32_t mat) {
    Tms32031State& cpu = x.cpu;
    cpu.r[2] = mat;
    x.call(0x9414);
    x.wf(cpu.ar[2] + 1, -960.0);
    x.wf(cpu.ar[2] + 2, -960.0);
    cpu.r[3] = cpu.ar[2];
    x.call(0x941E);
    cpu.ar[3] = x.rd(0x45);
    addOffset(x, cpu.ar[3], cpu.ar[2]);
}

void loopBody(Ctx& x);

/// `0x2A3F`: estado inicial da câmera, processo da vista e luzes; segue no laço.
void commonInit(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    x.wf(0xE86C, 0.0);
    x.wf(0xE86F, 0.0);
    x.wf(0xE86E, -400.0);
    x.wf(0xE870, -400.0);
    x.wf(0xE86B, 2200.0);
    x.wf(0xE86D, 2200.0);
    cpu.ar[2] = x.rd(0xC61C);
    cpu.r[0] = x.rd(kView);
    if (cpu.r[0] == 0) cpu.ar[2] = x.rd(0xC61B);
    cpu.r[1] = 1;
    x.wr(kView, 1);
    if (cpu.r[0] == 2) cpu.ar[2] = x.rd(0xC61D);
    cpu.r[2] = 0x500;
    x.call(0xA844);
    cpu.r[0] = 0;
    for (uint32_t a : {0xE872u, kSpin, 0xE87Bu}) x.wr(a, 0);
    x.call(0x2E31);
    x.wf(0xE87D, 1.0);
    x.wf(0xE87E, 1.0);
    x.orField(0xC8C5, 0, 0x100);
    loopBody(x);
}

/// `0x29B8`: o carro saiu da garagem — parado no rumo `[E402]`, câmera atrás, vista 1.
void startFromGarage(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    cpu.ar[5] = x.rd(x.O(0x1B));
    cpu.r[0] = 0;
    x.wr(x.D(0x38), 0);
    x.call(0x2EFD);
    cpu.ar[2] = x.rd(x.D(0x37));
    cpu.r[0] = x.rd(0xE640);
    uint32_t ar0 = cpu.r[0];
    if (cpu.r[0] == 4) ar0 = 0xE;
    if (cpu.r[0] == 5) ar0 = 0xB;
    if (ar0 == 6) ar0 = 7;
    if (s(ar0) >= 7) ar0 = 4;  // ROM: 0xE e 0xB também caem aqui (todas as variantes ≥ 4 → 4)
    cpu.ar[0] = ar0 * 0xB + x.rd(0x961B);
    cpu.ar[2] = x.rd(cpu.ar[0] + 1);
    x.call(0x9ECD);
    x.wr(x.O(0x10), cpu.r[0]);
    const uint32_t yaw = x.rd(0xE402);
    x.wr(x.D(0x2C), yaw);
    x.wr(x.D(0x2D), yaw);
    x.setR(0, 0.0);
    x.setD(0x24, 0.0);
    x.setD(0x26, 0.0);
    x.rotY(x.O(4), yaw);
    x.rotY(x.rd(0x5A), x.rd(0xE402));
    cameraBehindStart(x, cpu.ar[2]);
    x.call(0x7298);
    x.wr(kView, 1);
    commonInit(x);
}

/// `0x29EE`: largada sem jogador (atrato) — carro novo no nó `[[E46E]+0x21]`, câmera
/// 480 (+1152 à direita) à frente e atrás.
void startAttract(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    x.call(0x7025);
    cpu.ar[4] = cpu.ar[0];
    x.call(0x2987);
    for (uint32_t i = 0; i < 3; ++i) x.wr(x.O(1 + i), x.rd(0xE3FF + i));
    x.setVar(3, 0x110);
    cpu.ar[5] = x.rd(x.O(0x1B));
    x.call(0x2EFD);
    cpu.ar[2] = x.rd(x.D(0x37));
    cpu.ar[2] = x.rd(0xE46E);
    cpu.r[0] = x.rd(cpu.ar[2] + 0x1E);
    cpu.ar[2] = x.rd(cpu.ar[2] + 0x21);
    for (uint32_t i = 0; i < 3; ++i) x.wr(x.O(1 + i), x.rd(cpu.ar[2] + 1 + i));
    x.call(0x303A);
    cpu.r[2] = cpu.r[0];
    x.wr(x.D(0x2C), cpu.r[2]);
    x.wr(x.D(0x2D), cpu.r[2]);
    cpu.ar[2] = x.O(4);
    x.call(0x938A);
    x.setR(2, -x.R(2));
    x.call(0x9308);
    x.wr(kCamYaw, cpu.r[2]);
    cpu.dp = 0;
    cpu.ar[2] = x.rd(0x47);
    x.call(0x938A);
    cpu.r[2] = x.O(4);
    x.call(0x9414);
    double ahead = 480.0;
    if (rightCabinet(x)) ahead += 1152.0;
    x.wf(cpu.ar[2], ahead);
    cpu.r[3] = cpu.ar[2];
    x.call(0x941E);
    cpu.ar[2] = cpu.r[3];
    for (uint32_t i = 0; i < 3; ++i) x.wf(x.O(1 + i), x.rf(x.O(1 + i)) + x.rf(cpu.ar[2] + i));
    cpu.ar[2] = x.rd(0x47);
    cameraBehindStart(x, cpu.ar[2]);
    x.call(0x7298);
    commonInit(x);
}

// --- Câmera de perseguição -----------------------------------------------------------------

/// `0x2BCF`: posição da câmera para o rumo R0 em `[0x5D]` (AR3): carro − rotY(R0)·(0, 0, −`[E86B]`)
/// no X, + altura `[E86E]` no Y. Preserva R0 e AR2.
void cameraPointFor(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    const uint32_t r0 = cpu.r[0], ar2 = cpu.ar[2];
    cpu.ar[2] = x.rd(0x5A);
    cpu.r[2] = r0;
    x.call(0x938A);
    x.setR(2, -x.rf(0xE86B));
    cpu.r[3] = x.rd(0x5D);
    cpu.ar[3] = cpu.r[3];
    x.call(0x9099);
    const uint32_t v = cpu.ar[3];
    x.wf(v, x.rf(x.O(1)) - x.rf(v));
    x.wf(v + 1, x.rf(0xE86E) + x.rf(x.O(2)) + x.rf(v + 1));
    x.wf(v + 2, x.rf(x.O(3)) + x.rf(v + 2));
    cpu.ar[2] = ar2;
    cpu.r[0] = r0;
}

/// `0x2B93` (R1 = −170) / `0x2B95` (R1 = +170): desloca o ponto AR0 (= `[0x5D]`, no lugar) para
/// o lado do rumo R0 e procura o chão (`0x1F8B`, carry = achou). Preserva R0, R3, AR0 e AR4.
void sideProbe(Ctx& x, double side) {
    Tms32031State& cpu = x.cpu;
    cpu.r[1] = doubleToTms(side);
    const uint32_t r0 = cpu.r[0], r3 = cpu.r[3], ar0 = cpu.ar[0], ar4 = cpu.ar[4];
    cpu.ar[4] = cpu.ar[0];
    cpu.ar[0] = x.rd(0x5D);
    cpu.r[2] = r0;
    x.call(0x92E2);
    double r3f = -x.R(0);
    x.call(0x92DF);
    const double r1 = x.R(1);
    const double c = x.R(0) * r1;
    r3f *= r1;
    x.wf(cpu.ar[0], c + x.rf(cpu.ar[4]));
    x.wr(cpu.ar[0] + 1, x.rd(cpu.ar[4] + 1));
    x.wf(cpu.ar[0] + 2, r3f + x.rf(cpu.ar[4] + 2));
    cpu.ar[4] = cpu.ar[0];
    x.call(0x1F8B);
    cpu.ar[4] = ar4;
    cpu.ar[0] = ar0;
    cpu.r[3] = r3;
    cpu.r[0] = r0;
}

/// `0x2BAF`: R3 = 1 (sem chão à esquerda) + 2 (sem chão à direita); Z de R3.
void sideProbes(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    sideProbe(x, -170.0);
    cpu.r[3] = cpu.carry() ? 0u : 1u;
    sideProbe(x, 170.0);
    cpu.r[1] = cpu.carry() ? 0u : 2u;
    cpu.r[3] += cpu.r[1];
    cpu.setFlag(Tms32031State::kStZero, cpu.r[3] == 0);
}

/// `0x2BB7`: câmera no rumo R0 tem lateral sem chão? (Z = 0 → sim). Preserva AR0.
bool cameraBlocked(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    const uint32_t ar0 = cpu.ar[0];
    cameraPointFor(x);
    cpu.ar[0] = cpu.ar[3];
    sideProbes(x);
    cpu.ar[0] = ar0;
    return cpu.r[3] != 0;
}

/// `0x2BBD`: R0 = rumo R0 puxado até 0,1 rad na direção do rumo da pista. Preserva AR0 e AR2.
void cameraSlide(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    const uint32_t ar0 = cpu.ar[0], ar2 = cpu.ar[2];
    cpu.r[3] = cpu.r[0];
    x.call(0x3037);
    x.setR(2, x.R(3) + x.R(0));  // R0 inteiro 0 (sem nó) vale 1,0 como float — igual à ROM
    x.call(0x9308);
    const double r2 = x.R(2);
    double r1;
    if (std::fabs(r2) > x.K(0x2BC4)) r1 = r2 < 0.0 ? x.K(0x2BC9) : x.K(0x2BCA);
    else r1 = -r2;
    x.setR(1, r1);
    x.setR(0, x.R(3) + r1);
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
}

/// `0x2AFB`–`0x2B39`: câmera de perseguição (vistas 1 e 2).
void chaseCamera(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    x.wf(kCamTilt, 0.0);
    bool fromCurrent = true;
    const double vsum = std::fabs(x.rf(x.O(0x11))) + std::fabs(x.rf(x.O(0x13)));
    if (!(vsum < 2.0) && x.rd(x.D(0x31)) == 0) {
        cpu.r[3] = x.rd(x.O(0x11));
        cpu.r[2] = x.rd(x.O(0x13));
        x.call(0x931A);
        double r0 = x.R(0) - x.rf(kCamYaw);
        double r1 = 0.0;
        if (r0 > x.K(0x2B0C)) r1 = x.K(0x2B0D);
        if (r0 < x.K(0x2B0E)) r1 = x.K(0x2B0F);
        r0 = (r0 + r1) * x.K(0x2B11) + x.rf(kCamYaw);
        cpu.dp = 0;
        x.setR(0, r0);
        fromCurrent = false;
        if (cameraBlocked(x)) {
            const double r4 = x.R(0);
            cameraSlide(x);
            const double r5 = x.R(0);
            const double r6 = x.rf((cpu.dp << 16) | 0x9807u);  // quirk: LDIU $0080,R6 (DP fica 0)
            cpu.dp = 0;
            const double a = std::fabs(x.wrap(r5 - r6));
            const double b = std::fabs(x.wrap(r5 - r4));
            if (a < b) fromCurrent = true;
            else x.setR(0, r5);
        }
    }
    if (fromCurrent) {
        // 02B26
        cpu.r[0] = x.rd(kCamYaw);
        cpu.dp = 0;
        if (cameraBlocked(x)) cameraSlide(x);
    }
    // 02B2C
    x.wr(kCamYaw, cpu.r[0]);
    cpu.dp = 0;
    x.rotY(x.rd(0x47), cpu.r[0]);
    x.call(0x2D4A);
    x.setR(2, -x.R(1));
    cpu.ar[2] = x.rd(0x5A);
    x.call(0x938A);
    x.mulInto(x.O(4), x.O(4));
}

/// `0x2ADB`–`0x2AFA`: câmera de cabine — matriz transposta do carro, rumo −`[D+0x2C]`,
/// inclinações `[D+0x42]`·(−0,7) e `[D+0x43]`·(−0,5); suavizada com a matriz anterior (`0x3195`).
void cockpitCamera(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    const uint32_t cam = x.rd(0x47);
    uint32_t saved[9];  // 0318C: cópia de [0x47] na pilha
    for (uint32_t i = 0; i < 9; ++i) saved[i] = x.rd(cam + i);
    cpu.ar[2] = cam;
    x.setR(2, -x.d(0x2C));
    x.wr(kCamYaw, cpu.r[2]);
    cpu.dp = 0;
    cpu.r[2] = x.O(4);
    x.call(0x93FD);
    x.setD(0x2F, 0.0);
    x.setR(2, x.d(0x42) * x.K(0x2AE7));
    x.wr(kCamTilt, cpu.r[2]);
    cpu.dp = 0;
    cpu.ar[2] = x.rd(0x5B);
    x.call(0x9377);
    cpu.ar[0] = cpu.ar[2];
    cpu.ar[1] = x.rd(0x5C);
    const uint32_t pushed = cpu.ar[1];
    x.setR(2, x.d(0x43) * -0.5);
    cpu.ar[2] = x.rd(0x5A);
    x.call(0x93C4);
    x.call(0x9481);
    cpu.r[2] = pushed;
    cpu.ar[2] = x.rd(0x47);
    cpu.r[3] = cpu.ar[2];
    x.call(0x9494);
    // 03195: sem rodopio de 1 quadro, 80% da matriz anterior + 20% da nova
    if (x.rd(x.D(0x31)) == 1) return;
    cpu.ar[0] = x.rd(0x47);
    for (uint32_t i = 0; i < 9; ++i) {
        const double old = tmsToDouble(saved[i]) * x.K(0x319F);
        const double cur = x.rf(cpu.ar[0] + i) * x.K(0x31A1);
        x.wf(cpu.ar[0] + i, cur + old);
    }
}

/// `0x2B3A`–`0x2B5D`: posição da câmera, sons e periféricos; dorme 1 quadro.
void cameraAndSounds(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    cpu.ar[2] = x.rd(0x47);
    x.setR(2, -x.rf(0xE86B));
    cpu.r[3] = x.rd(0x5D);
    cpu.ar[3] = cpu.r[3];
    x.call(0x9099);
    cpu.ar[0] = x.rd(0x45);
    x.wf(cpu.ar[0], x.rf(x.O(1)) - x.rf(cpu.ar[3]));
    double h = 0.0;
    if (x.rd(kSpin) != 0) h = x.d(3);
    h = h * x.K(0x2B46) + x.rf(0xE86E) + x.rf(x.O(2)) + x.rf(cpu.ar[3] + 1);
    x.wf(cpu.ar[0] + 1, h);
    x.wf(cpu.ar[0] + 2, x.rf(x.O(3)) + x.rf(cpu.ar[3] + 2));
    if (x.rd(kView) != 0) x.call(0x2BE5);
    x.call(0x2C4C);
    x.call(0x3097);
    x.call(0x472A);
    x.call(0x304A);
    x.call(0x67EC);
    x.wr(x.D(0x46), cpu.r[0]);
    if (x.rd(0xC8E3) != 0) {
        cpu.ar[0] = x.rd(x.D(0x37));
        cpu.r[0] = x.rd(cpu.ar[0] + 0x1E);
        x.wr(x.D(0x57), cpu.r[0]);
        x.call(0x7ADB);
        x.call(0x2B61);
    }
    x.sleep(1, 0x2B60);
}

/// `0x2A72`–`0x2AD7`: um quadro dirigindo.
void drive(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    x.call(0x2CD3);
    if (cpu.r[0] == 0) x.call(0x2BF5);
    if (x.rf(x.O(8)) < x.K(0x2A76)) x.call(0x2BF5);
    x.call(0x2E61);
    const uint32_t oldGear = x.rd(x.D(0x38));
    x.wr(x.D(0x38), cpu.r[0]);
    if (s(oldGear) >= s(cpu.r[0])) {
        x.call(0x2E57);
    } else {
        x.call(0x2E57);
        x.setR(0, x.R(0) * 2.0);
        if (x.R(0) > x.K(0x2A7F)) {
            const uint32_t r0 = cpu.r[0];
            x.sound(0x1C8);
            x.spawn(0xA868, 0xC61E);
            x.spawn(0xA868, 0xC61F);
            cpu.r[0] = r0;
        }
    }
    // 02A8D
    if ((x.rd(0xC8C5) & 0x200u) == 0) x.setR(0, 0.0);
    x.wr(x.D(0x24), cpu.r[0]);
    x.call(0x2E0C);
    x.wr(x.D(0x2B), cpu.r[0]);
    x.call(0x2D83);
    double v = x.d(0x26);
    if (s(x.rd(0xE604)) <= 0) x.wr(0xE5EC, 0x5577);
    x.wr(0xE605, tmsFix(v * x.K(0x2A99)));
    x.call(0x2C9C);
    double slip = x.d(0x25);
    if (x.rd(0xC8E3) != 0 && x.rd(x.D(0x31)) == 0) {
        slip *= 2.0 - x.rf(0xE87E);
        x.setD(0x25, slip);
    }
    double k = slip * slip;
    if (k > 1.0) k = 1.0;
    k = 1.0 - k;
    if (k < x.K(0x2AAA)) k = x.K(0x2AAB);
    x.wf(0xC8EB, k * 2.5);
    x.call(0x2EFD);
    x.call(0x2E88);
    x.call(0x2CEB);
    const uint32_t turn = cpu.r[0];
    x.call(0x2F63);
    x.call(0x2F17);
    x.setR(2, tmsToDouble(turn) + x.d(0x2F));
    cpu.ar[2] = x.rd(0x5A);
    x.call(0x938A);
    x.mulInto(x.O(4), x.O(4));
    x.setR(2, x.d(0x2D) - x.d(0x2C));
    cpu.ar[2] = x.rd(0x5B);
    x.call(0x938A);
    x.mulInto(x.O(4), cpu.ar[2]);
    x.setR(2, x.d(0x27) * x.rf(0xE87E));
    cpu.ar[2] = x.rd(0x5B);
    if (x.rd(x.D(0x20)) == 0) {
        cpu.r[3] = x.O(0x11);
        x.call(0x9099);
    }
    x.call(0x9081);
    cpu.r[3] = x.rd(x.O(0x1B));
    x.call(0x1FF6);
    cpu.r[2] = x.rd(x.D(0x2C));
    x.wr(x.O(0x15), cpu.r[2]);
    cpu.ar[2] = x.rd(0x5A);
    x.call(0x938A);
    x.mulInto(x.O(4), x.O(4));
}

/// `0x2A5C`: laço do processo (1 quadro).
void loopBody(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    if (x.rd(0xE635) != 0) return x.jump(0x1D59);
    const int32_t dt = s(x.rd(0xC92F));
    int32_t lead = -dt;
    if (x.rd(0xE607) == 1) lead = s(x.rd(0xE87B));
    x.wr(0xE87B, static_cast<uint32_t>(lead + dt));
    cpu.r[1] = x.rd(0xE604);
    if (s(cpu.r[1]) <= 0) x.call(0x3C71);
    if ((x.rd(0xC8C5) & 0xFu) == 5) return x.jump(0x2976);
    cpu.ar[4] = x.rd(kCarObj);
    cpu.ar[5] = x.rd(x.O(0x1B));
    if (x.rd(kSpin) != 0) {
        x.call(0x5567);
        x.call(0x2F63);
    } else {
        drive(x);
    }
    // 02AD8
    x.call(0x2EE6);
    if (x.rd(kView) == 0) cockpitCamera(x);
    else chaseCamera(x);
    cameraAndSounds(x);
}

/// `0x2976`/`0x297D`/`0x2982`: modo 5 — só a transição da câmera até o modo voltar a 4.
void modeFiveWait(Ctx& x, bool entry) {
    Tms32031State& cpu = x.cpu;
    if (entry && x.rd(0xE53F) == 0) {
        cpu.ar[2] = x.rd(kCarObj);
        if ((x.rd(cpu.ar[2] + 0xE) & 0x3000u) == 0) x.call(0x7093);
    }
    cpu.ar[4] = x.rd(kCarObj);
    cpu.ar[5] = x.rd(kCarData);
    x.call(0x2EE6);
    x.sleep(1, 0x2982);
}

// --- Câmera: transição entre vistas ----------------------------------------------------------

/// `0x2EE6`: aproxima `[E86B]`/`[E86E]` do alvo `[E86D]`/`[E870]` pelos passos `[E86C]`/`[E86F]`.
void cameraZoom(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    const double step = x.rf(0xE86C);
    cpu.r[2] = x.rd(0xE86C);
    if (step == 0.0) return;
    double r1 = x.rf(0xE86E) + x.rf(0xE86F);
    double r2 = step + x.rf(0xE86B);
    const double target = x.rf(0xE86D);
    const bool reached = step < 0.0 ? !(r2 > target) : r2 >= target;
    if (reached) {
        if (x.rd(kView) == 0) {
            cpu.ar[2] = x.rd(kCarObj);
            x.call(0x715F);  // cabine: tira o carro da lista de desenho
        }
        x.wf(0xE86C, 0.0);
        x.wf(0xE86F, 0.0);
        r2 = x.rf(0xE86D);
        r1 = x.rf(0xE870);
    }
    x.wf(0xE86E, r1);
    x.wf(0xE86B, r2);
    x.setR(1, r1);
    x.setR(2, r2);
}

/// `0x2EB4`/`0x2EB9`/`0x2EBE`: processos dos botões de vista (distância, altura, vista, lâmpadas).
void viewButton(Ctx& x, double dist, double height, uint32_t view, uint32_t lamps) {
    Tms32031State& cpu = x.cpu;
    if ((x.rd(0xC8C5) & 0xFu) != 4) return x.die();
    const uint32_t cur = x.rd(kView);
    if (view == cur) return x.die();
    x.sound(s(view) < s(cur) ? 0x1B9 : 0x1BC);
    x.wr(0xC8CD, (x.rd(0xC8CD) & ~0xEu) | lamps);
    x.wf(0xE86D, dist);
    x.wf(0xE870, height);
    double r4 = (dist - x.rf(0xE86B)) * x.K(0x2ED2);
    double r5 = (height - x.rf(0xE86E)) * x.K(0x2ED4);
    double k = 1.0;
    if (std::fabs(r4) < 25.0) k = 4.0;
    r4 *= k;
    r5 *= k;
    x.wf(0xE86C, r4);
    x.wf(0xE86F, r5);
    cpu.r[3] = x.rd(kView);
    x.wr(kView, view);
    if (cpu.r[3] == 0) {
        cpu.ar[2] = x.rd(kCarObj);
        if ((x.rd(cpu.ar[2] + 0xE) & 0x3000u) == 0) x.call(0x7093);  // sai da cabine: volta a desenhar
    }
    x.call(0x2EE6);
    x.die();
}

// --- Entradas: pedais, volante e câmbio ------------------------------------------------------

/// `0x2E57`: R0 = acelerador 0..1 (`[C93B]` entre `[E89A]`+5 e `[E89B]`).
void gasPedal(Ctx& x) {
    double r0 = static_cast<double>(s(x.rd(0xC93B))) - x.rf(0xE89A);
    if (r0 < 5.0) r0 = 0.0;
    x.setR(0, r0);
    x.setR(1, x.rf(0xE89B) - x.rf(0xE89A));
    x.call(0xA509);
    if (x.R(0) > 1.0) x.setR(0, 1.0);
}

/// `0x2E31` (apaga) / `0x2E3B` (acende): luz de freio — copia 4 cores de `[2E2C]`/`[2E27]` para
/// a paleta do carro (`[961B]` + modelo·11: +1 textura, +5 deslocamento da cor). Preserva AR0, AR2, R2, R3.
void brakeLights(Ctx& x, bool on) {
    Tms32031State& cpu = x.cpu;
    const uint32_t ar0 = cpu.ar[0], ar2 = cpu.ar[2], r2 = cpu.r[2], r3 = cpu.r[3];
    const uint32_t lamps = x.rd(0xC8CD);
    x.wr(0xC8CD, on ? (lamps | 0x30u) : (lamps & ~0x30u));
    const uint32_t table = x.rd(on ? 0x2E27 : 0x2E2C);
    cpu.ar[0] = x.rd(0xE640) * 0xB + x.rd(0x961B);
    cpu.r[3] = x.rd(cpu.ar[0] + 5);
    if (cpu.r[3] != 0) {
        cpu.ar[2] = x.rd(cpu.ar[0] + 1);
        x.call(0x9ECD);
        cpu.r[0] = cpu.r[0] + 0x100 - cpu.r[3];
        cpu.r[2] = cpu.r[0];
        cpu.ar[2] = table;
        x.call(0x9F5C);
    }
    cpu.r[3] = r3;
    cpu.r[2] = r2;
    cpu.ar[2] = ar2;
    cpu.ar[0] = ar0;
}

/// `0x2E0C`: R0 = freio² (0..1; `[C93C]` acima de `[E89F]`+20) e acende/apaga a luz de freio.
void brakePedal(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    double r0 = static_cast<double>(s(x.rd(0xC93C))) - x.rf(0xE89F);
    if (r0 < 20.0) {
        x.setR(0, 0.0);
        const uint32_t keep = cpu.r[0];
        if (x.rd(0xE872) != 0) {
            brakeLights(x, false);
            x.wr(0xE872, 0);
        }
        cpu.r[0] = keep;
        return;
    }
    x.setR(0, r0);
    x.setR(1, x.rf(0xE8A0) - x.rf(0xE89F));
    x.call(0xA509);
    r0 = x.R(0);
    if (r0 > 1.0) r0 = 1.0;
    if (r0 < 0.0) r0 = 0.0;
    x.setR(0, r0 * r0);
    const uint32_t keep = cpu.r[0];
    if (x.rd(0xE872) == 0) {
        brakeLights(x, true);
        x.wr(0xE872, 1);
    }
    cpu.r[0] = keep;
}

/// `0x2E61`: R0 = marcha — câmbio manual (`[D+0x56]` ≠ 0) pelos bits de `[C937]`; automático
/// pela `0x2E73` depois da largada (antes dela mantém `[D+0x38]`).
void gearInput(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    if (x.rd(x.D(0x56)) == 0) {
        if (x.rd(0xE5EC) != 0x33BB) return x.call(0x2E73);
        cpu.r[0] = x.rd(x.D(0x38));
        return;
    }
    const uint32_t sh = x.rd(0xC937);
    cpu.r[1] = sh;
    cpu.r[0] = 0;
    if (sh & 0x400u) cpu.r[0] = 4;
    if (sh & 0x800u) cpu.r[0] = 3;
    if (sh & 0x1000u) cpu.r[0] = 2;
    if (sh & 0x2000u) cpu.r[0] = 1;
}

/// `0x2E88`: R0 = esterço pedido pelo volante `[C8C8]` (centro `[E89E]`, curso `[E8A1]`), com a
/// curva de sensibilidade do ajuste 0x26 e o ganho pela velocidade; `[D+0x21]` = −0,3·volante.
void steeringInput(Ctx& x) {
    const double sens = static_cast<double>(s(x.item(0x26)));
    const double r4 = (5.0 - sens) * x.K(0x2E8D) + 0.5;
    const double r5 = 1.0 - r4;
    x.setR(0, static_cast<double>(s(x.rd(0xC8C8))) - x.rf(0xE89E));
    x.setR(1, x.rf(0xE8A1) * 0.5);
    x.call(0xA509);
    double r0 = x.R(0);
    if (r0 < -1.0) r0 = -1.0;
    if (r0 > 1.0) r0 = 1.0;
    r0 *= sens * x.K(0x2E9A) + x.K(0x2E9B);
    const double sq = std::fabs(r0) * r0 * r4;
    r0 = r0 * r5 + sq;
    x.setD(0x21, r0 * x.K(0x2EA3));
    const double v = x.d(0x26);
    double g = v * v * x.K(0x2EA8);
    if (g > 5.0) g = 5.0;
    double l = v * 0.0625;
    if (l > 15.0) l = 15.0;
    r0 *= g + l;
    r0 *= x.fc92f();
    r0 *= x.rf(0x2E87);
    x.setR(0, r0);
}

// --- Estado do carro ---------------------------------------------------------------------------

/// `0x2CD3`: R0 = tolerância fora da estrada. Algum ponto no asfalto (0x30x) → `[E86A]` = 20;
/// algum ponto em 0x3xx/0x9xx → `[E86A]`; senão `[E86A]` se > 8, ou 0 (→ reposição).
void surfaceCheck(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    cpu.ar[3] = x.D(5);
    cpu.r[3] = 0;
    cpu.r[0] = x.rd(0xE86A);
    for (int i = 0; i < 5; ++i) {
        cpu.ar[0] = x.rd(cpu.ar[3]);
        cpu.ar[3] += 6;
        cpu.r[1] = x.rd(cpu.ar[0] + 0xF) & 0xFF0u;
        if (cpu.r[1] == 0x300) {
            cpu.r[0] = 0x14;
            x.wr(0xE86A, 0x14);
            return;
        }
        cpu.r[1] &= 0xF00u;
        if (cpu.r[1] == 0x300 || cpu.r[1] == 0x900) cpu.r[3] = 1;
    }
    if (cpu.r[3] == 0 && s(cpu.r[0]) <= 8) cpu.r[0] = 0;
}

/// `0x2BF5`: repõe o carro no nó mais próximo, alinhado à pista e 722 unidades para o lado.
void respawn(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    x.setD(0x32, 0.0);
    x.setD(0x2E, 0.0);
    x.wr(x.D(0x31), 0);
    x.wr(kSpin, 0);
    cpu.ar[0] = x.O(4);
    x.call(0x93D7);
    x.call(0x2EFD);
    x.wr(x.O(1), x.rd(cpu.ar[0] + 1));
    x.wr(x.O(3), x.rd(cpu.ar[0] + 3));
    x.wr(x.O(2), x.rd(cpu.ar[0] + 2));
    x.call(0x3037);
    x.wr(x.D(0x2C), cpu.r[0]);
    x.wr(x.D(0x2D), cpu.r[0]);
    cpu.r[2] = cpu.r[0];
    x.setR(0, -x.R(0));
    x.wr(kCamYaw, cpu.r[0]);
    cpu.dp = 0;
    x.call(0x92E2);
    double r3 = x.R(0);
    x.call(0x92DF);
    const double r1 = 722.0;
    x.setR(1, r1);
    const double r0 = x.R(0) * r1;
    r3 *= r1;
    x.wf(x.O(1), r0 + x.rf(x.O(1)));
    x.wf(x.O(3), r3 + x.rf(x.O(3)));
    x.setR(0, r0);
    x.setR(3, r3);
}

/// `0x2D4A`: inclinação lateral da carroceria `[D+0x2F]` → alvo −0,5·`[D+0x21]` (ou ela mesma no
/// ar), em passos de velocidade·0,005 (velocidade até 10); zero em rodopio. R1 = resultado.
void bodyRoll(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    const uint32_t r3 = cpu.r[3];
    const double r0 = x.d(0x2F);
    double r1 = x.d(0x21) * x.K(0x2D4D);
    if (x.rd(x.D(0x1F)) != 0) r1 = r0;
    double r2 = x.d(0x27);
    if (r2 > 10.0) r2 = 10.0;
    r2 = r2 * x.K(0x2D53) * x.K(0x2D54);
    if (std::fabs(r0 - r1) > r2) {
        if (!(r0 - r1 < 0.0)) r2 = -r2;
        r1 = r0 + r2;
    }
    cpu.r[0] = x.rd(kSpin);
    if (cpu.r[0] != 0) r1 = 0.0;
    x.setD(0x2F, r1);
    x.setR(1, r1);
    x.setR(2, r2);
    cpu.r[3] = r3;
}

/// `0x2F17`: sem rodopio e com velocidade ≥ 2, desvio ≥ 1,75 rad entre o rumo desejado e a pista
/// inicia um rodopio de 1 volta (±0,12 rad por quadro).
void spinCheck(Ctx& x) {
    if (x.rd(x.D(0x31)) != 0) return;
    if (x.d(0x26) < 2.0) return;
    x.call(0x3037);
    const double r2 = x.wrap(x.R(0) - x.d(0x2D));
    if (std::fabs(r2) < x.K(0x2F21)) return;
    x.setD(0x32, 1.0);
    x.wf(x.D(0x2E), r2 < 0.0 ? x.K(0x2F27) : x.K(0x2F25));
    x.wr(x.D(0x31), 1);
}

/// `0x304A`: faixas sonoras (chão 0x310) nos pontos 1/2 — tranco do volante `[EA48]` = centro ±15
/// e, enquanto pisa nelas, tremor aleatório ±2 (±4 nas duas) em volta do centro.
void rumbleStrips(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    double r4 = x.rf(0xE89E);
    int32_t r3 = s(x.rd(0xE898));
    const uint32_t r5 = x.rd(0xE899);
    uint32_t r1 = 0, r2 = 0;
    auto strip = [&](uint32_t off) {
        const uint32_t g = x.rd(x.D(off));
        return g != 0 && (x.rd(g + 0xF) & 0xFF0u) == 0x310u;
    };
    if (strip(0xB)) r1 = 1;
    if (strip(0x11)) r2 = 2;
    r2 += r1;
    x.wr(0xE899, r2);
    bool store = true;
    if (r2 != 0 && (r2 & ~r5) != 0) {
        x.call(0x3037);
        const double a = x.R(0);
        x.call(0x3031);
        r4 += x.wrap(a - x.R(0)) >= 0.0 ? 15.0 : -15.0;
        r3 = 4;
    } else {
        --r3;
        if (r3 > 0) store = false;
        else if (r3 < 0) r3 = 0;
    }
    if (store) x.wf(0xEA48, r4);
    x.wr(0xE898, static_cast<uint32_t>(r3));
    cpu.r[1] = x.rd(0xE899);
    if (cpu.r[1] == 0 || x.rd(0xE898) != 0) return;
    const double centre = x.rf(0xE89E);
    x.setR(0, cpu.r[1] == 3 ? 4.0 : 2.0);
    x.call(0x8ED1);
    double r0 = x.R(0) + x.rf(0xEA48);
    if (r0 > centre + 15.0) r0 = centre + 15.0;
    if (r0 < centre - 15.0) r0 = centre - 15.0;
    x.wf(0xEA48, r0);
}

/// `0x2C4C`: som do motor — rotação `[D+0x39]` (pelo pedal com folga de ±2·`[C92F]` em ponto
/// morto/no ar/antes da largada; senão velocidade × relação da marcha) e volume pelo acelerador.
void engineSound(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    const uint32_t mode = x.rd(0xC8C5);
    double r0;
    if ((mode & 0x200u) != 0 && x.rd(x.D(0x38)) != 0 && x.rd(x.D(0x20)) == 0) {
        cpu.ar[0] = x.rd(0x2C46) + x.rd(x.D(0x38));
        r0 = x.d(0x26) * x.rf(cpu.ar[0]);
    } else {
        x.call(0x2E57);
        r0 = x.R(0) * 47.0;
        double r3 = x.fc92f() * 2.0;
        double r1 = x.d(0x39) + r3;
        const double hi = r1;
        r3 *= 2.0;
        r1 -= r3;
        if (r0 > hi) r0 = hi;
        if (r0 < r1) r0 = r1;
    }
    const int32_t oldRpm = s(tmsFix(x.d(0x39)));
    x.setD(0x39, r0);
    int32_t ar0 = s(tmsFix(r0));
    double vol = x.d(0x24) * x.K(0x2C69) + x.K(0x2C6A);
    double r2 = 1.0 - x.d(0x26) * x.K(0x2C6C) * x.K(0x2C6D);
    r2 = r2 * 0.75 + 0.25;
    if (r2 > 1.0) r2 = 1.0;
    if (r2 < 0.0) r2 = 0.0;
    vol = vol * r2 * 225.0;
    vol *= x.d(0x39) * x.K(0x2C79) * 0.5 + 0.5;
    const int32_t v = s(tmsFix(vol));
    if (ar0 == oldRpm && std::abs(s(x.rd(0xE897)) - v) < 4) {
        cpu.r[0] = static_cast<uint32_t>(v);
        return;
    }
    uint32_t r1 = static_cast<uint32_t>(v);
    x.wr(0xE897, r1);
    if (ar0 < 0) ar0 = 0;
    ar0 *= 5;
    cpu.ar[0] = static_cast<uint32_t>(ar0);
    cpu.r[0] = static_cast<uint32_t>(ar0 > 0xFD ? 0xFD : ar0);
    if ((x.rd(0xC8C5) & 0x10u) != 0) r1 >>= 4;
    cpu.r[1] = r1;
    x.call(0x91D0);
}

/// `0x3143`: toca AR2 se não estiver tocando nos canais contínuos `[E8FF]`/`[E908]`.
void playIfIdle(Ctx& x) {
    const uint32_t id = x.cpu.ar[2];
    if (x.rd(0xE8FF) == id) return;
    if (x.rd(0xE908) != id) x.call(0x9188);
}

/// `0x3148`: som contínuo AR2 com volume R0: toca (`0x9186`) ou só ajusta o volume do canal
/// 1/2 (`0x9114`) quando mudou mais de 8.
void playOrVolume(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    const uint32_t id = cpu.ar[2];
    const int32_t vol = s(cpu.r[0]);
    cpu.r[1] = cpu.r[0];
    uint32_t chan = 0, cur = 0;
    if (x.rd(0xE8FF) == id) {
        chan = 1;
        cur = x.rd(0xE8FE);
    } else if (x.rd(0xE908) != id) {
        return x.call(0x9186);
    } else {
        chan = 2;
        cur = x.rd(0xE907);
    }
    cpu.r[0] = static_cast<uint32_t>(std::abs(vol - s(cur)));
    if (s(cpu.r[0]) <= 8) return;
    cpu.r[0] = chan;
    x.call(0x9114);
}

/// `0x3097`: sons do carro do jogador no modo 4 (corrida): arrancada, buzina de largada, salto,
/// pouso, derrapagem (0x1BF/0x1C2 com volume), freio (0x1C5), ponto morto (0x21F), vento (0x1CB)
/// e cascalho (0x1CE).
void raceSounds(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    if ((x.rd(0xC8C5) & 0xFu) != 4) return;
    x.call(0x2E57);
    if (!(x.R(0) < 0.75)) {
        if ((x.rd(0xC8C5) & 0x10u) != 0) x.spawn(0xA868, 0xC620);
        if (x.rd(0xE874) == 0) {
            x.spawn(0xA868, 0xC621);
            cpu.r[0] = 2;
            cpu.ar[2] = x.rd(0x3094);
            x.call(0x3159);
            x.wr(0xE874, 1);
        }
    } else {
        x.wr(0xE874, x.R(0) >= 0.5 ? x.rd(0xE874) : 0u);
    }
    // 030B2
    if ((x.rd(0xC8C5) & 0x10u) != 0 && (x.rd(0xE8F9) == 0 || x.rd(0xE902) == 0)) x.sound(0x90);
    if (x.rd(kSpin) == 0) {
        uint32_t c = x.rd(0xE87C);
        bool store = true;
        if (c == 0) {
            if (x.d(3) < 200.0) {
                store = false;
            } else {
                cpu.ar[2] = x.rd(0x3090);
                cpu.r[0] = 3;
                x.call(0x3159);
                c = 0x64;
            }
        }
        if (store) x.wr(0xE87C, c - 1);
    }
    // 030C8
    const uint32_t air = x.rd(x.D(0x20));
    if (air != 0) {
        x.wr(0xE87A, air);
    } else if (x.rd(0xE87A) != 0) {
        x.call(0x87D7);
        x.sound(0x1EF);
        x.wr(0xE87A, 0);
    }
    // 030D1
    if (!(x.d(0x25) < 0.5)) x.spawn(0xA868, 0xC622);
    const double slip = x.d(0x25);
    if (slip < 0.25) {
        cpu.ar[2] = 0x1BF;
        x.call(0x91C1);
        cpu.ar[2] = 0x1C2;
        x.call(0x91C1);
    } else {
        cpu.r[1] = static_cast<uint32_t>(s(tmsFix(slip * 115.0)) + 0x8C);
        const uint32_t c1 = x.rd(0xE8FF), c2 = x.rd(0xE908);
        uint32_t cur = 0;
        if (c1 == 0x1BF || c1 == 0x1C2) {
            cur = x.rd(0xE8FE);
            cpu.r[0] = 1;
        } else if (c2 == 0x1BF || c2 == 0x1C2) {
            cur = x.rd(0xE907);
            cpu.r[0] = 2;
        } else {
            if (!(x.d(0x26) > 20.0)) x.spawn(0xA868, 0xC623);
            cpu.ar[2] = x.rd(0x308D);
            cpu.r[0] = 2;
            x.call(0x3160);
            cur = cpu.r[1];
            cpu.r[0] = 0;
        }
        if (cpu.r[0] != 0 && std::abs(s(cur) - s(cpu.r[1])) >= 10) x.call(0x9114);
    }
    // 03101
    if (x.d(0x2B) > x.K(0x3102) && !(x.d(0x26) < 1.5)) {
        x.spawn(0xA868, 0xC624);
        cpu.ar[2] = 0x1C5;
        playIfIdle(x);
    } else {
        cpu.ar[2] = 0x1C5;
        x.call(0x91C1);
    }
    // 0310F
    if (x.d(0x24) < x.K(0x3110) && x.d(0x39) > 30.0 && (x.rd(0xC8C5) & 0x10u) == 0) {
        cpu.ar[2] = 0x21F;
        if (x.rd(0xE908) != 0x21F) x.call(0x91E0);
    } else {
        cpu.ar[2] = 0x21F;
        x.call(0x91C1);
    }
    // 0311F
    cpu.r[4] = x.rd(0xC8C5);
    cpu.ar[2] = 0x1CB;
    if ((cpu.r[4] & 0x400u) != 0) {
        int32_t v = s(tmsFix(x.d(0x26)));
        if (v < 0x64) v = 0x64;
        if (v > 0xFF) v = 0xFF;
        cpu.r[0] = static_cast<uint32_t>(v);
        playOrVolume(x);
    } else {
        x.call(0x91C1);
    }
    // 0312C  cascalho: pontos 3 e 5 fora do asfalto
    uint32_t r1 = 0, r2 = 0;
    if (x.rd(x.rd(x.D(0x17)) + 0xF) != 0x300) r1 = 0x7F;
    if (x.rd(x.rd(x.D(0x1D)) + 0xF) != 0x300) r2 = 0x7F;
    cpu.r[0] = r1 + r2;
    cpu.ar[2] = 0x1CE;
    if (cpu.r[0] != 0 && !(x.d(0x26) < 1.0)) return playOrVolume(x);
    x.call(0x91C1);
    if (cpu.carry()) x.sound(0x1EF);
}

// --- Rodopio ---------------------------------------------------------------------------------

/// `0x25D6`: matriz do objeto = rotY(`*+AR7(0x10)`) × rotX(`*+AR7(0xF)`) × rotZ(`*+AR7(0x11)`).
void spinMatrix(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    cpu.r[2] = x.var(0x11);
    cpu.ar[2] = x.O(4);
    x.call(0x93C4);
    cpu.ar[2] = x.rd(0x5A);
    cpu.r[2] = x.var(0xF);
    x.call(0x9377);
    x.mulInto(x.O(4), x.O(4));
    cpu.ar[2] = x.rd(0x5A);
    cpu.r[2] = x.var(0x10);
    x.call(0x938A);
    x.mulInto(x.O(4), x.O(4));
}

/// `0x5567`: um quadro do rodopio — ângulos `*+AR7(0xF..0x11)` += taxas `*+AR7(0xC..0xE)`·dt,
/// velocidade no rumo `[D+0x2D]`, chão, salto `[D+3]` e fim quando `[obj+0x12]` passa de 60.
void spinStep(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    x.call(0x2EFD);
    const double dt = x.fc92f();
    for (uint32_t i = 0; i < 3; ++i) x.wf(cpu.ar[7] + 0xF + i, x.rf(cpu.ar[7] + 0xC + i) * dt + x.rf(cpu.ar[7] + 0xF + i));
    x.call(0x25D6);
    x.setR(2, x.d(0x2D) + x.K(0x5577));
    x.call(0x92E2);
    const double v = x.d(0x26);
    x.wf(x.O(0x13), x.R(0) * v);
    x.call(0x92DF);
    x.wf(x.O(0x11), x.R(0) * v);
    x.setR(3, v);
    x.call(0x908B);
    cpu.r[3] = x.rd(x.O(0x1B));
    x.call(0x2041);
    cpu.ar[5] = x.rd(x.O(0x1B));
    if (x.rd(x.D(5)) != 0) {
        const double h = x.d(3) + x.rf(x.O(2));
        x.wf(x.O(2), h - x.rf(cpu.ar[7] + 0x20) + x.rf(x.O(2)));
        x.wf(cpu.ar[7] + 0x20, h);
    }
    const double t = x.fc92f() * 2.0 + x.rf(x.O(0x12));
    x.wf(x.O(0x12), t);
    if (t < 60.0) return;
    x.wr(kSpin, 0);
    x.wr(x.D(0x41), 1);
    x.sound(0x1EF);
    if (x.var(0x21) == 0) x.spawn(0xA844, 0xC61B);
}

// --- Link: vácuo e potência (só com `[C8E3]` ≠ 0) --------------------------------------------

/// Flags de `0x5268` (R1 − R0 das voltas do carro `[E592]` e do focado).
int32_t lapCompare(Ctx& x) {
    x.call(0x5268);
    return s(x.cpu.r[1]) - s(x.cpu.r[0]);
}

/// `0x2B61`: `[E87E]` (vácuo) pela distância até o carro `[E592]` e a diferença de voltas;
/// `[E87D]` (potência extra) por `[E87E]` e a velocidade.
void linkDraft(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    double r2 = x.fc92f();
    int32_t cmp = lapCompare(x);
    if (cmp == 0) {
        x.wf(0xE87D, 1.0);
        return;
    }
    double r1 = x.rf(cmp > 0 ? 0x2B90 : 0x2B8F) * r2 + x.rf(0xE87E);
    cpu.ar[0] = x.rd(0xE592);
    const double dz = x.rf(cpu.ar[0] + 3) - x.rf(x.O(3));
    const double dx = x.rf(x.O(1)) - x.rf(cpu.ar[0] + 1);  // SUBF3 *+AR4(1),*+AR0(1),R3
    cpu.r[3] = doubleToTms(dx * dx);  // ROM: R3 (0/1 de antes) é sobrescrito aqui
    x.setR(2, dz * dz + tmsToDouble(cpu.r[3]));
    x.call(0xA5F1);
    double r0 = x.R(0) * x.rf(0x2B91);
    if (r0 > x.K(0x2B74)) r0 = x.K(0x2B75);
    r0 += 1.0;
    if (cpu.r[3] == 1 && r1 < r0) r1 = r0;  // quirk: compara os bits do float dx²
    r0 += x.K(0x2B7B);
    if (r1 > r0) r1 = r0;
    if (r1 < 1.0) r1 = 1.0;
    x.wf(0xE87E, r1);
    r2 = 300.0 - x.d(0x26);
    if (r2 < 0.0) r2 = 0.0;
    r2 *= x.rf(0x2B92);
    r0 = (r0 - x.K(0x2B85)) * 10.0;
    r2 = r2 * r0 + 1.0;
    if (r2 > 2.0) r2 = 2.0;
    if (lapCompare(x) >= 0) r2 = 1.0;
    x.wf(0xE87D, r2);
}

// --- Fim de jogo ------------------------------------------------------------------------------

/// `0x1D59`: abandono da partida (`[E635]`): ajusta os itens 0xF7/0x75, mata os processos 0x2xx
/// e 0x8xx, espera o DMA e a troca de página e reinicia o jogo (`0x1E17`).
void abortRace(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    cpu.r[2] = 0;
    cpu.ar[2] = 0xF7;
    x.call(0x99F4);
    cpu.r[2] = x.item(0x75) - 1;
    cpu.ar[2] = 0x75;
    x.call(0x99F4);
    x.wr(0xC931, 0);
    cpu.ar[2] = 0xFFFFFFFEu;
    x.wr(0xC8E8, cpu.ar[2]);
    cpu.r[0] = 0x200;
    cpu.r[1] = 0xF00;
    x.call(0xA8CF);
    cpu.r[0] = 0x800;
    cpu.r[1] = 0xF00;
    x.call(0xA8CF);
    x.waitDma(1);
    x.waitDma(2);
    x.wr(0xC93F, 1);
    x.sleep(1, 0x1D78);
}

/// `0x1D78`: espera a INT0 atender `[C93F]` (port: 1 quadro por vez) e reinicia.
void abortRaceEnd(Ctx& x) {
    Tms32031State& cpu = x.cpu;
    if (x.rd(0xC93F) != 0) return x.sleep(1, 0x1D78);
    x.call(0x7648);
    cpu.r[0] = 0;
    cpu.r[1] = 0;
    x.call(0xA8CF);
    x.call(0x1E17);
}

} // namespace

void registerPlayerCar(SubroutineDispatcher& d) {
    addCtx(d, 0x29B8, startFromGarage);
    addCtx(d, 0x29EE, startAttract);
    addCtx(d, 0x2A5C, loopBody);
    addCtx(d, 0x2B60, loopBody);  // BU $2A5C depois do sleep
    addCtx(d, 0x2976, [](Ctx& x) { modeFiveWait(x, true); });
    addCtx(d, 0x297D, [](Ctx& x) { modeFiveWait(x, false); });
    addCtx(d, 0x2982, [](Ctx& x) {
        if ((x.rd(0xC8C5) & 0xFu) != 4) return modeFiveWait(x, false);
        x.jump(0x29EE);
    });
    addCtx(d, 0x2B61, linkDraft);
    addCtx(d, 0x2B93, [](Ctx& x) { sideProbe(x, -170.0); });
    addCtx(d, 0x2B95, [](Ctx& x) { sideProbe(x, 170.0); });
    addCtx(d, 0x2BAF, sideProbes);
    addCtx(d, 0x2BB7, [](Ctx& x) {
        cameraBlocked(x);
        x.cpu.setFlag(Tms32031State::kStZero, x.cpu.r[3] == 0);
    });
    addCtx(d, 0x2BBD, cameraSlide);
    addCtx(d, 0x2BCF, cameraPointFor);
    // 02BE5  câmera abaixo do chão (AR0): sobe até 133 acima dele (só se estiver entre 0 e 500 abaixo)
    addCtx(d, 0x2BE5, [](Ctx& x) {
        Tms32031State& cpu = x.cpu;
        const uint32_t ar4 = cpu.ar[4];
        cpu.ar[4] = cpu.ar[0];
        x.call(0x1F8B);
        cpu.ar[0] = cpu.ar[4];
        cpu.ar[4] = ar4;
        if (!cpu.carry()) return;
        const double r1 = 133.0 - x.R(0);
        x.setR(1, r1);
        if (r1 <= 0.0) return;
        x.setR(0, 500.0);
        if (r1 > 500.0) return;
        x.wf(cpu.ar[0] + 1, x.rf(cpu.ar[0] + 1) - r1);
    });
    addCtx(d, 0x2BF5, respawn);
    addCtx(d, 0x2C4C, engineSound);
    addCtx(d, 0x2CD3, surfaceCheck);
    addCtx(d, 0x2D4A, bodyRoll);
    addCtx(d, 0x2E0C, brakePedal);
    addCtx(d, 0x2E31, [](Ctx& x) { brakeLights(x, false); });
    addCtx(d, 0x2E3B, [](Ctx& x) { brakeLights(x, true); });
    addCtx(d, 0x2E57, gasPedal);
    addCtx(d, 0x2E61, gearInput);
    addCtx(d, 0x2E88, steeringInput);
    addCtx(d, 0x2EB4, [](Ctx& x) { viewButton(x, 0.0, -150.0, 0, 2); });
    addCtx(d, 0x2EB9, [](Ctx& x) { viewButton(x, 2200.0, -400.0, 1, 4); });
    addCtx(d, 0x2EBE, [](Ctx& x) { viewButton(x, 3840.0, -700.0, 2, 8); });
    addCtx(d, 0x2EE6, cameraZoom);
    addCtx(d, 0x2F17, spinCheck);
    addCtx(d, 0x304A, rumbleStrips);
    addCtx(d, 0x3097, raceSounds);
    addCtx(d, 0x3143, playIfIdle);
    addCtx(d, 0x3148, playOrVolume);
    addCtx(d, 0x25D6, spinMatrix);
    addCtx(d, 0x5567, spinStep);
    // 03C71  tempo esgotado: [E53F] = 1, auditoria 0x97 e rotina da pista [[396F] + [E481]]
    addCtx(d, 0x3C71, [](Ctx& x) {
        x.wr(0xE53F, 1);
        x.audit(0x97);
        x.cpu.ar[2] = x.rd(0xE481) + x.rd(0x396F);
        x.cpu.r[0] = x.rd(x.cpu.ar[2]);
        x.call(x.cpu.r[0]);
    });
    // 0472A  plataforma de movimento: só com o DIP de movimento (0x4693 com Z = 1); 0x472C em service/service_motion.cpp
    addCtx(d, 0x472A, [](Ctx& x) {
        x.call(0x4693);
        if (!x.cpu.zero()) return;
        x.call(0x472C);
    });
    addCtx(d, 0x1D59, abortRace);
    addCtx(d, 0x1D78, abortRaceEnd);
}

} // namespace rea::core::game
