/**
 * @file dma_renderer.cpp
 * @brief Quads do V-Unit: `process_dma_queue` + `render_polygon<4, N>` + `render_flat/tex/textrans/textransmask` (MAME).
 */
#include "core/video/dma_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/vunit_hw.hpp"

namespace rea::core::video {

namespace {

/// @brief `float` → `int32_t` como o `cvttss2si` do x86 (o MAME roda assim): fora da faixa ou NaN dá `INT32_MIN`.
/// @note Em C++ a conversão fora da faixa é indefinida; aqui o resultado fica igual em qualquer plataforma.
int32_t floatToInt32(float f) {
    if (!(f >= -2147483648.0f && f < 2147483648.0f)) return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(f);
}

/// Vértice do quad em coordenadas de tela, com os parâmetros de textura (u, v).
struct Vertex {
    float x;     ///< Coluna.
    float y;     ///< Linha.
    float p[2];  ///< Parâmetros interpolados: u e v.
};

/// `poly_manager::round_coordinate` (o meio arredonda para −∞).
int32_t roundCoordinate(float value) {
    if (value >= static_cast<float>(std::numeric_limits<int32_t>::max())) return std::numeric_limits<int32_t>::max();
    const float ipart = std::floor(value);
    if (ipart < static_cast<float>(std::numeric_limits<int32_t>::min())) return std::numeric_limits<int32_t>::min();
    const float fpart = value - ipart;
    return static_cast<int32_t>(ipart) + (fpart > 0.5f ? 1 : 0);
}

/// `make_vertices_inclusive`: empurra os pontos da direita/de baixo em 0,001 (ordem horária).
void makeVerticesInclusive(Vertex* vert) {
    uint8_t rmask = 0, bmask = 0, eqmask = 0;
    for (int vnum = 0; vnum < 4; vnum++) {
        const Vertex& currv = vert[vnum];
        const Vertex& nextv = vert[(vnum + 1) & 3];
        if (nextv.y == currv.y && nextv.x == currv.x) eqmask |= 1 << vnum;
        if (nextv.y > currv.y || (nextv.y == currv.y && nextv.x < currv.x)) rmask |= 1 << vnum;
        if (nextv.x < currv.x || (nextv.x == currv.x && nextv.y < currv.y)) bmask |= 1 << vnum;
    }
    if (eqmask == 0x0f) return;
    for (int vnum = 0; vnum < 4; vnum++) {
        int effvnum = vnum;
        while (eqmask & (1 << effvnum)) effvnum = (effvnum + 1) & 3;
        if (rmask & (1 << effvnum)) vert[vnum].x += 0.001f;
        if (bmask & (1 << effvnum)) vert[vnum].y += 0.001f;
    }
}

/// Modo de pintura do quad (palavra 0 da fila).
enum class Mode { Flat, Tex, TexTrans, TexTransMask };

/// Trecho de uma linha do polígono, como o `extent_t` do MAME.
struct Extent {
    int32_t startx;   ///< Primeira coluna.
    int32_t stopx;    ///< Coluna final (exclusiva).
    float pstart[2];  ///< u, v na primeira coluna.
    float pdpdx[2];   ///< Variação de u, v por coluna.
};

/// Retângulo de recorte (área visível).
struct Clip {
    int32_t left;    ///< Primeira coluna visível.
    int32_t right;   ///< Última coluna visível.
    int32_t top;     ///< Primeira linha visível.
    int32_t bottom;  ///< Última linha visível.
};

/// `render_polygon<4, ParamCount>`: chama @p span(y, extent) para cada linha.
template <typename Span>
void renderPolygon(const Clip& clip, const Vertex* v, int paramCount, Span&& span) {
    constexpr int kNumVerts = 4;
    int minv = 0, maxv = 0;
    for (int n = 1; n < kNumVerts; n++) {
        if (v[n].y < v[minv].y) minv = n;
        else if (v[n].y > v[maxv].y) maxv = n;
    }
    const int32_t miny = roundCoordinate(v[minv].y);
    const int32_t maxy = roundCoordinate(v[maxv].y);
    const int32_t minyclip = std::max(miny, clip.top);
    const int32_t maxyclip = std::min(maxy, clip.bottom + 1);
    if (maxyclip - minyclip <= 0) return;

    struct Edge {
        const Vertex* v1;
        const Vertex* v2;
        float dxdy;
        float dpdy[2];
    };
    Edge fedge[kNumVerts - 1]{}, bedge[kNumVerts - 1]{};
    int nf = 0, nb = 0;
    for (int curv = minv; curv != maxv; curv = (curv == kNumVerts - 1) ? 0 : curv + 1) {
        Edge& e = fedge[nf];
        e.v1 = &v[curv];
        e.v2 = &v[(curv == kNumVerts - 1) ? 0 : curv + 1];
        if (e.v1->y == e.v2->y) continue;
        const float ooy = 1.0f / (e.v2->y - e.v1->y);
        e.dxdy = (e.v2->x - e.v1->x) * ooy;
        for (int p = 0; p < paramCount; p++) e.dpdy[p] = (e.v2->p[p] - e.v1->p[p]) * ooy;
        ++nf;
    }
    for (int curv = minv; curv != maxv; curv = (curv == 0) ? kNumVerts - 1 : curv - 1) {
        Edge& e = bedge[nb];
        e.v1 = &v[curv];
        e.v2 = &v[(curv == 0) ? kNumVerts - 1 : curv - 1];
        if (e.v1->y == e.v2->y) continue;
        const float ooy = 1.0f / (e.v2->y - e.v1->y);
        e.dxdy = (e.v2->x - e.v1->x) * ooy;
        for (int p = 0; p < paramCount; p++) e.dpdy[p] = (e.v2->p[p] - e.v1->p[p]) * ooy;
        ++nb;
    }
    const Edge *ledge, *redge;
    if ((fedge[0].v1 == bedge[0].v1 && fedge[0].dxdy < bedge[0].dxdy) ||
        (fedge[0].v1 != bedge[0].v1 && fedge[0].v1->x < bedge[0].v1->x)) {
        ledge = fedge;
        redge = bedge;
    } else {
        ledge = bedge;
        redge = fedge;
    }
    // Como no MAME, as listas podem ter entradas não preenchidas no fim (arestas horizontais);
    // o avanço é limitado por fully < v[maxv].y.
    for (int32_t y = minyclip; y < maxyclip; ++y) {
        const float fully = static_cast<float>(y) + 0.5f;
        while (fully > ledge->v2->y && fully < v[maxv].y) ++ledge;
        while (fully > redge->v2->y && fully < v[maxv].y) ++redge;
        const float startx = ledge->v1->x + (fully - ledge->v1->y) * ledge->dxdy;
        const float stopx = redge->v1->x + (fully - redge->v1->y) * redge->dxdy;
        int32_t istartx = roundCoordinate(startx);
        int32_t istopx = roundCoordinate(stopx);
        if (istartx > istopx) std::swap(istartx, istopx);
        Extent ext{};
        if (paramCount > 0) {
            const float ldy = fully - ledge->v1->y;
            const float rdy = fully - redge->v1->y;
            const float oox = 1.0f / (stopx - startx);
            for (int p = 0; p < paramCount; p++) {
                const float lparam = ledge->v1->p[p] + ldy * ledge->dpdy[p];
                const float rparam = redge->v1->p[p] + rdy * redge->dpdy[p];
                const float dpdx = (rparam - lparam) * oox;
                ext.pstart[p] = lparam + (static_cast<float>(istartx) + 0.5f - startx) * dpdx;
                ext.pdpdx[p] = dpdx;
            }
        }
        if (istartx < clip.left) {
            for (int p = 0; p < paramCount; p++) ext.pstart[p] += static_cast<float>(clip.left - istartx) * ext.pdpdx[p];
            istartx = clip.left;
        }
        if (istopx > clip.right) istopx = clip.right + 1;
        if (istartx >= istopx) istartx = istopx = 0;
        ext.startx = istartx;
        ext.stopx = istopx;
        span(y, ext);
    }
}

} // namespace

bool DmaRenderer::ioRead(uint32_t addr, uint32_t& value) {
    switch (addr) {
        case kEntriesPort: value = 0; return true;   // dma_queue_entries_r
        case kTriggerPort - 1: value = 0; return true;  // dma_trigger_r, offset 0
        case kTriggerPort:
            processQuad(queue_);
            index_ = 0;
            value = 0;
            return true;
        default: return false;
    }
}

void DmaRenderer::ioWrite(uint32_t addr, uint32_t value) {
    // MAME: uint16_t m_dma_data[16] — cada palavra da fila guarda só os 16 bits baixos.
    if (addr == kQueuePort && index_ < queue_.size()) queue_[index_++] = value & 0xFFFFu;
}

void DmaRenderer::processQuad(const std::array<uint32_t, 16>& dma) {
    Vertex vert[4]{};
    for (int i = 0; i < 4; ++i) {
        vert[i].x = static_cast<float>(static_cast<int16_t>(dma[2 + 2 * i])) + 0.5f;
        vert[i].y = static_cast<float>(static_cast<int16_t>(dma[3 + 2 * i])) + 0.5f;
    }
    makeVerticesInclusive(vert);

    auto pixdata = static_cast<uint16_t>(dma[1]);
    const bool textured = (dma[0] & 0x300u) == 0x100u;
    Mode mode = Mode::Flat;
    if (!textured) {
        pixdata = static_cast<uint16_t>(pixdata + (dma[0] & 0xFFu));
    } else {
        for (int i = 0; i < 4; ++i) {
            vert[i].p[0] = static_cast<float>(dma[10 + i] & 0xFFu) * 65536.0f + 32768.0f;
            vert[i].p[1] = static_cast<float>(dma[10 + i] >> 8) * 65536.0f + 32768.0f;
        }
        switch (dma[0] & 0xC00u) {
            case 0x000: mode = Mode::Tex; break;
            case 0x800: mode = Mode::TexTrans; break;
            case 0xC00: mode = Mode::TexTransMask; pixdata = static_cast<uint16_t>(pixdata + (dma[0] & 0xFFu)); break;
            default: mode = Mode::Flat; pixdata = static_cast<uint16_t>(pixdata + (dma[0] & 0xFFu)); break;
        }
    }
    const uint32_t page = bus_.readWord(vunit::kPageControl);
    const uint32_t destbase = (page & 4u) ? vunit::kVideoPageWords : 0u;
    const std::size_t texbase = static_cast<std::size_t>(dma[14]) * 256u;
    const int dither = (dma[0] & 0x2000u) ? 1 : 0;

    // Área visível (video_control_w): max = (total + fim_blank − início_blank) % total
    Clip clip{0, 511, 0, 399};
    const uint32_t r2 = bus_.readWord(0x980022) & 0xFFFFu, r5 = bus_.readWord(0x980025) & 0xFFFFu;
    const uint32_t r6 = bus_.readWord(0x980026) & 0xFFFFu, r7 = bus_.readWord(0x980027) & 0xFFFFu;
    const uint32_t r10 = bus_.readWord(0x98002A) & 0xFFFFu, r11 = bus_.readWord(0x98002B) & 0xFFFFu;
    if (r6 != 0 && r11 != 0) {
        clip.right = static_cast<int32_t>((r6 + r2 - r5) % r6);
        clip.bottom = static_cast<int32_t>((r11 + r7 - r10) % r11);
    }

    if (textured) last_palette_[(texbase / TexturePack::kPageBytes) % TexturePack::kPages] = pixdata;
    auto texel = [&](int32_t u, int32_t v) -> uint8_t {
        const int64_t off = static_cast<int64_t>(texbase) + ((v >> 8) & 0xFF00) + (u >> 16);
        if (off < 0) return 0;
        uint8_t t;
        if (pack_ && pack_->texel(static_cast<std::size_t>(off), t)) return t;  // re-skin
        return bus_.textureByte(static_cast<std::size_t>(off));
    };

    renderPolygon(clip, vert, textured ? 2 : 0, [&](int32_t y, const Extent& e) {
        const uint32_t row = destbase + static_cast<uint32_t>(y) * vunit::kVideoStride;
        const int xstep = dither + 1;
        int32_t startx = e.startx;
        if (mode == Mode::Flat) {
            startx += (y ^ startx) & dither;
            if (pixdata == 0 && xstep == 1) {
                for (int32_t x = startx; x <= e.stopx; ++x) bus_.videoWrite(row + static_cast<uint32_t>(x), 0);
                pixels_ += static_cast<uint64_t>(std::max(0, e.stopx - startx + 1));
            } else {
                for (int32_t x = startx; x < e.stopx; x += xstep) {
                    bus_.videoWrite(row + static_cast<uint32_t>(x), pixdata);
                    ++pixels_;
                }
            }
            return;
        }
        // u, v em 16.16 sem sinal: as somas "dão a volta" em 32 bits como no int32 do MAME, sem estouro
        // indefinido de inteiro com sinal; o texel recebe os mesmos bits reinterpretados como int32.
        auto u = static_cast<uint32_t>(floatToInt32(e.pstart[0]));
        auto v = static_cast<uint32_t>(floatToInt32(e.pstart[1]));
        auto dudx = static_cast<uint32_t>(floatToInt32(e.pdpdx[0]));
        auto dvdx = static_cast<uint32_t>(floatToInt32(e.pdpdx[1]));
        if (xstep == 2) {
            if ((y ^ startx) & 1) {
                startx++;
                u += dudx;
                v += dvdx;
            }
            dudx *= 2u;
            dvdx *= 2u;
        }
        for (int32_t x = startx; x < e.stopx; x += xstep) {
            const uint8_t pix = texel(static_cast<int32_t>(u), static_cast<int32_t>(v));
            if (mode == Mode::Tex) {
                bus_.videoWrite(row + static_cast<uint32_t>(x), static_cast<uint16_t>(pixdata + pix));
            } else if (pix != 0) {
                bus_.videoWrite(row + static_cast<uint32_t>(x),
                                mode == Mode::TexTrans ? static_cast<uint16_t>(pixdata + pix) : pixdata);
            }
            ++pixels_;
            u += dudx;
            v += dvdx;
        }
    });
    ++quads_;
}

} // namespace rea::core::video
