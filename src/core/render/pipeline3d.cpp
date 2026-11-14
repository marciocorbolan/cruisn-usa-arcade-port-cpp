/**
 * @file pipeline3d.cpp
 * @brief Tradução de `0x0064`–`0x05F4` (pipeline 3D).
 */
#include "core/render/pipeline3d.hpp"

#include <array>
#include <initializer_list>

#include "core/math/matrix.hpp"
#include "core/tms_float.hpp"

namespace rea::core::render {

using namespace p3d;

namespace {

/// Reinterpreta a palavra de 32 bits como inteiro com sinal (comparações com sinal do C31).
inline int32_t s32(uint32_t v) { return static_cast<int32_t>(v); }
constexpr double kYScale = 1.0400390625;  ///< MPYF 1.040039 (imediato curto 0x0052)

/// Estado de uma execução do pipeline (equivale aos registradores do original).
class Pipe {
public:
    /// @param bus Barramento de memória. @param stats Contadores atualizados durante a execução.
    Pipe(MemoryBus& bus, Pipeline3dStats& stats) : bus_(bus), stats_(stats) {}

    void list(uint32_t head);
    void subdivide(uint32_t clip, uint32_t& model, uint32_t palette, uint32_t header);

private:
    /// Escolhe a base de paleta de um polígono a partir do cabeçalho.
    using PalFn = uint32_t (Pipe::*)(uint32_t header) const;

    MemoryBus& bus_;  ///< Barramento de memória simulado.
    Pipeline3dStats& stats_;  ///< Contadores do pipeline.
    uint32_t obj_ = 0;  ///< AR0 nos caminhos de polígonos.

    /// Lê a palavra no endereço @p a.
    uint32_t rd(uint32_t a) const { return bus_.readWord(a); }
    /// Grava a palavra @p v no endereço @p a.
    void wr(uint32_t a, uint32_t v) { bus_.writeWord(a, v); }
    /// Lê o float TMS em @p a e o converte para double.
    double rdF(uint32_t a) const { return tmsToDouble(rd(a)); }
    /// Converte @p v para float TMS e grava em @p a.
    void wrF(uint32_t a, double v) { wr(a, doubleToTms(v)); }
    /// `FIX` do float TMS em @p a (inteiro com sinal).
    int32_t fixAt(uint32_t a) const { return s32(tmsFix(rdF(a))); }

    /// Índice da tabela de perspectiva: `FIX(Z) >> 4` limitado a [−0x50, 0x1387].
    static uint32_t perspIndex(double z) {
        int32_t i = s32(tmsFix(z)) >> 4;
        if (i >= 0x1387) i = 0x1387;
        if (i < -0x50) i = -0x50;
        return static_cast<uint32_t>(i);
    }

    /// Espera a fila de quads esvaziar (`[0x980082] & 4`); depois de 1 000 000 voltas conta uma trava em `stats_.hangs`.
    void waitFifo() {
        for (uint32_t n = 0; rd(kFifoStatus) & 4u; ++n) {
            if (n > 1000000) {
                ++stats_.hangs;
                return;
            }
        }
    }
    /// Envia uma palavra à fila de DMA de quads (`0x600000`).
    void push(uint32_t v) { wr(kFifo, v); }

    void object(uint32_t obj);
    uint32_t verticesStd(uint32_t& ar1, uint32_t mat);
    uint32_t verticesFlat(uint32_t& ar1, uint32_t mat2);
    void articulated(uint32_t obj, uint32_t ar1);
    bool specialBone(uint32_t bone, uint32_t& ar1, uint32_t& vb);
    void normalBone(uint32_t bone, uint32_t& ar1, uint32_t& vb);

    void polygons(uint32_t obj, uint32_t& ar1, uint32_t bk);
    /// Base de paleta pela tabela `[0x62]`: slot do id nos 16 bits altos do cabeçalho, deslocado 8 bits.
    uint32_t tablePalette(uint32_t header) const {
        // LSH3 R2,R6 (id = cabeçalho >> 16); LSH3 R6,*AR6 (slot = [[0x62] + id] >> 16); LSH 8
        return (rd(rd(kPalTablePtr) + (header >> 16)) >> 16) << 8;
    }
    /// Base de paleta fixa do objeto (`+0x10`, flag 0x400).
    uint32_t fixedPalette(uint32_t) const { return rd(obj_ + 0x10); }

