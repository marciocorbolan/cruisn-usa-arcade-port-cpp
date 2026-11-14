#pragma once
/**
 * @file link_port.hpp
 * @brief Placa de comunicação entre gabinetes (`0x997000`), semântica do MAME `comcs`.
 *
 * @details
 * - Escrita em `+0`: `comm_data = valor >> 16`.
 * - Escrita em `+1`: `comm_flags = (valor >> 24) & 0xE0`.
 *   - `0x20` (COMCOE): a placa dirige as linhas de controle `(comm_data >> 4) & 0xF00`.
 *   - `0x40` (COMDOE): a placa dirige as 8 linhas de dados.
 * - Leitura de `+0`: o barramento do cabo, sem as linhas que a própria placa
 *   dirige, nos bits 16–31. As demais posições leem 0.
 *
 * Divisão observada na ROM (`0x7F5D`):
 * - o Master escreve `0xCxxx` e dirige as linhas 0x800/0x400 (strobe = 0x400);
 * - o Slave escreve `0x3xxx` e dirige 0x200/0x100 (strobe = 0x200).
 *
 * Sozinha, a porta se comporta como no MAME: o barramento é a própria saída,
 * e como as linhas próprias são mascaradas, a leitura dá 0. Com um
 * `LinkCable`, o barramento é o OR das saídas de todas as placas conectadas.
 */

#include <cstdint>
#include <vector>

#include "core/memory_bus.hpp"

namespace rea::core::io {

class LinkCable;

/**
 * @brief Fio para outro gabinete fora deste processo (ex.: memória compartilhada).
 * @details A porta publica as linhas que dirige a cada escrita e, a cada leitura, pede as linhas do outro lado.
 * Quem implementa decide a sincronização (o port usa o relógio de instruções das esperas ocupadas).
 */
class ILinkWire {
public:
    virtual ~ILinkWire() = default;
    /// @brief As linhas dirigidas por esta placa mudaram.
    virtual void publish(uint16_t lines) = 0;
    /// @return Linhas dirigidas pelo outro gabinete (0 se desconectado).
    virtual uint16_t peerLines() = 0;
};

/**
 * @brief Uma placa de link mapeada em `0x997000–0x997008`.
 */
class LinkPort : public IIoDevice {
public:
    bool ioRead(uint32_t addr, uint32_t& value) override;
    void ioWrite(uint32_t addr, uint32_t value) override;

    /// @return Linhas que esta placa dirige no barramento (MAME comm_bus_out).
    uint16_t busOut() const;
    /// @brief Conecta a um cabo (nullptr = sozinha, loopback do MAME).
    void connect(LinkCable* cable) { cable_ = cable; }
    /// @brief Conecta a um fio externo (outro processo); nullptr desliga.
    void connectWire(ILinkWire* wire) { wire_ = wire; }

    uint16_t data() const { return comm_data_; }   ///< Último dado escrito (16 bits).
    uint8_t flags() const { return comm_flags_; }  ///< COMCOE/COMDOE atuais.

private:
    uint16_t comm_data_ = 0;  ///< Último dado escrito (16 bits).
    uint8_t comm_flags_ = 0;  ///< Flags COMCOE/COMDOE.
    LinkCable* cable_ = nullptr;  ///< Cabo local, ou nullptr.
    ILinkWire* wire_ = nullptr;  ///< Fio externo (outro processo), ou nullptr.
};

/**
 * @brief Cabo que liga várias placas: barramento = OR das saídas.
 * @note Não é thread-safe; quem simula várias máquinas serializa os acessos.
 */
class LinkCable {
public:
    /// @brief Liga @p port ao cabo (não assume a posse).
    void attach(LinkPort& port) {
        ports_.push_back(&port);
        port.connect(this);
    }
    /// @return OR das linhas dirigidas por todas as placas.
    uint16_t bus() const {
        uint16_t v = 0;
        for (const LinkPort* p : ports_) v = static_cast<uint16_t>(v | p->busOut());
        return v;
    }

private:
    std::vector<LinkPort*> ports_;  ///< Placas ligadas ao cabo.
};

} // namespace rea::core::io
