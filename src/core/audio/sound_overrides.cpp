/**
 * @file sound_overrides.cpp
 * @brief Carga de WAV e mixagem dos sons externos.
 */
#include "core/audio/sound_overrides.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace rea::core::audio {

namespace {

/// Lê um inteiro de 32 bits little-endian de @p b no deslocamento @p o.
uint32_t le32(const std::vector<uint8_t>& b, std::size_t o) {
    return static_cast<uint32_t>(b[o]) | static_cast<uint32_t>(b[o + 1]) << 8 | static_cast<uint32_t>(b[o + 2]) << 16 |
           static_cast<uint32_t>(b[o + 3]) << 24;
}
/// Lê um inteiro de 16 bits little-endian de @p b no deslocamento @p o.
uint16_t le16(const std::vector<uint8_t>& b, std::size_t o) { return static_cast<uint16_t>(b[o] | b[o + 1] << 8); }

} // namespace

bool loadWavMono(const std::filesystem::path& file, uint32_t rate, std::vector<int16_t>& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    const std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::string(b.begin(), b.begin() + 4) != "RIFF" || std::string(b.begin() + 8, b.begin() + 12) != "WAVE")
        return false;
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t src_rate = 0;
    std::size_t data = 0, data_len = 0;
    for (std::size_t o = 12; o + 8 <= b.size();) {  // percorre os blocos (campos little-endian)
        const std::string id(b.begin() + static_cast<std::ptrdiff_t>(o), b.begin() + static_cast<std::ptrdiff_t>(o + 4));
        const uint32_t len = le32(b, o + 4);
        if (id == "fmt " && len >= 16 && o + 24 <= b.size()) {
            format = le16(b, o + 8);
            channels = le16(b, o + 10);
            src_rate = le32(b, o + 12);
            bits = le16(b, o + 22);
        } else if (id == "data") {
            data = o + 8;
            data_len = std::min<std::size_t>(len, b.size() - data);
        }
        // Em size_t: um tamanho perto de 4 GB somado em 32 bits daria 0 e o laço não avançaria.
        const std::size_t next = o + 8 + static_cast<std::size_t>(len) + (len & 1u);
        if (next <= o || next > b.size() + 1) break;  // bloco além do fim (o +1 aceita o enchimento final ausente)
        o = next;
    }
    if (format != 1 || (bits != 8 && bits != 16) || channels < 1 || channels > 2 || src_rate == 0 || data == 0) return false;
    const std::size_t frame = static_cast<std::size_t>(channels) * (bits / 8);
    const std::size_t frames = data_len / frame;
    std::vector<int32_t> mono(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        int32_t sum = 0;
        for (uint16_t c = 0; c < channels; ++c) {
            const std::size_t p = data + i * frame + c * (bits / 8u);
            sum += bits == 16 ? static_cast<int16_t>(le16(b, p)) : (static_cast<int32_t>(b[p]) - 128) * 256;
        }
        mono[i] = sum / channels;
    }
    // Reamostragem linear para a taxa da placa.
    out.clear();
    if (frames == 0) return true;
    const double step = static_cast<double>(src_rate) / static_cast<double>(rate);
    const auto count = static_cast<std::size_t>(static_cast<double>(frames - 1) / step) + 1;
    out.reserve(count);
    for (std::size_t k = 0; k < count; ++k) {
        const double t = static_cast<double>(k) * step;
        const auto i = std::min(static_cast<std::size_t>(t), frames - 1);
        const double f = t - static_cast<double>(i);
        const double a = mono[i];
        const double c = i + 1 < frames ? mono[i + 1] : a;
        out.push_back(static_cast<int16_t>(std::clamp(a + (c - a) * f, -32768.0, 32767.0)));
    }
    return true;
}

int SoundOverrides::loadDir(const std::filesystem::path& dir, uint32_t rate) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return 0;
    int n = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        // cmd_XXXX.wav ou cmd_XXXX_loop.wav
        if (name.size() < 12 || name.compare(0, 4, "cmd_") != 0) continue;
        const bool loop = name.size() == 17 && name.compare(8, 9, "_loop.wav") == 0;
        if (!loop && !(name.size() == 12 && name.compare(8, 4, ".wav") == 0)) continue;
        char* end = nullptr;
        const std::string hex = name.substr(4, 4);
        const unsigned long cmd = std::strtoul(hex.c_str(), &end, 16);
        if (end != hex.c_str() + 4) continue;
        std::vector<int16_t> pcm;
        if (!loadWavMono(e.path(), rate, pcm)) continue;
        if (!has(static_cast<uint16_t>(cmd))) ++n;
        add(static_cast<uint16_t>(cmd), std::move(pcm), loop);
    }
    return n;
}

void SoundOverrides::add(uint16_t command, std::vector<int16_t> pcm, bool loop) {
    voices_.clear();  // os ponteiros para clips antigos deixariam de valer
    clips_[command] = Clip{std::move(pcm), loop};
}

void SoundOverrides::trigger(uint16_t command) {
    const auto it = clips_.find(command);
    if (it == clips_.end() || it->second.pcm.empty()) return;
    if (it->second.loop)
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.clip->loop; }),
                      voices_.end());
    voices_.push_back(Voice{&it->second, 0});
}

void SoundOverrides::mix(int16_t* pcm, std::size_t n) {
    if (voices_.empty()) return;
    for (std::size_t i = 0; i < n; ++i) {
        int32_t acc = 0;
        for (Voice& v : voices_) {
            if (v.pos >= v.clip->pcm.size()) {
                if (!v.clip->loop) continue;
                v.pos = 0;
            }
            acc += v.clip->pcm[v.pos++];
        }
        acc = acc * static_cast<int32_t>(volume_) / 255 + pcm[i];
        pcm[i] = static_cast<int16_t>(std::clamp<int32_t>(acc, -32768, 32767));
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return !v.clip->loop && v.pos >= v.clip->pcm.size(); }),
                  voices_.end());
}

} // namespace rea::core::audio
