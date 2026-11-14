#pragma once
/**
 * @file pipeline3d.hpp
 * @brief Pipeline 3D da ROM (`0x0064`–`0x05F4`): transformação, projeção,
 *        descarte e envio dos polígonos para a fila de DMA (`0x600000`).
 *
 * @details **Entrada `0x0064`**, chamada uma vez por quadro pelo loop principal:
 * 1. liga o bit 0x80 de `[C8C7]` e o copia para `0x994000`;
 * 2. monta `[0x4D]` = câmera × rotY(−`[809807]`) (`0x938A` + `0x9494`) e copia
 *    os elementos 0, 2, 6 e 8 para a matriz 2×2 `[0x4E]`;
 * 3. desenha as listas `[[0x43]]`, `[[0x40]]`, `[[0x42]]` e `[[0x44]]`;
 * 4. desliga o bit 0x80.
 *
 * **Por objeto (`0x0091`)**:
 * - vetor de visão `[0x809821..23]`: posição relativa à câmera (flag 0x20 =
 *   já relativa), girada pela câmera `[0x47]` (sem a flag 1);
 * - profundidade `FIX(z)` → `+0x1C`; LOD: flag 0x200 e z > 8000 → modelo `+0x18`;
 *   com a flag 4 e z > 15000 → `+0x19`;
 * - descarte por esfera (raio = 1ª palavra do modelo): z ± r fora de
 *   [100, `[0x55]`] ou o círculo projetado fora de ±200 (y) / ±256 (x);
 * - matriz dos vértices: objeto já no espaço da câmera (flag 1), *billboard*
 *   (flag 8, só com z ≥ 300), só a câmera (flag 0x80) ou câmera × objeto
 *   (`[0x4B]`); a flag 0x800 desvia para o caminho articulado (`0x01BC`);
 * - modo 4 (`[C8C5] & 0xF == 4` e `[E871] != 0`): matriz 2×2 (`0x0169`, só giro em Y).
 *
 * **Vértice** (2 palavras no modelo): `(y << 16) | (x & 0xFFFF)` e z.
 * Projeção: `sx = X·T + 256`, `sy = Y·T·1,040039 + 200`, com
 * `T = [[0x52] + clamp(FIX(Z) >> 4, −0x50, 0x1387)]`.
 * Buffer `[0x4F]`: 3 floats por vértice (sx, sy, Z).
 *
 * **Polígono** (5 palavras): cabeçalho (id de paleta nos 16 bits altos),
 * índices (4 bytes), A, B, C (textura). Pacote de 15 palavras na fila:
 * cabeçalho, base de paleta, x/y inteiros dos vértices 0, 1, 2, 3, A,
 * A >> 16, B, B >> 16, C; a leitura de `0x980083` dispara o desenho.
 * - `0x0453` (objeto longe: z − r > 1000): só o descarte de face traseira;
 * - `0x029D`/`0x04C0` (perto): descarta o polígono com os 4 vértices atrás
 *   da câmera e o quad "torcido"; com x/y fora de ±1023 e caixa maior que
 *   0x7FF, subdivide em 4 recursivamente (`0x032F`, pilha de 16 palavras por quad);
 * - flag 0x400: base de paleta fixa (`+0x10`) em vez da tabela `[0x62]`;
 * - flag 0x40 (`0x059E`): polígonos lisos iluminados pela normal (pacote de 10
 *   palavras, modo `0x200 | sombra`).
 *
 * **Desvios do port** (ver `docs/architecture.md`):
 * - a aritmética de ponto flutuante usa double. O original guarda somas
 *   parciais em registradores de 40 bits e trunca no `STF`. O port aplica o
 *   truncamento do `STF` (`doubleToTms`) só onde o original relê o valor da
 *   memória;
 * - os registradores de saída não são reproduzidos (o chamador, em `0x4BE7`, não os
 *   usa), exceto R4 (preservado) e DP = 0;
 * - a trava de depuração de `0x0094` (`[C92E]` == objeto → laço infinito) e a
 *   espera infinita da fila só incrementam `Stats::hangs`.
 */

#include <cstdint>

#include "core/subroutines.hpp"

