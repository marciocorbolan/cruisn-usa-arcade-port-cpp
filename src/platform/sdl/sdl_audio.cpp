/**
 * @file sdl_audio.cpp
 * @brief Áudio SDL2: toca a saída da placa DCS (mono, 16 bits, 31 250 Hz).
 */
#include "platform/sdl/sdl_platform.hpp"

#include <iostream>

namespace rea::platform::sdl {

SdlAudio::SdlAudio() {
    SDL_AudioSpec want{};
    want.freq = 31250;          // SPORT1 do ADSP-2105: 10 MHz / 320 (SCLKDIV = 9)
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);  // o SDL converte a taxa se preciso
    if (!device_) std::cerr << "[SDL] Áudio indisponível: " << SDL_GetError() << '\n';
}

SdlAudio::~SdlAudio() {
    if (device_) SDL_CloseAudioDevice(device_);
}

void SdlAudio::queue(const int16_t* samples, std::size_t count) {
    if (!device_ || count == 0) return;
    constexpr Uint32 kBytesPerSecond = 31250 * sizeof(int16_t);
    constexpr Uint32 kLow = kBytesPerSecond / 25;   // ~40 ms: fila curta, risco de faltar som
    constexpr Uint32 kHigh = kBytesPerSecond / 7;   // ~140 ms: fila crescendo (relógio do jogo à frente da placa)
    constexpr Uint32 kMax = kBytesPerSecond / 2;    // ~0,5 s: travada longa; descarta o quadro inteiro
    constexpr std::size_t kEvery = 32;              // ajuste de 1 em 32 amostras (~3 %, inaudível)
    const Uint32 queued = SDL_GetQueuedAudioSize(device_);
    if (queued > kMax) return;
    // O relógio do jogo e o da placa de som do PC nunca batem exatamente; antes, ao passar de 0,2 s o bloco
    // inteiro era jogado fora (um estalo de ~17 ms). Agora a fila é corrigida aos poucos: tira ou repete uma
    // amostra a cada kEvery, sem buraco no som.
    const bool shrink = queued > kHigh;
    const bool stretch = queued < kLow && SDL_GetAudioDeviceStatus(device_) == SDL_AUDIO_PLAYING;
    if (shrink || stretch) {
        adjusted_.clear();
        adjusted_.reserve(count + count / kEvery + 1);
        for (std::size_t i = 0; i < count; ++i) {
            const bool mark = i % kEvery == kEvery - 1;
            if (shrink && mark) continue;
            adjusted_.push_back(samples[i]);
            if (stretch && mark) adjusted_.push_back(samples[i]);
        }
        samples = adjusted_.data();
        count = adjusted_.size();
    }
    SDL_QueueAudio(device_, samples, static_cast<Uint32>(count * sizeof(int16_t)));
    SDL_PauseAudioDevice(device_, 0);
}

} // namespace rea::platform::sdl
