/**
 * @file test_audio.cpp
 * @brief Som: driver de comandos, núcleo ADSP-21xx, sons externos, AudioManager e a placa DCS.
 */
#include "test_support.hpp"

using namespace rea::core;
using namespace rea::test;

namespace {

/// Driver de som: 0x91E0 (prioridade, sequência), 0x92B3 (passos e reinício), 0x910F
void testSoundDriver() {
    {
        MemoryBus sb;
        RecordingSubroutines pend;
        SubroutineDispatcher d(pend);
        audio::registerSoundCommands(d);
        std::vector<uint32_t> sent;
        d.add(0x9211, [&sent](Tms32031State& t, MemoryBus&, ISubroutines&) { sent.push_back(t.ar[2]); t.ar[2] &= 0xFF; });
        d.add(0x99E9, [](Tms32031State& t, MemoryBus&, ISubroutines&) { t.r[0] = 0x80; });
        sb.writeWord(0x9175, 0xE8F0);
        sb.writeWord(0x90A6, 0x8974);
        // som 5: canal 1, prioridade 0x10; passos (cmd 0x123, 2 quadros), (cmd 0x456, 1 quadro), fim
        const uint32_t snd = 0x8974 + 5;
        sb.writeWord(snd, 0x0110); sb.writeWord(snd + 1, (2u << 16) | 0x123);
        sb.writeWord(snd + 2, 0x0110); sb.writeWord(snd + 3, (1u << 16) | 0x456);
        sb.writeWord(snd + 4, 0);
        const uint32_t ch1 = 0xE8F0 + 9;
        sb.writeWord(ch1 + 5, 0xFF);
        Tms32031State c;
        c.ar[2] = 5;
        d.call(0x91E0, c, sb);
        const bool started = sent == std::vector<uint32_t>{0x123} && sb.readWord(ch1) == 0x0110 &&
                             sb.readWord(ch1 + 2) == 2 && sb.readWord(ch1 + 6) == 5 && c.ar[2] != 5;
        // som 6 no mesmo canal, prioridade menor: recusado
        sb.writeWord(0x8974 + 6, 0x0105);
        c.ar[2] = 6;
        d.call(0x91E0, c, sb);
        check(started && sent.size() == 1, "0x91E0: toca no canal dos bits 8–11 e recusa prioridade menor");
        for (int i = 0; i < 3; ++i) d.call(0x92B3, c, sb);
        check(sent == std::vector<uint32_t>({0x123, 0x456}) && sb.readWord(ch1) == 0 && sb.readWord(ch1 + 2) == 0,
              "0x92B3: avança a sequência pelos quadros e encerra no terminador");

        sent.clear();
        c.r[1] = 0x80;
        d.call(0x910F, c, sb);
        check(sent == std::vector<uint32_t>({0x55AA, 0x807F}) && sb.readWord(0xE917) == 0,
              "0x910F: volume geral = 0x55AA, (v << 8) | ~v");

        sent.clear();
        d.call(0x92AD, c, sb);
        for (int i = 0; i < 5; ++i) d.call(0x92B3, c, sb);
        check(sb.readWord(0x809A79) == 0 && sb.readWord(0x809A7A) == 6 && sent == std::vector<uint32_t>({0x55AA, 0x807F}),
              "0x92AD/0x92B3: reinício em 5 quadros termina com o volume do item 8");
    }
}

/// DCS: núcleo ADSP-21xx
void testAdspCore() {
    {
        struct NullMem : audio::dcs::IAdspMemory {
            uint16_t dmRead(uint32_t) override { return 0; }
            void dmWrite(uint32_t, uint16_t) override {}
            uint32_t pmRead(uint32_t) override { return 0; }
            void pmWrite(uint32_t, uint32_t) override {}
        } mem;
        audio::dcs::AdspCore a(mem);
        a.reset();
        a.setMstat(0x0000);
        a.toAr(a.aluAdd(0x7FFF, 0x0001));
        const bool ovf = a.ar == 0x8000 && !a.eq() && (a.astat & 0x04) && a.lt() == false;
        a.toAr(a.aluSub(0x0003, 0x0005));
        const bool neg = a.ar == 0xFFFE && a.lt() && !a.acFlag();
        a.mul(0x4000, 0x4000, audio::dcs::AdspCore::Mul::SS);  // 0,5 · 0,5 = 0,25 (fracionário)
        const bool frac = a.mr1() == 0x2000 && a.mr0() == 0;
        a.lshift(0x00F0, -4, false);
        const bool sh = a.sr0() == 0x000F;
        check(ovf && neg && frac && sh, "ADSP-2105: flags da ALU (V/N/C), MAC fracionário e LSHIFT");
        a.ashift(0x8000, -4, true);  // HI: −0x80000000 >> 4
        const bool ash_hi = a.sr1() == 0xF800 && a.sr0() == 0x0000;
        a.ashift(0xFFF0, -2, false);  // LO: −16 >> 2 = −4
        const bool ash_lo = a.sr1() == 0xFFFF && a.sr0() == 0xFFFC;
        a.ashift(0x8000, -40, true);  // além de 31: só o sinal
        const bool ash_far = a.sr1() == 0xFFFF && a.sr0() == 0xFFFF;
        a.ashift(0x4000, -1, true);  // positivo
        const bool ash_pos = a.sr1() == 0x2000 && a.sr0() == 0x0000;
        check(ash_hi && ash_lo && ash_far && ash_pos, "ADSP-2105: ASHIFT à direita estende o sinal (HI, LO, ≥ 32 bits)");
    }
}

/// Sons externos: WAV -> 31 250 Hz mono e filtro dos comandos da placa
void testSoundOverrides() {
    {
        const std::string path = tempPath("sound_override_test.wav").string();
        {
            // 8000 Hz, estéreo, 16 bits, 800 quadros: esquerda 1000, direita 3000 (média 2000)
            std::ofstream f(path, std::ios::binary);
            auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) f.put(static_cast<char>(v >> (8 * i))); };
            auto u16 = [&](uint16_t v) { f.put(static_cast<char>(v)); f.put(static_cast<char>(v >> 8)); };
            f.write("RIFF", 4); u32(36 + 3200); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32(8000); u32(32000);
            u16(4); u16(16); f.write("data", 4); u32(3200);
            for (int i = 0; i < 800; ++i) { u16(1000); u16(3000); }
        }
        std::vector<int16_t> pcm;
        const bool ok = audio::loadWavMono(path, 31250, pcm);
        std::remove(path.c_str());
        check(ok && pcm.size() == 3122 && pcm.front() == 2000 && pcm.back() == 2000,
              "Sons externos: WAV estéreo 8 kHz vira mono a 31 250 Hz (interpolação linear)");
        {
            // Bloco com tamanho declarado 0xFFFFFFF8: somado em 32 bits dava 0 e o laço não avançava (travava o boot).
            std::ofstream f(path, std::ios::binary);
            auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) f.put(static_cast<char>(v >> (8 * i))); };
            f.write("RIFF", 4); u32(100); f.write("WAVE", 4); f.write("JUNK", 4); u32(0xFFFFFFF8u);
            for (int i = 0; i < 16; ++i) f.put(0);
        }
        const bool bad = audio::loadWavMono(path, 31250, pcm);
        std::remove(path.c_str());
        check(!bad, "Sons externos: WAV com bloco de tamanho gigante é recusado (sem laço infinito)");

        audio::AudioManager am;
        am.overrides().add(0x0085, std::vector<int16_t>(100, 1000), false);
        for (uint8_t b : {0x55, 0xAA, 0xC8, 0x37,   // volume geral 200 (parâmetro segue para a placa)
                          0x55, 0xAB, 0x00, 0x85,   // parâmetro igual a um comando substituído: segue
                          0x00, 0x85,               // ficha: som externo, não vai para a placa
                          0x01, 0x90})              // start: vai para a placa
            am.hostByte(b);
        std::vector<int16_t> out(10, 0);
        am.overrides().mix(out.data(), out.size());
        check(am.board().latchPending() == 10 && am.overridden() == 1 && am.overrides().activeVoices() == 1 &&
                  out[0] == 1000 * 200 / 255,
              "Sons externos: só o comando substituído deixa de ir para a placa; volume de 0x55AA aplicado");
        am.hostByte(0x00);
        am.hostByte(0x00);
        check(am.overrides().activeVoices() == 0, "Sons externos: 0x0000 para as vozes externas");
    }
}

