#pragma once
/**
 * @file link_shm.hpp
 * @brief Cabo de link entre dois processos `rea_cruisn` por um arquivo mapeado em memória.
 *
 * @details O arquivo (`--link-file`, padrão `build/link.shm`) guarda, para cada lado (0 = Master, 1 = Slave):
 * as linhas que a placa dirige, o relógio de instruções e a hora do último sinal de vida. Por ser um arquivo
 * comum, funciona entre dois containers que montam o mesmo diretório. Linux/macOS usam `mmap`; Windows usa
 * `CreateFileMapping`/`MapViewOfFile` sobre o mesmo arquivo.
 *
 * **Sincronização** (a mesma ideia da co-simulação dos testes): o relógio avança com as instruções das esperas
 * ocupadas (`advance()`) e completa 431 600 por quadro (`frameDone()`), exceto quando o quadro termina dentro de
 * uma espera do link (aí completar faria o outro lado estourar o timeout no meio do pacote; antes disso ~1 % das
 * trocas falhava). Ao ler a porta, o processo espera o
 * relógio do outro alcançar o seu, e depois de cada pausa quem ficou à frente espera o outro; assim nenhum lado
 * se adianta mais que uma pausa e as leituras de um lado veem as escritas que o outro fez "antes" no tempo
 * emulado. Se o outro lado não der sinal de vida por 1 s, a leitura segue com as linhas em 0
 * (cabo desligado). Um lado aberto (ou reaberto) com o outro já rodando começa no relógio do outro, então não
 * é preciso iniciar os dois juntos.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/io/link_port.hpp"

namespace rea::platform {

/// @brief Cabo do link entre duas cabines simulado por memória compartilhada (dois processos na mesma máquina). @details Cada lado publica as linhas da porta e um relógio em instruções; as leituras esperam o relógio do outro lado para manter os dois em passo (lockstep).
class ShmLinkWire final : public core::io::ILinkWire {
public:
    static constexpr uint64_t kInstructionsPerFrame = 431600;  ///< Instruções do C31 por quadro, usadas como relógio do link.

    /// @param side 0 = Master, 1 = Slave.
    /// @param path Arquivo mapeado em memória compartilhada entre as duas cabines.
    ShmLinkWire(const std::string& path, int side);
    ~ShmLinkWire() override;
    ShmLinkWire(const ShmLinkWire&) = delete;
    ShmLinkWire& operator=(const ShmLinkWire&) = delete;

    /// @return true se o arquivo compartilhado foi mapeado.
    bool ok() const { return shared_ != nullptr; }

    void publish(uint16_t lines) override;
    uint16_t peerLines() override;

    /// @brief Instruções gastas numa espera ocupada.
    void advance(uint64_t instructions);
    /**
     * @brief Fim de um quadro: completa o relógio até 431 600 instruções no quadro.
     * @param topUp false quando o quadro terminou dentro de uma espera do link (o gancho do `FrameBusyWait`
     *        cedeu no meio dela): o relógio já avançou pelas instruções esperadas, e completá-lo faria o outro
     *        lado ler a porta um quadro inteiro sem o byte seguinte chegar (timeout no meio do pacote).
     */
    void frameDone(bool topUp = true);

    uint64_t waits() const { return waits_; }          ///< leituras que precisaram esperar o outro lado
    uint64_t disconnects() const { return disconnects_; }  ///< leituras com o outro lado sem sinal de vida

private:
    /// Estado publicado por uma das cabines.
    struct Side {
        std::atomic<uint64_t> clock;  ///< Relógio da cabine, em instruções.
        std::atomic<uint64_t> alive_ns;  ///< Último batimento (`nowNs`), para detectar o outro lado desligado.
        std::atomic<uint32_t> lines;  ///< Linhas da porta do link publicadas.
        std::atomic<uint32_t> pad;  ///< Alinhamento.
    };
    /// Layout do arquivo compartilhado.
    struct Shared {
        std::atomic<uint32_t> magic;  ///< `kMagic` quando o bloco está iniciado.
        uint32_t pad;  ///< Alinhamento.
        Side side[2];  ///< Lado 0 = Master, lado 1 = Slave.
    };

    /// Atualiza o batimento deste lado.
    void beat();
    /// Espera o relógio do outro lado alcançar o deste (vale para a leitura da porta e para depois de uma pausa).
    /// @return false se o outro lado não dá sinal de vida.
    bool waitPeer();

    void* mapFile(const std::string& path, std::size_t size);
    /// Desfaz o mapeamento e fecha o arquivo.
    void unmapFile();

    Shared* shared_ = nullptr;  ///< Bloco mapeado; nullptr se a abertura falhou.
#ifdef _WIN32
    void* file_ = nullptr;        ///< Win32: HANDLE do arquivo
    void* mapping_ = nullptr;     ///< Win32: HANDLE do mapeamento
#else
    int fd_ = -1;                 ///< POSIX
#endif
    int me_ = 0;  ///< Lado desta cabine (0 = Master, 1 = Slave).
    uint64_t in_frame_ = 0;  ///< instruções de espera já contadas neste quadro
    uint64_t waits_ = 0;  ///< Leituras que precisaram esperar o outro lado.
    uint64_t disconnects_ = 0;  ///< Leituras com o outro lado sem sinal de vida.
};

} // namespace rea::platform