namespace rea::core::render {

/// Ponteiros e constantes do pipeline (DP = 0).
namespace p3d {
inline constexpr uint32_t kListPtrs[4] = {0x43, 0x40, 0x42, 0x44}; ///< Ordem de desenho das listas.
inline constexpr uint32_t kCamPosPtr    = 0x45; ///< `[0x45]` = 0x809800: posição da câmera.
inline constexpr uint32_t kCamMatPtr    = 0x47; ///< `[0x47]` = 0x809809: matriz da câmera.
inline constexpr uint32_t kLightPtr     = 0x49; ///< `[0x49]`: vetor de luz (usa `[-1]`, `[0]`, `[+1]`).
inline constexpr uint32_t kComposedPtr  = 0x4B; ///< `[0x4B]` = 0x809818: câmera × objeto.
inline constexpr uint32_t kViewPtr      = 0x4C; ///< `[0x4C]` = 0x809822: vetor de visão em `[-1]`, `[0]`, `[+1]`.
inline constexpr uint32_t kYawMatPtr    = 0x4D; ///< `[0x4D]`: câmera × rotY(−yaw).
inline constexpr uint32_t kYaw2x2Ptr    = 0x4E; ///< `[0x4E]`: elementos 0, 2, 6, 8 de `[0x4D]`.
inline constexpr uint32_t kVertexBufPtr = 0x4F; ///< `[0x4F]` = 0x809D00: sx, sy, Z por vértice.
inline constexpr uint32_t kTempAPtr     = 0x50; ///< `[0x50]` = 0x809815.
inline constexpr uint32_t kTempBPtr     = 0x51; ///< `[0x51]` = 0x809816.
inline constexpr uint32_t kPerspPtr     = 0x52; ///< `[0x52]`: tabela de perspectiva (por Z >> 4).
inline constexpr uint32_t kHalfWidth    = 0x53; ///< `[0x53]` = 256.0.
inline constexpr uint32_t kHalfHeight   = 0x54; ///< `[0x54]` = 200.0.
inline constexpr uint32_t kFarLimit     = 0x55; ///< `[0x55]` = 80000.
inline constexpr uint32_t kBoneMatPtr   = 0x5A; ///< `[0x5A]` = 0x809837: matriz do osso.
inline constexpr uint32_t kBoneOrgPtr   = 0x61; ///< `[0x61]` = 0x809856: origem do osso.
inline constexpr uint32_t kPalTablePtr  = 0x62; ///< `[0x62]` = 0xEA4C: slot de paleta por id.
inline constexpr uint32_t kClipBufPtr   = 0x5F5; ///< `[0x5F5]` = 0x809889: rascunho da subdivisão.
inline constexpr uint32_t kCamYaw       = 0x809807;  ///< Rumo da câmera (página 0x80 do DP).
inline constexpr uint32_t kFlagsVar     = 0xC8C7; ///< bit 0x80 = pipeline em execução.
inline constexpr uint32_t kFlagsPort    = 0x994000;  ///< Controle: bit7 LED, bit3 watchdog, bit1 reset do som DCS.
inline constexpr uint32_t kDebugObj     = 0xC92E;  ///< Depuração: o objeto com este endereço trava o original num laço (aqui só conta em `hangs`).
inline constexpr uint32_t kGameMode     = 0xC8C5;  ///< Modo do jogo: bits 0–3 = modo; 0x40 e 0x100 = flags.
inline constexpr uint32_t kMode4Enable  = 0xE871;  ///< No modo 4, != 0 liga este caminho; a INT0 também o soma a 0x91 como item de auditoria.
inline constexpr uint32_t kFifo         = 0x600000;  ///< Fila de DMA de quads: recebe os pacotes de 15 palavras.
inline constexpr uint32_t kFifoStatus   = 0x980082; ///< bit 2 = fila ocupada.
inline constexpr uint32_t kFifoTrigger  = 0x980083;  ///< Ler esta porta dispara o desenho do quad da fila (`0x980082` só informa o status).
inline constexpr uint32_t kMulY         = 0x0052;   ///< Imediato curto do `MPYF`: 1,040039.
} // namespace p3d

/// Contadores do pipeline (diagnóstico do port).
struct Pipeline3dStats {
    uint64_t objects = 0;     ///< Objetos visitados.
    uint64_t culled = 0;      ///< Objetos descartados pela esfera.
    uint64_t polys = 0;       ///< Pacotes enviados à fila.
    uint64_t backfaces = 0;   ///< Polígonos descartados (face traseira, atrás, torcidos).
    uint64_t subdivided = 0;  ///< Polígonos que passaram pela `0x032F`.
    uint64_t hangs = 0;       ///< Travas do original (depuração `[C92E]`, fila sempre ocupada).
};

/// @brief `0x0064`: desenha as quatro listas de objetos.
void renderScene(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs, Pipeline3dStats& stats);

/**
 * @brief `0x0166`: desenha a lista cujo primeiro objeto está em `[head]`.
 * @param head Endereço da palavra com o primeiro objeto (ex.: `[0x40]` = 0xC982).
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param stats Contadores do pipeline 3D, atualizados pela chamada.
 */
void renderList(MemoryBus& bus, uint32_t head, Pipeline3dStats& stats);

/**
 * @brief `0x032F`: subdivide um quad grande em 4 até caber em 0x7FF × 0x7FF.
 * @param clip Endereço do rascunho (8 coordenadas inteiras do quad).
 * @param model Ponteiro para as palavras A, B, C do polígono. Volta avançado de 3.
 * @param palette Base de paleta (palavra 1 do pacote).
 * @param header Cabeçalho (palavra 0 do pacote).
 * @param bus Barramento de memória simulado (RAM, ROM e I/O).
 * @param stats Contadores do pipeline 3D, atualizados pela chamada.
 */
void subdivideQuad(MemoryBus& bus, uint32_t clip, uint32_t& model, uint32_t palette, uint32_t header,
                   Pipeline3dStats& stats);

/// @brief Registra `0x0064`.
void registerPipeline3d(SubroutineDispatcher& d, Pipeline3dStats& stats);

} // namespace rea::core::render