/// AudioManager: amostras por quadro e WAV little-endian (com as ROMs de som)
void testAudioManager() {
    {
        MemoryBus m;
        audio::AudioManager am;
        if (am.load(ResourceManager(REA_SOURCE_DIR), m)) {
            struct Sink : IAudio {
                std::size_t n = 0;
                void queue(const int16_t*, std::size_t c) override { n += c; }
            } sink;
            am.setOutput(&sink);
            am.setRecording(true);
            for (int i = 0; i < 100; ++i) am.frame(57.9);  // 100 quadros: 100 · 31250 / 57,9 ≈ 53972 amostras
            const std::string path = tempPath("audio_manager_test.wav").string();
            const bool wrote = am.writeWav(path);
            unsigned char h[44] = {};
            if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
                const std::size_t got = std::fread(h, 1, sizeof h, f);
                (void)got;
                std::fclose(f);
            }
            std::remove(path.c_str());
            auto le32 = [&](int o) { return h[o] | h[o + 1] << 8 | h[o + 2] << 16 | static_cast<uint32_t>(h[o + 3]) << 24; };
            check(sink.n == 53972 && am.recorded().size() == sink.n && wrote && std::memcmp(h, "RIFF", 4) == 0 &&
                      le32(24) == 31250 && le32(40) == sink.n * 2,
                  "AudioManager: 31250/57,9 amostras por quadro (resto acumulado) e WAV com cabeçalho little-endian");
        }
    }
}

