#pragma once
/**
 * @file sound_overrides.hpp
 * @brief Sons externos (re-skin de áudio): WAVs que substituem comandos da placa DCS.
 *
 * @details A CPU principal só envia comandos de 16 bits à placa DCS; cada som, fala ou música é um comando
 * (ex.: 0x0085 = ficha). Um arquivo em `assets/audio/` substitui o comando correspondente:
 * - `cmd_XXXX.wav` (XXXX em hexadecimal, 4 dígitos) toca uma vez;
 * - `cmd_XXXX_loop.wav` toca em laço (músicas) até o comando 0x0000 (parar tudo) ou outro laço começar.
 *
 * O WAV deve ser PCM de 8 ou 16 bits, mono ou estéreo, em qualquer taxa: é convertido para mono a 31 250 Hz
 * (interpolação linear) na carga. O volume geral enviado pelo jogo (`0x55AA`, ajuste VOLUME do menu) também
 * vale para os sons externos.
 */

#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

namespace rea::core::audio {

/**
 * @brief Lê um WAV PCM (8/16 bits, 1/2 canais) e converte para mono 16 bits a @p rate Hz.
 * @return false se o arquivo não existir ou não for PCM suportado.
 */
bool loadWavMono(const std::filesystem::path& file, uint32_t rate, std::vector<int16_t>& out);

/// @brief Sons externos (`assets/audio/cmd_XXXX[_loop].wav`) que substituem comandos da placa DCS.
class SoundOverrides {
public:
    /// @brief Carrega os `cmd_XXXX[_loop].wav` de @p dir. @return Número de comandos substituídos.
    int loadDir(const std::filesystem::path& dir, uint32_t rate);
    /// @brief Instala um clip direto (testes).
    void add(uint16_t command, std::vector<int16_t> pcm, bool loop);

    /// @return true se nenhum som foi carregado.
    bool empty() const { return clips_.empty(); }
    /// @return true se o comando @p command tem som externo.
    bool has(uint16_t command) const { return clips_.count(command) != 0; }

    /// @brief Começa a tocar o clip do comando (um laço novo substitui o laço anterior).
    void trigger(uint16_t command);
    /// @brief Comando 0x0000: para todas as vozes.
    void stopAll() { voices_.clear(); }
    /// @brief Volume geral do jogo (0..255, palavra seguinte a `0x55AA`).
    void setMasterVolume(uint32_t v) { volume_ = v > 255 ? 255 : v; }

    /// @brief Soma as vozes ativas em @p pcm (saturando).
    void mix(int16_t* pcm, std::size_t n);
    /// @return Vozes tocando.
    std::size_t activeVoices() const { return voices_.size(); }

private:
    /// Som carregado de um WAV.
    struct Clip {
        std::vector<int16_t> pcm;  ///< Amostras mono 16 bits na taxa da placa.
        bool loop = false;  ///< Toca em laço.
    };
    /// Voz tocando.
    struct Voice {
        const Clip* clip;  ///< Som.
        std::size_t pos;  ///< Próxima amostra.
    };
    std::map<uint16_t, Clip> clips_;  ///< Sons por comando.
    std::vector<Voice> voices_;  ///< Vozes ativas.
    uint32_t volume_ = 255;  ///< Volume geral (0–255).
};

} // namespace rea::core::audio
