/**
 * @file audio_manager.cpp
 * @brief Implementação do AudioManager.
 */
#include "core/audio/audio_manager.hpp"

#include <algorithm>
#include <cstdio>

namespace rea::core::audio {

bool AudioManager::load(const ResourceManager& res, MemoryBus& bus) {
    const auto src = res.openRom("cusa.u2");
    if (!src || !board_.loadRoms(*src)) return false;
    bus.attachDevice(&port_);
    return true;
}

void AudioManager::HostPort::ioWrite(uint32_t addr, uint32_t value) {
    if (addr == dcs::kHostDataPort) {
        am.hostByte(static_cast<uint8_t>(value));  // midvunit: dcs data_w(data & 0xFF)
        return;
    }
    if (addr == dcs::kHostControlPort && (value & 2u) == 0) {  // reset: recomeça a montagem das palavras
        am.have_high_ = false;
        am.expect_param_ = false;
    }
    am.board_.ioWrite(addr, value);
}

void AudioManager::hostByte(uint8_t b) {
    if (overrides_.empty()) {  // sem sons externos: byte direto para a placa, como no hardware
        board_.writeData(b);
        return;
    }
    if (!have_high_) {
        high_ = b;
        have_high_ = true;
        return;
    }
    have_high_ = false;
    const auto word = static_cast<uint16_t>(high_ << 8 | b);
    bool forward = true;
    if (expect_param_) {
        expect_param_ = false;
        if (param_is_volume_) overrides_.setMasterVolume(word >> 8);
    } else if ((word >= 0x55AA && word <= 0x55AF) || word == 0x55CC) {
        expect_param_ = true;
        param_is_volume_ = word == 0x55AA;
    } else if (word == 0x0000) {
        overrides_.stopAll();
    } else if (overrides_.has(word)) {
        overrides_.trigger(word);
        ++overridden_;
        forward = false;
    }
    if (forward) {
        board_.writeData(high_);
        board_.writeData(b);
    }
}

void AudioManager::frame(double refreshHz) {
    if (!board_.loaded() || refreshHz <= 0.0) return;
    acc_ += static_cast<double>(kSampleRate) / refreshHz;
    const auto n = static_cast<std::size_t>(acc_);
    acc_ -= static_cast<double>(n);
    pcm_.clear();
    board_.run(n, pcm_);
    overrides_.mix(pcm_.data(), pcm_.size());
    if (out_) out_->queue(pcm_.data(), pcm_.size());
    if (recording_ && !recording_truncated_) {
        const std::size_t room = kMaxRecordedSamples - recorded_.size();
        const std::size_t take = std::min(room, pcm_.size());
        recorded_.insert(recorded_.end(), pcm_.begin(), pcm_.begin() + static_cast<std::ptrdiff_t>(take));
        recording_truncated_ = take < pcm_.size();
    }
}

bool AudioManager::writeWav(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    // Campos do cabeçalho e amostras em little-endian, byte a byte (vale em qualquer CPU).
    auto u32 = [&](uint32_t v) {
        const unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                                    static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
        std::fwrite(b, 1, 4, f);
    };
    auto u16 = [&](uint16_t v) {
        const unsigned char b[2] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)};
        std::fwrite(b, 1, 2, f);
    };
    const auto bytes = static_cast<uint32_t>(recorded_.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);               // tamanho do bloco fmt
    u16(1);                // PCM
    u16(1);                // mono
    u32(kSampleRate);
    u32(kSampleRate * 2);  // bytes por segundo
    u16(2);                // alinhamento de bloco
    u16(16);               // bits por amostra
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    // Amostras em blocos (antes, um fwrite por amostra).
    std::vector<unsigned char> buf;
    buf.reserve(64 * 1024);
    for (std::size_t i = 0; i < recorded_.size(); ++i) {
        const auto v = static_cast<uint16_t>(recorded_[i]);
        buf.push_back(static_cast<unsigned char>(v));
        buf.push_back(static_cast<unsigned char>(v >> 8));
        if (buf.size() >= 64 * 1024 || i + 1 == recorded_.size()) {
            std::fwrite(buf.data(), 1, buf.size(), f);
            buf.clear();
        }
    }
    // Um erro no meio (disco cheio) fica marcado no FILE; o fclose sozinho pode não acusá-lo.
    const bool write_error = std::ferror(f) != 0;
    return std::fclose(f) == 0 && !write_error;
}

} // namespace rea::core::audio
