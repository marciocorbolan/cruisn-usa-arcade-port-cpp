#pragma once
/**
 * @file subroutines.hpp
 * @brief Despacho de sub-rotinas da ROM (`CALL addr`) para código C++ traduzido.
 */

#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "core/memory_bus.hpp"
#include "core/tms32031_state.hpp"

namespace rea::core {

/**
 * @brief Executor de `CALL addr`.
 *
 * @details Toda rotina traduzida chama outras por esta interface, nunca
 * diretamente. Assim a tradução avança aos poucos: o que já foi traduzido é
 * executado e o que falta é registrado, sem alterar o código que chama.
 */
class ISubroutines {
public:
    virtual ~ISubroutines() = default;
    /**
     * @brief Executa a sub-rotina em @p addr.
     * @param addr Endereço da sub-rotina na ROM.
     * @param cpu Registradores (entrada e saída, ex.: R0 de retorno, flag C).
     * @param bus Barramento de memória.
     */
    virtual void call(uint32_t addr, Tms32031State& cpu, MemoryBus& bus) = 0;
};

/**
 * @brief Apenas registra as chamadas (sub-rotinas ainda não traduzidas).
 */
class RecordingSubroutines : public ISubroutines {
public:
    /// Chamada registrada: endereço + AR2 de entrada (parâmetro mais usado).
    struct Entry {
        uint32_t addr;  ///< Endereço chamado.
        uint32_t ar2;  ///< AR2 na entrada.
        uint32_t r0;  ///< R0 na entrada.
        uint32_t r1;  ///< R1 na entrada.
    };
    void call(uint32_t addr, Tms32031State& cpu, MemoryBus&) override {
        calls.push_back({addr, cpu.ar[2], cpu.r[0], cpu.r[1]});
    }
    std::vector<Entry> calls; ///< Ordem das chamadas.
};

/**
 * @brief Conta chamadas por endereço (uso contínuo, ex.: a cada quadro).
 */
class CountingSubroutines : public ISubroutines {
public:
    void call(uint32_t addr, Tms32031State&, MemoryBus&) override { ++counts[addr]; }
    std::unordered_map<uint32_t, uint64_t> counts; ///< endereço → número de chamadas.
};

/**
 * @brief Tabela endereço → rotina C++, com fallback para as não traduzidas.
 */
class SubroutineDispatcher : public ISubroutines {
public:
    /// Assinatura de uma rotina traduzida; @p subs permite chamadas aninhadas.
    using Routine = std::function<void(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs)>;

    /**
     * @brief Constrói o despachante.
     * @param fallback Destino das chamadas a endereços ainda não traduzidos.
     */
    explicit SubroutineDispatcher(ISubroutines& fallback) : fallback_(fallback) {}

    /// @brief Associa @p addr a uma rotina traduzida.
    void add(uint32_t addr, Routine fn) { table_[addr] = std::move(fn); }
    /// @return true se @p addr já tem tradução.
    bool translated(uint32_t addr) const { return table_.count(addr) != 0; }
    /// @return Endereços traduzidos, em ordem crescente.
    std::vector<uint32_t> addresses() const {
        std::vector<uint32_t> out;
        out.reserve(table_.size());
        for (const auto& kv : table_) out.push_back(kv.first);
        std::sort(out.begin(), out.end());
        return out;
    }

    void call(uint32_t addr, Tms32031State& cpu, MemoryBus& bus) override {
        if (auto it = table_.find(addr); it != table_.end()) it->second(cpu, bus, *this);
        else fallback_.call(addr, cpu, bus);
    }

private:
    ISubroutines& fallback_;  ///< Destino das chamadas sem tradução registrada.
    std::unordered_map<uint32_t, Routine> table_;  ///< Rotinas traduzidas por endereço da ROM.
};

} // namespace rea::core