/// DCS: placa com as ROMs de som (opcional)
void testDcsBoard() {
    if (auto dir = ResourceManager(REA_SOURCE_DIR).findRomDir("cusa.u2")) {
        audio::dcs::DcsBoard dcs;
        check(dcs.loadRoms(*dir), "DCS: carrega cusa.u2..u9");
        std::vector<int16_t> pcm;
        dcs.run(31250 * 5, pcm);  // 5 s: autoteste (bipe) e boot do residente
        std::size_t nz = 0;
        int peak = 0;
        for (int16_t v : pcm) {
            if (v) ++nz;
            peak = std::max(peak, std::abs(static_cast<int>(v)));
        }
        std::printf("DCS: boots=%u banco=%u quadros=%llu amostras!=0: %zu pico=%d\n", dcs.boots(), dcs.lastBootBank(),
                    static_cast<unsigned long long>(dcs.framesDecoded()), nz, peak);
        check(dcs.boots() == 2 && dcs.lastBootBank() == 1 && nz > 0 && dcs.framesDecoded() > 0,
              "DCS: autoteste toca o bipe e faz o boot do residente (banco 1), que decodifica quadros");
        // Comandos: palavra de 16 bits, byte alto primeiro.
        int best = 0;
        uint16_t best_cmd = 0;
        for (uint16_t cmd = 1; cmd < 0x40; ++cmd) {
            dcs.writeData(static_cast<uint8_t>(cmd >> 8));
            dcs.writeData(static_cast<uint8_t>(cmd));
            pcm.clear();
            dcs.run(31250 / 4, pcm);
            int pk = 0;
            for (int16_t v : pcm) pk = std::max(pk, std::abs(static_cast<int>(v)));
            if (pk > best) best = pk, best_cmd = cmd;
        }
        std::printf("DCS: maior pico apos comandos 1..0x3F = %d (comando 0x%04X), quadros=%llu\n", best, best_cmd,
                    static_cast<unsigned long long>(dcs.framesDecoded()));
        check(best > 256, "DCS: comandos de som produzem áudio audível");

        // Byte no trinco durante a espera inicial: o autoteste é pulado (sem bipe) e o banco 1 sobe logo.
        audio::dcs::DcsBoard quick;
        quick.loadRoms(*dir);
        quick.setReset(true);
        quick.setReset(false);
        pcm.clear();
        quick.run(64, pcm);       // o DSP já passou de 0x03A (que descarta o trinco)
        quick.writeData(0x00);
        quick.run(31250 / 2, pcm);
        bool silent = true;
        for (int16_t v : pcm) silent = silent && v == 0;
        check(quick.boots() == 2 && quick.lastBootBank() == 1 && quick.framesDecoded() > 0 && silent,
              "DCS: byte recebido na espera pula o autoteste (0x0F4: boot do banco 1, sem bipe)");
    }
}

} // namespace

void runAudioTests() {
    testSoundDriver();
    testAdspCore();
    testSoundOverrides();
    testAudioManager();
    testDcsBoard();
}
