/**
 * @file test_video.cpp
 * @brief Vídeo: LZW sintético, céu, textos sobrepostos, paletas, VRAM/páginas, DMA de quads e re-skin de texturas.
 */
#include "test_support.hpp"

using namespace rea::core;
using namespace rea::test;

namespace {

/// RAM de texturas e descompressor LZW (fluxo sintético)
void testLzwSynthetic() {
    {
        MemoryBus tb;
        tb.writeWord(0xA00010, 0xDEAD1234u);
        check(tb.readWord(0xA00010) == 0x1234u && tb.textureByte(0x20) == 0x34 && tb.textureByte(0x21) == 0x12,
              "RAM de texturas: 16 bits baixos = 2 texels (MAME textureram_w)");

        using namespace gfx::lzw_vars;
        tb.writeWord(kDictPtr, 0x1976D);
        tb.writeWord(kStackPtr, 0x1B9F7);
        tb.writeWord(kBufferPtr, 0xC8EE);
        tb.writeWord(kContextPtr, 0xE5D0);
        std::vector<uint8_t> data(512);
        const char* phrase = "AAAAAAAAAAAATOBEORNOTTOBEORTOBEORNOT";
        for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(phrase[i % 36] + (i / 200));
        const auto stream = lzwEncode(data, true);
        for (std::size_t i = 0; i < stream.size(); ++i) tb.writeWord(0x10000 + static_cast<uint32_t>(i), stream[i]);

        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        gfx::registerRoutines(d);
        Tms32031State c;
        c.ar[4] = 0x10000;
        c.ar[5] = 0xA00000;
        c.r[6] = 0x66;
        tb.writeWord(kBlocking, 1);
        d.call(0xA2F9, c, tb);
        bool same = true;
        for (std::size_t i = 0; i < data.size(); ++i) same = same && tb.textureByte(i) == data[i];
        check(same, "LZW sintético (literais, KwKwK, 0x101): 512 bytes idênticos na RAM de texturas");
        check(tb.readWord(kActive) == 0 && tb.readWord(kBlocking) == 0, "fim do fluxo: [E5CA] = [E5CB] = 0");
        check(c.ar[4] == 0x10000 && c.ar[5] == 0xA00000 && c.r[6] == 0x66 && c.bk == 0xA316,
              "0x9042/0x905C: AR/R preservados; BK = retorno do CALL $905C");
        check((c.st & Tms32031State::kStGie) != 0 && tb.readWord(vunit::kBusControl) == 0x470,
              "flush termina em RETIU (GIE ligado) e volta os wait states para 0x470");
    }
}

/// Céu: projeção 0x839B (x + rolagem dobrado, y + [E3FD], 1,04·y + [0x54])
void testSkyProjection() {
    {
        MemoryBus mb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        math::registerFloatMath(d);
        video::registerSkyRenderer(d);
        Tms32031State c;
        mb.writeWord(0x5A, 0x2000);
        mb.writeWord(0x5D, 0x2010);
        for (uint32_t i = 0; i < 9; ++i) mb.writeWord(0x2000 + i, doubleToTms(i % 4 == 0 ? 1.0 : 0.0));
        mb.writeWord(0x53, doubleToTms(256.0));
        mb.writeWord(0x54, doubleToTms(200.0));
        mb.writeWord(0xE3FD, doubleToTms(10.0));
        mb.writeWord(0x8226, doubleToTms(100.0));
        const double pts[] = {1, 2, 3, 98, 0, 0};
        for (uint32_t i = 0; i < 6; ++i) mb.writeWord(0x2100 + i, doubleToTms(pts[i]));
        c.r[6] = doubleToTms(5.0);
        c.ar[5] = 0x2100;
        c.ar[6] = 0x2200;
        c.ar[4] = 1;
        d.call(0x839B, c, mb);
        const double x0 = tmsToDouble(mb.readWord(0x2200)), y0 = tmsToDouble(mb.readWord(0x2201));
        const double x1 = tmsToDouble(mb.readWord(0x2203));
        check(x0 == 262.0 && std::fabs(y0 - (12.0 * 1.040039 + 200.0)) < 0.01 && x1 == 256.0 + 3.0 && c.ar[6] == 0x2206,
              "0x839B: pontos do céu com rolagem dobrada em [8226], y + [E3FD], 1,04·y + [0x54]");
    }
}

/// Textos sobrepostos (0x774A / 0x7720)
void testTextOverlay() {
    {
        MemoryBus tb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        video::registerTextOverlay(d);
        tb.writeWord(0x763F, 0xE91A);
        tb.writeWord(0x763E, 0xE919);
        tb.writeWord(0x48, 0xEA34);
        const uint32_t n1 = 0x30000, font = 0x31000, str = 0x32000;
        tb.writeWord(0xE91A, n1);
        tb.writeWord(n1, 0);
        tb.writeWord(n1 + 1, str);
        tb.writeWord(n1 + 3, doubleToTms(100.0));
        tb.writeWord(n1 + 4, doubleToTms(50.0));
        tb.writeWord(n1 + 5, doubleToTms(2.0));
        tb.writeWord(n1 + 6, doubleToTms(0.0));
        tb.writeWord(n1 + 7, 2);
        tb.writeWord(n1 + 8, 0x100);  // centralizado
        tb.writeWord(n1 + 9, 10);
        tb.writeWord(n1 + 0xA, font);
        tb.writeWord(n1 + 0xB, 0x77);
        tb.writeWord(n1 + 0xC, 0x500);
        // glyph '0': x+1, 2 extra, u 10..18, v 3; glyph '1': x+0, 0 extra, u 20..24, v 3
        const uint32_t g[] = {(2u << 16) | 1u, 10, 18, 3, 0, 20, 24, 3};
        for (uint32_t i = 0; i < 8; ++i) tb.writeWord(font + i, g[i]);
        tb.writeWord(str, 0x00312030u);  // "0 1"
        Tms32031State c;
        c.ar[2] = str; c.ar[4] = n1;
        d.call(0x7720, c, tb);
        // largura: '0' = 8 + 1 + 2 = 11; espaço = 11; '1' = 4 → 26
        const bool w = c.r[0] == 26 && c.ar[2] == str;
        c.ar[4] = 0x999; c.r[7] = 0;
        d.call(0x774A, c, tb);
        check(w && c.ar[4] == 0x999, "0x7720: largura da string (o espaço repete a última largura)");
        // x = 100 − 13 = 87; '0' em 88..96; avanço 8 + 2 → 98; espaço +10 → 108; '1' em 108..112
        check(tb.readWord(0xEA36) == 108 && tb.readWord(0xEA39) == 112 && tb.readWord(0xEA37) == 50 &&
                  tb.readWord(0xEA3D) == 60 && tb.readWord(0xEA42) == (20u | (3u << 8)) &&
                  tb.readWord(0xEA45) == (20u | (13u << 8)) && tb.readWord(0xEA34) == 0x900 && tb.readWord(0xEA46) == 0x77 &&
                  tmsToDouble(tb.readWord(n1 + 3)) == 102.0 && tb.readWord(n1 + 7) == 1,
              "0x774A: glyphs em quads (0xAED6), centralizado, move e envelhece");
        d.call(0x774A, c, tb);
        check(tb.readWord(0xE91A) == 0 && tb.readWord(0xE919) == n1 && tb.readWord(0xE91B) == 1,
              "0x774A: texto sem vida volta para a lista livre");
    }
}

/// 0x95F5: paletas básicas do jogo
void testBasicPalettes() {
    {
        MemoryBus pb;
        RecordingSubroutines pend;
        for (uint32_t v = 0xC710; v <= 0xC730; ++v) pb.writeWord(v, 0x50000 + v);
        pb.writeWord(0x9E00FF, 0x1234);
        Tms32031State c;
        video::loadBasePalettes(c, pb, pend);
        std::vector<uint32_t> seq;
        for (const auto& e : pend.calls) seq.push_back(e.addr == 0x9F2F || e.addr == 0x9DF6 ? e.ar2 - 0x50000 : e.addr);
        const std::vector<uint32_t> want = {0x9E80, 0xC710, 0xC711, 0xC712, 0xC713, 0xC714, 0xC715, 0x96FF, 0xC716,
                                            0x9EFA, 0xC717, 0xC718, 0xC719, 0xC71A, 0xC71B, 0xC71C};
        check(seq == want && pb.readWord(0x9E00FF) == 0, "0x95F5: reinicia a fila (0x9E80) e carrega as paletas básicas");
    }
}

/// Slots de paleta (0x9DF6-0x9FB5)
void testPaletteSlots() {
    {
        MemoryBus pb;
        pb.writeWord(0x9E7B, 0xA005);
        pb.writeWord(0x9E7C, 0xCA8D);
        pb.writeWord(0x9E7D, 0xC98D);
        pb.writeWord(0x9E7E, 0xEA4C);
        pb.writeWord(0x9E7F, 0xCA0D);
        pb.writeWord(0x9F6F, 0xCC0F);
        pb.writeWord(0x9F70, 0xCC10);
        pb.writeWord(0x9F71, 0xCC12);
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        video::registerPaletteSlots(d);
        scene::registerObjectQuery(d);
        uint64_t hangs = 0;
        scene::registerSceneObject(d, hangs);
        Tms32031State c;
        d.call(0x9E80, c, pb);
        // paleta A (por ponteiro): 3 palavras simples; paleta B (id 5): empacotada, 2 palavras -> 4 cores
        pb.writeWord(0x30000, 3);
        pb.writeWord(0x30001, 0x1111); pb.writeWord(0x30002, 0x2222); pb.writeWord(0x30003, 0x3333);
        pb.writeWord(0x31000, 0x80000004u);
        pb.writeWord(0x31001, 0x00BB00AAu); pb.writeWord(0x31002, 0x00DD00CCu);
        pb.writeWord(0xA005 + 5, 0x31000);
        c.ar[2] = 0x30000;
        d.call(0x9F2F, c, pb);
        const uint32_t slotA = c.r[0];
        c.ar[2] = 5;
        d.call(0x9EFA, c, pb);
        const uint32_t slotB = c.r[0];
        c.ar[2] = 5;
        d.call(0x9EFA, c, pb);
        check(slotA == 0 && slotB == 0x100 && c.r[0] == 0x100 && pb.readWord(0xEA4C + 5) == 0x10002 &&
                  pb.readWord(0xC98D) == 0xFFFFFFFFu && pb.readWord(0xC98E) == 0x8005 && pb.readWord(0xCA0D) == 0x30000 &&
                  pb.readWord(0xCC11) == 1 && (c.st & Tms32031State::kStGie),
              "0x9F2F/0x9EFA: slots 0 e 1 (R0 = slot << 8), contagem de referências, pedidos na fila");
        d.call(0x9E97, c, pb);
        // Empacotado: a 1ª cor recebe a palavra inteira (STI R2), a 2ª os 16 bits altos; a cor usa os 15 bits baixos.
        check(pb.paletteWord(0) == 0x1111 && pb.paletteWord(2) == 0x3333 && pb.paletteWord(0x100) == 0x00BB00AAu &&
                  pb.paletteWord(0x101) == 0xBB && pb.paletteWord(0x102) == 0x00DD00CCu && pb.paletteWord(0x103) == 0xDD &&
                  pb.readWord(0xCC0F) == 0,
              "0x9E97: copia os pedidos (simples e empacotado) para 0x9E0000 + slot*256 e esvazia a fila");
        d.call(0x9E97, c, pb);
        check(pb.readWord(0xCC11) == 0, "0x9E97 com a fila vazia: [CC11] = 0");
        c.ar[2] = 0x30000;
        d.call(0x9EDB, c, pb);
        const bool byPtr = (c.st & Tms32031State::kStCarry) && c.r[0] == 0;
        c.ar[2] = 5;
        d.call(0x9ECD, c, pb);
        check(byPtr && !(c.st & Tms32031State::kStCarry) && c.r[0] == 0x100, "0x9EDB/0x9ECD: acham o slot por ponteiro / por id");
        c.ar[2] = 5;
        d.call(0x9F9F, c, pb);
        check(pb.readWord(0xEA4C + 5) == 0 && pb.readWord(0xC98E) == 0, "0x9F9F: libera o slot do id (ignora a contagem)");
        // 14 pedidos: 13 copiados, o 13º continua na fila
        for (int i = 0; i < 14; ++i) { c.ar[2] = 0x30000; d.call(0x9F2F, c, pb); }
        d.call(0x9E97, c, pb);
        uint32_t left = 0;
        for (uint32_t p = pb.readWord(0xCC0F); p != 0 && left < 200; p = pb.readWord(p)) ++left;
        check(left == 2, "0x9E97: 13 cópias por quadro; o 13º pedido não sai da fila (fica com o 14º)");
    }
}

/// Vídeo: VRAM, páginas, pixel, retângulo, texto, números, composição
void testVideoMemory() {
    {
        using namespace video::video_vars;
        MemoryBus vb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        video::registerRoutines(d);
        Tms32031State c;
        vb.writeWord(0x900000, 0x12345678);
        check(vb.readWord(0x900000) == 0x5678 && vb.videoWord(0) == 0x5678, "VRAM 0x900000: só 16 bits por palavra (MAME)");

        vb.writeWord(kPage0Ptr, 0x900000);
        vb.writeWord(kPage1Ptr, 0x940000);
        vb.writeWord(kPageWordsM1Ptr, 0x3FFFF);
        d.call(0x8E4E, c, vb);
        const bool p1 = vb.readWord(kDrawBase) == 0x940000 && vb.readWord(vunit::kPageControl) == 4;
        d.call(0x8E59, c, vb);
        check(p1 && vb.readWord(kDrawBase) == 0x900000 && vb.readWord(vunit::kPageControl) == 1 && c.dp == 0,
              "0x8E4E / 0x8E59: desenha numa página e exibe a outra ([CE13], [0x980040] bits 0 e 2)");
        vb.writeWord(kForcePage1, 1);
        d.call(0x8E59, c, vb);
        check(vb.readWord(kDrawBase) == 0x940000 && vb.readWord(vunit::kPageControl) == 5, "[8E64] != 0: fica na página 1 (0x8E65)");
        vb.writeWord(kForcePage1, 0);

        c.ar[2] = 3; c.r[2] = 2; c.r[3] = 7; c.ar[1] = 0x11; c.st = 0;
        d.call(0xA824, c, vb);
        check(vb.readWord(0x940000 + 2 * 512 + 3) == 7 && c.ar[2] == 3 && c.r[2] == 2 && c.ar[1] == 0x11 &&
                  (c.st & Tms32031State::kStGie) && vb.readWord(vunit::kBusControl) == 0x470,
              "0xA824: pixel em [CE13] + y*512 + x; registradores preservados; GIE ligado; barramento 0x470");
        {
            // 0xAC23: vertical de y=5 até y=0 (troca as pontas) e inclinada (0,20)→(4,22) percorrendo x.
            auto px = [&](int x, int y) { return vb.readWord(0x940000 + y * 512 + x); };
            c.ar[2] = 10; c.r[2] = 5; c.r[3] = 10; c.rc = 0; c.rs = 9; c.r[5] = 0x55;
            d.call(0xAC23, c, vb);
            bool vert = c.r[5] == 0x55 && c.rs == 10 && px(10, 6) == 0;
            for (int y = 0; y <= 5; ++y) vert = vert && px(10, y) == 9;
            c.ar[2] = 0; c.r[2] = 20; c.r[3] = 4; c.rc = 22; c.rs = 6;
            d.call(0xAC23, c, vb);
            const bool diag = px(0, 20) == 6 && px(1, 20) == 6 && px(2, 21) == 6 && px(3, 21) == 6 && px(4, 22) == 6 &&
                              px(1, 21) == 0;
            check(vert && diag, "0xAC23: linha vertical (6 pontos, pontas trocadas) e inclinada por x (dy/dx = 0,5)");
            // Íngreme com as pontas trocadas (60,12)→(58,8): percorre y de 8 a 12; a troca não mexe em RS
            // (fica o x1 de entrada) e deixa o maior y em RC (0AC42–0AC46).
            c.ar[2] = 60; c.r[2] = 12; c.r[3] = 58; c.rc = 8; c.rs = 3;
            d.call(0xAC23, c, vb);
            // 0x5D00: mistura sem o limite 0,08/0,92 (o original mistura com R7, copiado antes do limite) e
            // canais sem máscara (t fora de [0, 1] invade o canal vizinho, como o OR do original).
            check(video::lerpColor15(0x0000, 0x001F, 0.05) == 1 && video::lerpColor15(0x0000, 0x001F, 0.95) == 29 &&
                      video::lerpColor15(0x0000, 0x001F, 1.5) == 46,
                  "lerpColor15 (0x5D00): t = 0,05 e 0,95 sem limite; t = 1,5 passa de 31 sem máscara");
            check(px(58, 8) == 3 && px(60, 12) == 3 && px(59, 10) == 3 && c.rs == 58 && c.rc == 12,
                  "0xAC23: linha íngreme com pontas trocadas; RS = x1 de entrada e RC = maior y na saída");
            c.ar[2] = 20; c.r[2] = 30; c.r[3] = 25; c.rc = 33; c.rs = 4; c.r[4] = 0x44; c.r[5] = 0x55;
            d.call(0xABFC, c, vb);
            bool rect = c.r[4] == 0x44 && c.r[5] == 0x55 && px(21, 31) == 0 && px(24, 32) == 0;
            for (int x = 20; x <= 25; ++x) rect = rect && px(x, 30) == 4 && px(x, 33) == 4;
            for (int y = 30; y <= 33; ++y) rect = rect && px(20, y) == 4 && px(25, y) == 4;
            check(rect, "0xABFC: retângulo (topo, base, laterais) com 0xAC23; interior intacto; R4/R5 preservados");
        }

        vb.writeWord(kDrawBase, 0x900000);
        c.ar[2] = 1; c.r[3] = 2; c.r[2] = 5; c.rc = 6; c.rs = 9;
        d.call(0xA7E5, c, vb);
        check(vb.readWord(0x900000 + 5 * 512 + 1) == 9 && vb.readWord(0x900000 + 6 * 512 + 2) == 9 &&
                  vb.readWord(0x900000 + 7 * 512 + 1) == 0 && vb.readWord(0x900000 + 5 * 512 + 3) == 0 && c.ar[2] == 1,
              "0xA7E5: retângulo inclusivo (x de AR2 a R3, y de R2 a RC) com a cor RS");

        // Texto com uma fonte sintética: 'A' = linhas 0x81 (pixel na 1ª e na 8ª coluna)
        vb.writeWord(kFontPtr, 0x3000);
        for (uint32_t r = 0; r < 7; ++r) vb.writeWord(0x3000 + (0x41 - 0x28) * 7 + r, 0x81);
        vb.writeWord(0x2000, 0x00412041);  // "A A"
        vb.writeWord(kDrawBase, 0x940000);
        c.ar[2] = 0x2000; c.r[2] = 10; c.r[3] = 20; c.rc = 3; c.r[4] = 0x44; c.r[5] = 0x55;
        d.call(0xA7F4, c, vb);
        std::size_t lit = 0;
        for (uint32_t y = 20; y < 27; ++y)
            for (uint32_t x = 0; x < 64; ++x) lit += vb.readWord(0x940000 + y * 512 + x) == 3;
        check(lit == 28 && vb.readWord(0x940000 + 20 * 512 + 10) == 3 && vb.readWord(0x940000 + 26 * 512 + 17) == 3 &&
                  vb.readWord(0x940000 + 20 * 512 + 26) == 3 && c.r[2] == 34 && c.r[3] == 20 && c.r[4] == 0x44 &&
                  c.r[5] == 0x55,
              "0xA7F4: glyphs 8x7 (bit 7 à esquerda), espaço avança 8; R2 = x final; R4/R5 preservados");

        c.ar[2] = 0x2100; c.r[2] = static_cast<uint32_t>(-123); c.r[1] = 0x77;
        d.call(0xA762, c, vb);
        check(vb.readWord(0x2100) == 0x3332313Eu && vb.readWord(0x2101) == 0 && c.r[0] == 0 && c.r[1] == 0x77 &&
                  c.ar[2] == 0x2100,
              "0xA762: -123 -> \">123\" (4 bytes) + palavra terminadora; R0 = 0");
        c.r[2] = 5; c.ar[7] = 0;
        d.call(0xA74E, c, vb);
        check(vb.readWord(0x2100) == 0x3530 && c.r[0] == 16, "0xA74E: 5 -> \"05\"; R0 = 16 bits");
        c.r[2] = 0;
        d.call(0xA762, c, vb);
        check(vb.readWord(0x2100) == 0x30 && c.r[0] == 8, "0xA762: 0 -> \"0\"");
        c.r[2] = 0xBEEF;
        d.call(0xA7AC, c, vb);
        check(vb.readWord(0x2100) == 0x46454542u && vb.readWord(0x2101) == 0 && c.r[0] == 0, "0xA7AC: 0xBEEF -> \"BEEF\"");

        // Composição: paleta 15 bits -> RGBA
        vb.writeWord(0x9E0001, 0x7FFF);
        vb.writeWord(0x9E0002, 0x7C00);
        vb.writeWord(0x900000, 1);
        vb.writeWord(0x940000, 2);
        vb.writeWord(vunit::kPageControl, 0);
        std::vector<uint32_t> img;
        video::composeDisplay(vb, img);
        const uint32_t a = img[0];
        vb.writeWord(vunit::kPageControl, 1);
        video::composeDisplay(vb, img);
        check(a == 0xFFFFFFFFu && img[0] == 0xFF0000FFu && img.size() == 512u * 400u,
              "composeDisplay: página do bit 0 de [0x980040]; xRRRRRGGGGGBBBBB -> RGBA (pal5bit)");

        c.r[3] = 0x3FF; c.r[2] = 0xAB; c.ar[2] = 0x900400;
        d.call(0x8E98, c, vb);
        check(vb.readWord(0x900400) == 0xAB && vb.readWord(0x9007FF) == 0xAB && vb.readWord(0x900800) == 0 &&
                  c.ar[2] == 0x900400 && c.rc == 0xFFFFFFFFu,
              "0x8E98: R3 + 1 palavras com R2; AR2 preservado; RC = -1");
        d.call(0x8E79, c, vb);
        check(vb.readWord(0x900400) == 0 && vb.readWord(0x940000) == 0, "0x8E79: limpa as duas páginas");
    }
}

/// DMA de quads (0x600000 / 0x980083) e 0xAECC / 0x8E6D
void testQuadDma() {
    {
        MemoryBus qb;
        video::DmaRenderer dma(qb);
        qb.attachDevice(&dma);
        qb.writeWord(vunit::kPageControl, 4);  // desenha na página 1
        auto px = [&](uint32_t x, uint32_t y) { return qb.videoWord(vunit::kVideoPageWords + y * 512 + x); };
        auto send = [&](std::array<uint32_t, 16> w) {
            for (uint32_t v : w) qb.writeWord(0x600000, v);
            qb.readWord(0x980083);
        };
        send({0x0005, 0x0100, 10, 20, 13, 20, 13, 22, 10, 22, 0, 0, 0, 0, 0, 0});
        std::size_t n = 0;
        for (uint32_t y = 18; y < 25; ++y)
            for (uint32_t x = 8; x < 16; ++x) n += px(x, y) != 0;
        check(n == 12 && px(10, 20) == 0x105 && px(13, 22) == 0x105 && px(14, 22) == 0 && px(10, 23) == 0 &&
                  qb.videoWord(10 + 20 * 512) == 0 && dma.quadsDrawn() == 1,
              "DMA: quad liso inclusivo (x 10-13, y 20-22) com cor dma[1] + dma[0] & 0xFF na página do bit 2");

        for (uint32_t x = 8; x < 16; ++x) qb.videoWrite(vunit::kVideoPageWords + 21 * 512 + x, 7);
        send({0x0000, 0x0000, 10, 20, 13, 20, 13, 22, 10, 22, 0, 0, 0, 0, 0, 0});
        check(px(9, 21) == 7 && px(10, 21) == 0 && px(14, 21) == 0 && px(15, 21) == 7,
              "DMA: cor 0 sem dither usa memset até stopx inclusive (um pixel a mais, como no MAME)");

        // Textura: texel(u, v) = u + 10v + 1 na página 2 (dma[14] = 2 -> byte 512)
        for (uint32_t v = 0; v < 4; ++v)
            for (uint32_t u = 0; u < 4; u += 2) {
                const uint32_t b = 512 + v * 256 + u;
                qb.writeWord(0xA00000 + b / 2, (u + 10 * v + 1) | ((u + 1 + 10 * v + 1) << 8));
            }
        send({0x0100, 0x0200, 30, 40, 33, 40, 33, 42, 30, 42, 0x0000, 0x0003, 0x0203, 0x0200, 2, 0});
        check(px(30, 40) == 0x201 && px(33, 42) == 0x200 + 24 && px(31, 40) == 0x202 && px(30, 41) == 0x200 + 11,
              "DMA: textura com u/v interpolados no centro do pixel (dma[10-13]) e página dma[14]");

        // Recorte na área visível (registradores de vídeo do RESET: x 0-511)
        send({0x0001, 0x0000, 500, 50, 530, 50, 530, 51, 500, 51, 0, 0, 0, 0, 0, 0});
        check(px(511, 50) == 1 && qb.videoWord(vunit::kVideoPageWords + 51 * 512 + 0) == 0,
              "DMA: recorte padrão em x = 511 (não vaza para a linha seguinte)");

        // 0x8E6D -> 0xAECC: limpa a página de render inteira
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        video::registerRoutines(d);
        Tms32031State c;
        qb.writeWord(video::video_vars::kQuadTemplatePtr, 0xEA34);
        for (uint32_t i = 0; i < 19; ++i) qb.writeWord(0xEA34 + i, 0);
        c.rc = 0x55; c.ar[0] = 0x66;
        d.call(0x8E6D, c, qb);
        bool clean = true;
        for (uint32_t i = 0; i < 400 * 512; ++i) clean = clean && qb.videoWord(vunit::kVideoPageWords + i) == 0;
        check(clean && dma.quadsDrawn() == 5 && c.rc == 0x018F && c.ar[0] == 0x66 && c.ar[3] == 0,
              "0x8E6D/0xAECC: um quad de 512x400 com cor 0 limpa a página de render");
    }
}

/// TexturePack: BMP 8 bits 256 x 256 (re-skin indexado)
void testTexturePack() {
    {
        std::vector<uint8_t> px(video::TexturePack::kPageBytes);
        for (std::size_t i = 0; i < px.size(); ++i) px[i] = static_cast<uint8_t>((i * 7 + (i >> 8)) & 0xFF);
        std::array<uint32_t, 256> rgb{};
        for (uint32_t i = 0; i < 256; ++i) rgb[i] = i * 0x010101u;
        const std::string dir = tempPath("texture_pack_test").string();
        std::filesystem::create_directories(dir);
        const bool wrote = video::TexturePack::writeBmp8(dir + "/" + video::TexturePack::pageName(5), px.data(), rgb);
        video::TexturePack pack;
        const int loaded = pack.loadDir(dir);
        uint8_t a = 0, b = 0, c = 0;
        const bool in5 = pack.texel(5 * video::TexturePack::kPageBytes + 0x1234, a) &&
                         pack.texel(5 * video::TexturePack::kPageBytes + 0xFFFF, b);
        const bool notIn4 = !pack.texel(4 * video::TexturePack::kPageBytes + 0x1234, c);
        // Mesma página como BMP de cima para baixo (altura negativa).
        std::vector<char> raw;
        {
            std::ifstream in(dir + "/page_05.bmp", std::ios::binary);
            raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const uint32_t off = 14 + 40 + 1024;
        std::vector<char> td(raw.begin(), raw.begin() + off);
        const int32_t neg = -256;
        std::memcpy(td.data() + 22, &neg, 4);  // x86/ARM little-endian
        for (int row = 0; row < 256; ++row) td.insert(td.end(), raw.begin() + off + (255 - row) * 256, raw.begin() + off + (256 - row) * 256);
        { std::ofstream(dir + "/page_06.bmp", std::ios::binary).write(td.data(), static_cast<std::streamsize>(td.size())); }
        video::TexturePack pack2;
        uint8_t d = 0;
        const bool topDown = pack2.loadDir(dir) == 2 && pack2.texel(6 * video::TexturePack::kPageBytes + 0x1234, d) && d == px[0x1234];
        // Página no formato errado (cabeçalho de BMP 24 bits): recusada e listada.
        std::vector<char> rgb24(raw);
        rgb24[28] = 24;
        { std::ofstream(dir + "/page_07.bmp", std::ios::binary).write(rgb24.data(), static_cast<std::streamsize>(rgb24.size())); }
        video::TexturePack pack3;
        const bool rejects = pack3.loadDir(dir) == 2 && !pack3.hasPage(7) && pack3.rejected().size() == 1 &&
                             pack3.rejected()[0] == "page_07.bmp";
        std::filesystem::remove_all(dir);
        check(wrote && loaded == 1 && in5 && a == px[0x1234] && b == px[0xFFFF] && notIn4 && topDown && rejects,
              "TexturePack: page_NN.bmp (8 bits, 256x256, de baixo p/ cima ou de cima p/ baixo) substitui só a página NN; "
              "formato errado é listado em rejected()");
    }
}

} // namespace

void runVideoTests() {
    testLzwSynthetic();
    testSkyProjection();
    testTextOverlay();
    testBasicPalettes();
    testPaletteSlots();
    testVideoMemory();
    testQuadDma();
    testTexturePack();
}