    bool backface(uint32_t vb, uint32_t i4, uint32_t i5, uint32_t i2) const;
    bool twisted(uint32_t vb, uint32_t i4, uint32_t i5, uint32_t i2, uint32_t i3) const;
    void emit(uint32_t header, uint32_t palette, const std::array<uint32_t, 8>& xy, uint32_t& ar1);
    void polysFast(uint32_t& ar1, uint32_t bk, PalFn pal, bool twist);
    void polysClip(uint32_t& ar1, uint32_t bk, PalFn pal);
    void polysLit(uint32_t& ar1, uint32_t bk);
};

// ---------------------------------------------------------------------------
/// 00166  laço da lista: R0 = *AR0; BNE 0091
void Pipe::list(uint32_t head) {
    for (uint32_t obj = rd(head); obj != 0; obj = rd(obj)) object(obj);
}

/// 00091  um objeto
void Pipe::object(uint32_t obj) {
    ++stats_.objects;
    // 00093  depuração: [C92E] == objeto → BEQ $96 (laço infinito)
    const uint32_t dbg = rd(kDebugObj);
    if (dbg != 0 && dbg == obj) ++stats_.hangs;
    uint32_t flags = rd(obj + 0xE);  // R6
    uint32_t ar1 = rd(obj + 0xD);    // modelo
    const uint32_t view = rd(kViewPtr);
    // 0009A  flag 0x20: posição já relativa; senão objeto − câmera
    double rx = rdF(obj + 1), ry = rdF(obj + 2), rz = rdF(obj + 3);
    if (!(flags & 0x20u)) {
        const uint32_t cam = rd(kCamPosPtr);
        rx -= rdF(cam);
        ry -= rdF(cam + 1);
        rz -= rdF(cam + 2);
    }
    // 000A6  flag 1: já no espaço da câmera; senão gira pela matriz [0x47]
    double z;
    if (flags & 1u) {
        wrF(view - 1, rx);
        wrF(view, ry);
        z = rz;
    } else {
        const uint32_t m = rd(kCamMatPtr), tmp = rd(kTempAPtr);
        wrF(tmp, rx);
        wrF(tmp + 1, rz);
        wrF(view - 1, rx * rdF(m) + ry * rdF(m + 1) + rz * rdF(m + 2));
        wrF(view, rx * rdF(m + 3) + ry * rdF(m + 4) + rz * rdF(m + 5));
        z = rx * rdF(m + 6) + ry * rdF(m + 7) + rz * rdF(m + 8);
    }
    // 000BC  [view+1] = Z; profundidade = FIX(Z); LOD
    wrF(view + 1, z);
    const int32_t depth = s32(tmsFix(z));
    wr(obj + 0x1C, static_cast<uint32_t>(depth));
    if (flags & 0x200u) {
        if (depth > 0x1F40) ar1 = rd(obj + 0x18);
        if ((flags & 4u) && depth > 0x3A98) ar1 = rd(obj + 0x19);
    }
    // 000C5  esfera: raio = 1ª palavra do modelo
    const uint32_t radius = rd(ar1++);
    if (s32(static_cast<uint32_t>(depth) + radius - 100u) < 0) { ++stats_.culled; return; }
    if (s32(static_cast<uint32_t>(depth) - radius - rd(kFarLimit)) > 0) { ++stats_.culled; return; }
    uint32_t idx = depth < 0 ? 0u : static_cast<uint32_t>(depth);
    idx >>= 4;
    if (s32(idx) > 0x1387) idx = 0x1387;
    const double t = rdF(rd(kPerspPtr) + idx);
    const double rt = static_cast<double>(s32(radius)) * t;
    const double hh = rdF(kHalfHeight), hw = rdF(kHalfWidth);
    double c = t * rdF(view);
    if (c + rt + hh < 0.0 || (c - rt) - hh > 0.0) { ++stats_.culled; return; }
    c = t * rdF(view - 1);
    if (c + rt + hw < 0.0 || (c - rt) - hw > 0.0) { ++stats_.culled; return; }

    // 000E9  modo 4 (só giro em Y): flags sem 0x800/0x40 e bit 27 = 0
    flags = rd(obj + 0xE);
    if (!(flags & 0x800u) && !(flags & 0x40u) && !(flags & (1u << 27)) &&
        (rd(kGameMode) & 0xFu) == 4 && rd(kMode4Enable) != 0) {
        uint32_t mat2;
        if (flags & 8u) {
            // 0016C  billboard: só com Z ≥ 300
            if (!(300.0 - rdF(view + 1) <= 0.0)) return;
            mat2 = rd(kYaw2x2Ptr);
        } else {
            // 00175  2×2 = elementos (0,2,6,8) de câmera × objeto
            const uint32_t a = obj + 4, m = rd(kCamMatPtr), out = rd(kComposedPtr);
            wrF(out + 0, rdF(a) * rdF(m) + rdF(a + 2) * rdF(m + 6));
            wrF(out + 1, rdF(a) * rdF(m + 2) + rdF(a + 2) * rdF(m + 8));
            wrF(out + 2, rdF(a + 6) * rdF(m) + rdF(a + 8) * rdF(m + 6));
            wrF(out + 3, rdF(a + 6) * rdF(m + 2) + rdF(a + 8) * rdF(m + 8));
            mat2 = out;
        }
        const uint32_t bk = verticesFlat(ar1, mat2);
        polygons(obj, ar1, bk);
        return;
    }

    uint32_t mat;
    if ((flags & 1u) && !(flags & 0x40u)) {
        mat = obj + 4;                                   // 000FA
    } else if (flags & 8u) {
        if (!(300.0 - rdF(view + 1) <= 0.0)) return;     // 000FF
        mat = rd(kYawMatPtr);
    } else if (flags & 0x80u) {
        mat = (flags & 2u) ? obj + 4 : rd(kCamMatPtr);   // 0010C
    } else {
        // 00115  [0x4B] = câmera × objeto
        math::mul3x3(bus_, rd(kCamMatPtr), obj + 4, rd(kComposedPtr));
        if (rd(obj + 0xE) & 0x800u) {
            articulated(obj, ar1);
            return;
        }
        mat = rd(kComposedPtr);
    }
    const uint32_t bk = verticesStd(ar1, mat);
    polygons(obj, ar1, bk);
}

/// 00129  vértices: matriz 3×3 + vetor de visão + projeção. Devolve BK (polígonos − 1).
uint32_t Pipe::verticesStd(uint32_t& ar1, uint32_t mat) {
    const uint32_t word = rd(ar1++);
    const uint32_t count = (word & 0xFFu) + 1;
    uint32_t vb = rd(kVertexBufPtr);
    const uint32_t tmp = rd(kTempBPtr), table = rd(kPerspPtr), view = rd(kViewPtr);
    const double vx = rdF(view - 1), vy = rdF(view);
    double m[9];
    for (uint32_t i = 0; i < 9; ++i) m[i] = rdF(mat + i);
    for (uint32_t n = 0; n < count; ++n) {
        // 00141  x = 16 bits baixos, y = 16 bits altos (com sinal), z = palavra seguinte
        const uint32_t w = rd(ar1++);
        const double x = static_cast<int16_t>(w & 0xFFFFu);
        const double y = s32(w) >> 16;
        const double z = s32(rd(ar1++));
        wrF(tmp - 1, x);
        wrF(tmp, y);
        wrF(tmp + 1, z);
        wrF(vb, x * m[0] + y * m[1] + z * m[2]);
        const double yy = x * m[3] + y * m[4] + z * m[5];
        const double zz = x * m[6] + y * m[7] + z * m[8] + rdF(view + 1);
        wrF(vb + 2, zz);
        // 00155  T = tabela[clamp(FIX(Z) >> 4)]; X é relido da memória (LDF *-AR3(1))
        const double t = rdF(table + perspIndex(zz));
        wrF(vb, (rdF(vb) + vx) * t + 256.0);
        wrF(vb + 1, (yy + vy) * t * kYScale + 200.0);
        vb += 3;
    }
    return word >> 16;
}

/// 00188  vértices do modo 4: matriz 2×2 em XZ, Y só transladado
uint32_t Pipe::verticesFlat(uint32_t& ar1, uint32_t mat2) {
    const uint32_t word = rd(ar1++);
    const uint32_t count = (word & 0xFFu) + 1;
    uint32_t vb = rd(kVertexBufPtr);
    const uint32_t table = rd(kPerspPtr), view = rd(kViewPtr);
    const double m0 = rdF(mat2), m1 = rdF(mat2 + 1), m2 = rdF(mat2 + 2), m3 = rdF(mat2 + 3);
    for (uint32_t n = 0; n < count; ++n) {
        const uint32_t w = rd(ar1++);
        const double x = static_cast<int16_t>(w & 0xFFFFu);
        const double y = s32(w) >> 16;
        const double z = s32(rd(ar1++));
        const double xx = x * m0 + rdF(view - 1) + z * m1;
        wrF(vb, xx);
        const double zz = x * m2 + rdF(view + 1) + z * m3;
        wrF(vb + 2, zz);
        const double yy = y + rdF(view);
        const double t = rdF(table + perspIndex(zz));
        wrF(vb, xx * t + 256.0);
        wrF(vb + 1, yy * t * kYScale + rdF(kHalfHeight));
        vb += 3;
    }
    return word >> 16;
}

/// 001BC  objeto articulado (flag 0x800): cada osso tem matriz e pivô próprios
void Pipe::articulated(uint32_t obj, uint32_t ar1) {
    if (s32(rd(obj + 0x1C)) < 250) return;
    // 001BF  LOD reduzido: desenha como objeto comum
    if (ar1 == rd(obj + 0x18) + 1 || ar1 == rd(obj + 0x19) + 1) {
        const uint32_t bk = verticesStd(ar1, rd(kComposedPtr));
        polygons(obj, ar1, bk);
        return;
    }
    uint32_t bone = rd(obj + 0x1A);
    uint32_t vb = rd(kVertexBufPtr);
    const uint32_t bk = rd(ar1++);
    do {
        if (s32(rd(bone + 0x12)) >= 0 || !specialBone(bone, ar1, vb)) normalBone(bone, ar1, vb);
        bone = rd(bone);
    } while (bone != 0);
    // 0024B  BK >>= 16; CALL 028D
    polygons(obj, ar1, bk >> 16);
}

/// 00250  osso especial (+0x12 < 0): 4 pontos de outro objeto, em coordenadas do mundo.
/// Devolve false quando cai no caminho normal (00259/0025C → 001D0).
bool Pipe::specialBone(uint32_t bone, uint32_t& ar1, uint32_t& vb) {
    uint32_t p = rd(rd(bone + 0x11) + 0x1B) + 6;
    if (rd(p + 0x3B) == 0) {
        // 00285  4 vértices em −1000 (fora da tela)
        for (uint32_t i = 0; i < 12; ++i) wrF(vb++, -1000.0);
        ar1 += 8;
        return true;
    }
    if ((rd(p + 0x19) | rd(p + 0x1A)) == 0) return false;
    if (rd(kGameMode) & 0x400u) return false;
    ar1 += 8;
    const uint32_t m = rd(kCamMatPtr), cam = rd(kCamPosPtr), org = rd(kBoneOrgPtr), table = rd(kPerspPtr);
    for (int k = 0; k < 4; ++k, p += 6) {
        const double rx = rdF(p) - rdF(cam);
        const double ry = rdF(p + 1) - rdF(cam + 1) + rdF(p + 3);
        const double rz = rdF(p + 2) - rdF(cam + 2);
        wrF(org - 1, rx);
        wrF(org, ry);
        wrF(org + 1, rz);
        wrF(vb, rx * rdF(m) + ry * rdF(m + 1) + rz * rdF(m + 2));
        wrF(vb + 1, rx * rdF(m + 3) + ry * rdF(m + 4) + rz * rdF(m + 5));
        const double zz = rx * rdF(m + 6) + ry * rdF(m + 7) + rz * rdF(m + 8);
        wrF(vb + 2, zz);
        const double t = rdF(table + perspIndex(zz));
        wrF(vb, rdF(vb) * t + 256.0);
        wrF(vb + 1, rdF(vb + 1) * t * kYScale + 200.0);
        vb += 3;
    }
    return true;
}

/// 001D0  osso normal: origem = [0x4B]·pos + visão; matriz = [0x4B] × osso; vértices relativos ao pivô
void Pipe::normalBone(uint32_t bone, uint32_t& ar1, uint32_t& vb) {
    const uint32_t m = rd(kComposedPtr), view = rd(kViewPtr), org = rd(kBoneOrgPtr), ta = rd(kTempAPtr);
    const double bx = rdF(bone + 4), by = rdF(bone + 5), bz = rdF(bone + 6);
    wrF(ta, bx);
    wrF(ta + 1, bz);
    wrF(org - 1, bx * rdF(m) + by * rdF(m + 1) + bz * rdF(m + 2) + rdF(view - 1));
    wrF(org, bx * rdF(m + 3) + by * rdF(m + 4) + bz * rdF(m + 5) + rdF(view));
    wrF(org + 1, bx * rdF(m + 6) + rdF(view + 1) + by * rdF(m + 7) + bz * rdF(m + 8));
    // 001E3  [0x5A] = [0x4B] × matriz do osso (+7)
    const uint32_t bm = rd(kBoneMatPtr);
    math::mul3x3(bus_, m, bone + 7, bm);
    double b[9];
    for (uint32_t i = 0; i < 9; ++i) b[i] = rdF(bm + i);
    const uint32_t tb = rd(kTempBPtr), table = rd(kPerspPtr);
    const double ox = rdF(org - 1), oy = rdF(org);
    const double px = rdF(bone + 1), py = rdF(bone + 2), pz = rdF(bone + 3);
    const uint32_t count = rd(bone + 0x10) + 1;
    for (uint32_t n = 0; n < count; ++n) {
        const uint32_t w = rd(ar1++);
        const double x = static_cast<double>(static_cast<int16_t>(w & 0xFFFFu)) - px;
        const double y = static_cast<double>(s32(w) >> 16) - py;
        const double z = static_cast<double>(s32(rd(ar1++))) - pz;
        wrF(tb - 1, x);
        wrF(tb, y);
        wrF(tb + 1, z);
        wrF(vb, x * b[0] + y * b[1] + z * b[2]);
        const double yy = x * b[3] + y * b[4] + z * b[5];
        const double zz = x * b[6] + y * b[7] + z * b[8] + rdF(org + 1);
        wrF(vb + 2, zz);
        const double t = rdF(table + perspIndex(zz));
        wrF(vb, (rdF(vb) + ox) * t + 256.0);
        wrF(vb + 1, (yy + oy) * t * kYScale + 200.0);
        vb += 3;
    }
}

// ---------------------------------------------------------------------------
/// 0028D  escolhe o caminho dos polígonos
void Pipe::polygons(uint32_t obj, uint32_t& ar1, uint32_t bk) {
    obj_ = obj;
    const uint32_t flags = rd(obj + 0xE);
    const uint32_t near = rd(obj + 0x1C) - rd(obj + 0x1D);  // profundidade − raio
    const bool far = s32(near - 1000u) > 0;
    if (flags & 0x400u) {
        // 00498  paleta fixa (+0x10)
        if (!far) {
            polysClip(ar1, bk, &Pipe::fixedPalette);
            return;
        }
        // 00506  ≥ 0x32 polígonos e perto: alguns tipos também descartam quads torcidos
        bool twist = false;
        if (s32(bk - 0x32u) >= 0 && !(s32(rd(obj + 0x1C) - 0x1F40u) > 0)) {
            const uint32_t type = rd(obj + 0xF);
            const uint32_t cls = type & 0xF00u;
            twist = type == 0x484u || (!(s32(cls - 0x200u) > 0) && s32(cls - 0x100u) >= 0);
        }
        polysFast(ar1, bk, &Pipe::fixedPalette, twist);
        return;
    }
    if (flags & 0x40u) {
        polysLit(ar1, bk);
        return;
    }
    if (far)
        polysFast(ar1, bk, &Pipe::tablePalette, false);  // 00453
    else
        polysClip(ar1, bk, &Pipe::tablePalette);         // 0029D
}

/// 0046D  face traseira: (y5 − y2)(x5 − x4) − (x5 − x2)(y5 − y4) > 0
bool Pipe::backface(uint32_t vb, uint32_t i4, uint32_t i5, uint32_t i2) const {
    const double x4 = rdF(vb + i4), y4 = rdF(vb + i4 + 1);
    const double x5 = rdF(vb + i5), y5 = rdF(vb + i5 + 1);
    const double x2 = rdF(vb + i2), y2 = rdF(vb + i2 + 1);
    return (y5 - y2) * (x5 - x4) - (x5 - x2) * (y5 - y4) > 0.0;
}

/// 002DF  quad "torcido": p1 = (y3 − y2)(y5 − y4) > 0 e p2 = (y2 − y5)(y4 − y3) ≥ 0.
/// O original faz OR inteiro das duas mantissas (sinal = OR dos sinais) e testa
/// com o expoente de p1 (zero → não descarta).
bool Pipe::twisted(uint32_t vb, uint32_t i4, uint32_t i5, uint32_t i2, uint32_t i3) const {
    if (i3 == i2) return false;  // triângulo
    const double y4 = rdF(vb + i4 + 1), y5 = rdF(vb + i5 + 1);
    const double y2 = rdF(vb + i2 + 1), y3 = rdF(vb + i3 + 1);
    const double p1 = (y3 - y2) * (y5 - y4);
    const double p2 = (y2 - y5) * (y4 - y3);
    return p1 > 0.0 && p2 >= 0.0;
}

/// 0047B  pacote de 15 palavras + disparo
void Pipe::emit(uint32_t header, uint32_t palette, const std::array<uint32_t, 8>& xy, uint32_t& ar1) {
    push(header);
    push(palette);
    for (uint32_t v : xy) push(v);
    const uint32_t a = rd(ar1++), b = rd(ar1++), c = rd(ar1++);
    push(a);
    push(a >> 16);
    push(b);
    push(b >> 16);
    push(c);
    (void)rd(kFifoTrigger);
    ++stats_.polys;
}

/// 00453 / 00515 / 00557  objeto longe: sem recorte
void Pipe::polysFast(uint32_t& ar1, uint32_t bk, PalFn pal, bool twist) {
    const uint32_t vb = rd(kVertexBufPtr);
    for (uint32_t n = 0; n <= bk; ++n) {
        const uint32_t idx = rd(ar1 + 1);
        const uint32_t i4 = (idx & 0xFFu) * 3, i5 = ((idx >> 8) & 0xFFu) * 3;
        const uint32_t i2 = ((idx >> 16) & 0xFFu) * 3, i3 = (idx >> 24) * 3;
        waitFifo();
        if (backface(vb, i4, i5, i2) || (twist && twisted(vb, i4, i5, i2, i3))) {
            ++stats_.backfaces;
            ar1 += 5;  // 00491  *++AR1(6); SUBI 1
            continue;
        }
        const uint32_t header = rd(ar1);
        ar1 += 2;
        std::array<uint32_t, 8> xy{};
        const uint32_t vs[4] = {i4, i5, i2, i3};
        for (int k = 0; k < 4; ++k) {
            xy[2 * k] = static_cast<uint32_t>(fixAt(vb + vs[k]));
            xy[2 * k + 1] = static_cast<uint32_t>(fixAt(vb + vs[k] + 1));
        }
        emit(header, (this->*pal)(header), xy, ar1);
    }
}

/// 0029D / 004C0  objeto perto: descarta atrás da câmera e subdivide quads enormes
void Pipe::polysClip(uint32_t& ar1, uint32_t bk, PalFn pal) {
    const uint32_t vb = rd(kVertexBufPtr), clip = rd(kClipBufPtr);
    for (uint32_t n = 0; n <= bk; ++n) {
        const uint32_t idx = rd(ar1 + 1);
        const uint32_t i4 = (idx & 0xFFu) * 3, i5 = ((idx >> 8) & 0xFFu) * 3;
        const uint32_t i2 = ((idx >> 16) & 0xFFu) * 3, i3 = (idx >> 24) * 3;
        bool cull = false;
        // 002CD  Z0 < 0 e os outros três também (bit 23 = sinal do float) → atrás
        if (rd(vb + i4 + 2) & 0x800000u) cull = (rd(vb + i2 + 2) & rd(vb + i5 + 2) & rd(vb + i3 + 2) & 0x800000u) != 0;
        if (!cull) {
            waitFifo();
            cull = backface(vb, i4, i5, i2) || twisted(vb, i4, i5, i2, i3);
        }
        if (cull) {
            ++stats_.backfaces;
            ar1 += 5;
            continue;
        }
        // 00309  FIX das 8 coordenadas no rascunho; grande = fora de ±1023 e caixa > 0x7FF
        std::array<uint32_t, 8> xy{};
        const uint32_t vs[4] = {i4, i5, i2, i3};
        uint32_t big = 0;
        for (int k = 0; k < 4; ++k) {
            xy[2 * k] = static_cast<uint32_t>(fixAt(vb + vs[k]));
            xy[2 * k + 1] = static_cast<uint32_t>(fixAt(vb + vs[k] + 1));
        }
        for (uint32_t k = 0; k < 8; ++k) {
            wr(clip + k, xy[k]);
            big |= s32(xy[k]) < 0 ? 0u - xy[k] : xy[k];  // ABSI
        }
        big >>= 10;
        if (big != 0) {
            uint32_t mx = xy[0], nx = xy[0], my = xy[1], ny = xy[1];
            for (int k = 1; k < 4; ++k) {
                const uint32_t x = xy[2 * k], y = xy[2 * k + 1];
                if (s32(mx - x) < 0) mx = x;
                if (s32(nx - x) > 0) nx = x;
                if (s32(my - y) < 0) my = y;
                if (s32(ny - y) > 0) ny = y;
            }
            big = (s32(mx - nx - 0x7FFu) > 0 || s32(my - ny - 0x7FFu) > 0) ? 1u : 0u;
        }
        const uint32_t header = rd(ar1);
        ar1 += 2;
        const uint32_t palette = (this->*pal)(header);
        if (big == 0) {
            emit(header, palette, xy, ar1);
        } else {
            ++stats_.subdivided;
            subdivide(clip, ar1, palette, header);
        }
    }
}

/// 0059E  polígonos lisos iluminados (flag 0x40): registro = cabeçalho, normal (3 floats), índices
void Pipe::polysLit(uint32_t& ar1, uint32_t bk) {
    const uint32_t vb = rd(kVertexBufPtr), m = rd(kComposedPtr), tb = rd(kTempBPtr), light = rd(kLightPtr);
    for (uint32_t n = 0; n <= bk; ++n) {
        const uint32_t idx = rd(ar1 + 4);
        const uint32_t i4 = (idx & 0xFFu) * 3, i5 = ((idx >> 8) & 0xFFu) * 3, i2 = ((idx >> 16) & 0xFFu) * 3;
        if (backface(vb, i4, i5, i2)) {
            ++stats_.backfaces;
            ar1 += 5;
            continue;
        }
        ++ar1;  // cabeçalho (lido em R7 e sobrescrito)
        const double nx = rdF(ar1), ny = rdF(ar1 + 1), nz = rdF(ar1 + 2);
        ar1 += 3;
        wrF(tb - 1, nx);
        wrF(tb, ny);
        wrF(tb + 1, nz);
        const double xn = nx * rdF(m) + ny * rdF(m + 1) + nz * rdF(m + 2);
        const double yn = nx * rdF(m + 3) + ny * rdF(m + 4) + nz * rdF(m + 5);
        const double zn = nx * rdF(m + 6) + ny * rdF(m + 7) + nz * rdF(m + 8);
        const double dot = zn * rdF(light + 1) + yn * rdF(light) + xn * rdF(light - 1);
        // 005D2  sombra = FIX(8 − 8·(N·L)); modo = 0x200 | sombra (OR, como no original)
        const uint32_t shade = tmsFix(dot * -8.0 + 8.0);
        waitFifo();
        push(0x200u | shade);
        push(0x200u);
        const uint32_t vi = rd(ar1++);
        for (uint32_t k = 0; k < 4; ++k) {
            const uint32_t v = ((vi >> (8 * k)) & 0xFFu) * 3;
            push(static_cast<uint32_t>(fixAt(vb + v)));
            push(static_cast<uint32_t>(fixAt(vb + v + 1)));
        }
        (void)rd(kFifoTrigger);
        ++stats_.polys;
    }
}

// ---------------------------------------------------------------------------
/// 0032F  subdivisão recursiva (pilha explícita de quads de 16 palavras)
void Pipe::subdivide(uint32_t clip, uint32_t& model, uint32_t palette, uint32_t header) {
    auto e = [&](uint32_t base, uint32_t k) { return rd(base + k); };
    auto rejected = [&](uint32_t b) {
        if (s32(e(b, 0) & e(b, 2) & e(b, 4) & e(b, 6)) < 0) return true;  // todos x < 0
        if (s32((0x1FFu - e(b, 0)) & (0x1FFu - e(b, 2)) & (0x1FFu - e(b, 4)) & (0x1FFu - e(b, 6))) < 0)
            return true;                                                   // todos x > 511
        // todos y > 399 (o 3º vértice compara com 0x1FF, como no original)
        if (s32((0x18Fu - e(b, 1)) & (0x18Fu - e(b, 3)) & (0x1FFu - e(b, 5)) & (0x18Fu - e(b, 7))) < 0)
            return true;
        return s32(e(b, 1) & e(b, 3) & e(b, 5) & e(b, 7)) < 0;            // todos y < 0
    };
    auto tooBig = [&](uint32_t b) {
        uint32_t mx = e(b, 0), nx = mx, my = e(b, 1), ny = my;
        for (uint32_t k = 1; k < 4; ++k) {
            const uint32_t x = e(b, 2 * k), y = e(b, 2 * k + 1);
            if (s32(mx - x) < 0) mx = x;
            if (s32(nx - x) > 0) nx = x;
            if (s32(my - y) < 0) my = y;
            if (s32(ny - y) > 0) ny = y;
        }
        return s32(mx - nx - 0x7FFu) > 0 || s32(my - ny - 0x7FFu) > 0;
    };
    uint32_t c = 0;
    auto emitEntry = [&](uint32_t b) {
        waitFifo();
        push(header);
        push(palette);
        for (uint32_t k = 0; k < 8; ++k) push(e(b, k));
        for (uint32_t k = 0; k < 4; ++k) push(e(b, 8 + 2 * k) + (e(b, 9 + 2 * k) << 8));
        push(c);
        (void)rd(kFifoTrigger);
        ++stats_.polys;
    };
    auto avg = [](uint32_t a, uint32_t b) { return static_cast<uint32_t>(s32(a + b) >> 1); };
    auto split = [&](uint32_t b) {
        const uint32_t x0 = e(b, 0), y0 = e(b, 1), x1 = e(b, 2), y1 = e(b, 3);
        const uint32_t x2 = e(b, 4), y2 = e(b, 5), x3 = e(b, 6), y3 = e(b, 7);
        const uint32_t u0 = e(b, 8), v0 = e(b, 9), u1 = e(b, 10), v1 = e(b, 11);
        const uint32_t u2 = e(b, 12), v2 = e(b, 13), u3 = e(b, 14), v3 = e(b, 15);
        auto put = [&](std::initializer_list<uint32_t> offs, uint32_t v) {
            for (uint32_t o : offs) wr(b + o, v);
        };
        // 003C0  vértices originais nos filhos
        put({0x12}, x1); put({0x13}, y1);
        put({0x24}, x2); put({0x25}, y2);
        put({0x36}, x3); put({0x37}, y3);
        // pontos médios das arestas e centro
        put({0x42, 0x10}, avg(x0, x1)); put({0x43, 0x11}, avg(y0, y1));
        put({0x14, 0x22}, avg(x1, x2)); put({0x15, 0x23}, avg(y1, y2));
        put({0x26, 0x34}, avg(x2, x3)); put({0x27, 0x35}, avg(y2, y3));
        put({0x46, 0x30}, avg(x0, x3)); put({0x47, 0x31}, avg(y0, y3));
        put({0x44, 0x16, 0x20, 0x32}, static_cast<uint32_t>(s32(x0 + x1 + x2 + x3) >> 2));
        put({0x45, 0x17, 0x21, 0x33}, static_cast<uint32_t>(s32(y0 + y1 + y2 + y3) >> 2));
        // 00400  coordenadas de textura, com a mesma topologia
        put({0x48}, u0); put({0x49}, v0);
        put({0x1A}, u1); put({0x1B}, v1);
        put({0x2C}, u2); put({0x2D}, v2);
        put({0x3E}, u3); put({0x3F}, v3);
        put({0x4A, 0x18}, avg(u0, u1)); put({0x4B, 0x19}, avg(v0, v1));
        put({0x1C, 0x2A}, avg(u1, u2)); put({0x1D, 0x2B}, avg(v1, v2));
        put({0x2E, 0x3C}, avg(u2, u3)); put({0x2F, 0x3D}, avg(v2, v3));
        put({0x4E, 0x38}, avg(u0, u3)); put({0x4F, 0x39}, avg(v0, v3));
        put({0x4C, 0x1E, 0x28, 0x3A}, static_cast<uint32_t>(s32(u0 + u1 + u2 + u3) >> 2));
        put({0x4D, 0x1F, 0x29, 0x3B}, static_cast<uint32_t>(s32(v0 + v1 + v2 + v3) >> 2));
        // 00441  4º filho por cima do pai: [b+2..b+15] = [b+0x42..b+0x4F]
        for (uint32_t k = 0; k < 14; ++k) wr(b + 2 + k, rd(b + 0x42 + k));
    };

    // 00335  raiz totalmente fora da tela: pula A, B, C
    if (rejected(clip)) {
        model += 3;
        return;
    }
    // 00351  desempacota os bytes de A e B (u, v de cada vértice); C = página de textura
    for (uint32_t w = 0; w < 2; ++w) {
        const uint32_t t = rd(model++);
        for (uint32_t k = 0; k < 4; ++k) wr(clip + 8 + 4 * w + k, (t >> (8 * k)) & 0xFFu);
    }
    c = rd(model++);
    const uint32_t limit = clip + 0xF0;
    uint32_t top = clip;
    bool doSplit = true;
    for (;;) {
        if (doSplit) {
            // 003BD  pilha cheia: desenha como está
            if (top < limit) {
                split(top);
                top += 0x30;
            } else {
                emitEntry(top);
                top -= 0x10;
            }
        }
        // 00370  pilha vazia?
        if (clip > top) break;
        if (rejected(top)) {
            top -= 0x10;
            doSplit = false;
            continue;
        }
        doSplit = tooBig(top);
        if (!doSplit) {
            emitEntry(top);
            top -= 0x10;
        }
    }
}

} // namespace

void renderList(MemoryBus& bus, uint32_t head, Pipeline3dStats& stats) {
    Pipe(bus, stats).list(head);
}

void subdivideQuad(MemoryBus& bus, uint32_t clip, uint32_t& model, uint32_t palette, uint32_t header,
                   Pipeline3dStats& stats) {
    Pipe(bus, stats).subdivide(clip, model, palette, header);
}

void renderScene(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, Pipeline3dStats& stats) {
    const uint32_t r4 = cpu.r[4];
    cpu.dp = 0;
    // 00065  [C8C7] |= 0x80 → 0x994000
    cpu.r[0] = bus.readWord(kFlagsVar) | 0x80u;
    bus.writeWord(kFlagsVar, cpu.r[0]);
    bus.writeWord(kFlagsPort, cpu.r[0]);
    // 0006B  [0x4D] = rotY(−yaw); depois [0x4D] = câmera × [0x4D]
    cpu.ar[2] = bus.readWord(kYawMatPtr);
    cpu.r[2] = doubleToTms(-tmsToDouble(bus.readWord(kCamYaw)));
    subs.call(0x938A, cpu, bus);
    cpu.ar[2] = bus.readWord(kYawMatPtr);
    cpu.r[3] = cpu.ar[2];
    cpu.r[2] = bus.readWord(kCamMatPtr);
    subs.call(0x9494, cpu, bus);
    // 00074  [0x4E] = elementos 0, 2, 6, 8
    cpu.ar[0] = bus.readWord(kYaw2x2Ptr);
    const uint32_t sel[4] = {0, 2, 6, 8};
    for (uint32_t i = 0; i < 4; ++i) {
        cpu.r[0] = bus.readWord(cpu.ar[2] + sel[i]);
        bus.writeWord(cpu.ar[0] + i, cpu.r[0]);
    }
    // 0007D  as quatro listas
    for (uint32_t p : kListPtrs) {
        cpu.ar[0] = bus.readWord(p);
        renderList(bus, cpu.ar[0], stats);
    }
    // 00089  [C8C7] &= ~0x80 → 0x994000
    cpu.dp = 0;
    cpu.r[0] = bus.readWord(kFlagsVar) & ~0x80u;
    bus.writeWord(kFlagsVar, cpu.r[0]);
    bus.writeWord(kFlagsPort, cpu.r[0]);
    cpu.r[4] = r4;
}

void registerPipeline3d(SubroutineDispatcher& d, Pipeline3dStats& stats) {
    d.add(0x0064, [&stats](Tms32031State& c, MemoryBus& b, ISubroutines& s) { renderScene(c, b, s, stats); });
}

} // namespace rea::core::render
