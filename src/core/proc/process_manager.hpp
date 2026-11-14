#pragma once
/**
 * @file process_manager.hpp
 * @brief Processos cooperativos do jogo (`0xA844`–`0xA93A`) e seus usuários diretos.
 *
 * @details A lógica do jogo roda como **processos cooperativos** escalonados uma
 * vez por iteração do loop principal (`CALL $A872` em `0x4C04`).
 *
 * **Listas:**
 * - ativa: começa em `[C980]` (`[0xA840]` = 0xC980);
 * - livre: começa em `[C981]` (`[0xA841]` = 0xC981).
 *
 * O nó 0xC980 funciona como pseudo-nó: o campo `next` dele (offset 0) é a cabeça.
 * A `0xA8F5` encadeia 0x69 blocos de 0x3E palavras a partir de `[0xA842]` = 0xEBE7.
 *
 * **Bloco de processo (PCB):**
 *
 * | Offset | Campo |
 * |---|---|
 * | 0 | próximo |
 * | 1 | PCB + 0x2F (topo de pilha local; o escalonador não o usa) |
 * | 2 | PC de retomada |
 * | 3 | ID (classe/tipo; buscas usam `ID & máscara`) |
 * | 4 | quadros restantes de sono |
 * | 5, 6 | R4, R5 |
 * | 7, 8 | R6, R7 (float) |
 * | 9, 0xA, 0xB | AR4, AR5, AR6 |
 * | 0xC–0x3D | variáveis do processo (`*+AR7(n)`) |
 *
 * **Escalonador (`0xA872`):** percorre a lista ativa. Para cada processo:
 * 1. decrementa o sono (offset 4);
 * 2. se o resultado for ≤ 0, guarda SP em `[C97F]` e o processo em `[C97E]`;
 * 3. restaura R4–R7 e AR4–AR6 e salta para o PC, com AR7 = PCB.
 *
 * O processo devolve o controle de dois jeitos:
 * - **adormece** com `CALL $A874` (AR2 = quadros): o `POP` do retorno vira o PC
 *   de retomada; SP volta a `[C97F]`; os registradores são salvos;
 * - **morre** com `BR $A896`: o PCB vai para a lista livre.
 *
 * Nos dois casos, o percurso continua do processo seguinte. No fim da lista, o
 * `RETS` volta ao loop principal. O processo não tem pilha própria entre
 * quadros: só os registradores do PCB e as variáveis em `*+AR7(n)` sobrevivem.
 *
 * **No port:** um processo traduzido é uma rotina C++ registrada no endereço
 * do seu PC (início ou ponto de retomada). Para adormecer, ela faz
 * `cpu.ret = <endereço de retomada>`, `cpu.ar[2] = quadros`,
 * `subs.call(0xA874, ...)` e **retorna em seguida** (desenrolando a pilha C++,
 * como o original descarta a pilha). Para morrer, ela chama `0xA896` e retorna.
 *
 * Um processo ainda não traduzido (a chamada volta sem adormecer nem morrer) é
 * tratado como se adormecesse 1 quadro no mesmo PC. Assim ele aparece como
 * pendente a cada quadro, sem travar o escalonador.
 */

#include <cstdint>
#include <map>

#include "core/subroutines.hpp"

namespace rea::core::proc {

/// Endereços e constantes dos processos.
namespace proc_vars {
inline constexpr uint32_t kActiveHeadPtr = 0xA840; ///< `[0xA840]` = 0xC980 (pseudo-nó da lista ativa).
inline constexpr uint32_t kFreeHeadPtr   = 0xA841; ///< `[0xA841]` = 0xC981 (cabeça da lista livre).
inline constexpr uint32_t kPoolPtr       = 0xA842; ///< `[0xA842]` = 0xEBE7 (primeiro PCB).
inline constexpr uint32_t kActiveHead    = 0xC980;  ///< Pseudo-nó da lista de processos ativos.
inline constexpr uint32_t kFreeHead      = 0xC981;  ///< Cabeça da lista de PCBs livres.
inline constexpr uint32_t kCurrent       = 0xC97E; ///< PCB em execução.
inline constexpr uint32_t kSchedulerSp   = 0xC97F; ///< SP do escalonador (restaurado ao adormecer).
inline constexpr uint32_t kPoolBlocks    = 0x69;  ///< Quantidade de PCBs no pool.
inline constexpr uint32_t kBlockWords    = 0x3E;  ///< Palavras por PCB.
inline constexpr uint32_t kEventBits     = 0xC8CB; ///< Eventos da `0x4DE6` (28 bits baixos).
inline constexpr uint32_t kEventTablePtr = 0x4DFA; ///< `[0x4DFA]` = 0x4DFB: PC por bit.
inline constexpr uint32_t kEventProcId   = 0x0810;  ///< ID dos processos criados pelos eventos da `0x4DE6`.
inline constexpr uint32_t kErrorLogger   = 0xAB1A; ///< Registra erro (R0 = código) na CMOS.
inline constexpr uint32_t kErrNoFreeBlock = 0x210;  ///< Erro: sem PCB livre ao criar um processo.
inline constexpr uint32_t kErrDieNotFound = 0x220;  ///< Erro: o processo a encerrar não está na lista ativa.
inline constexpr uint32_t kErrKillSelf    = 0x223;  ///< Erro: um processo tentou matar a si mesmo.
inline constexpr uint32_t kErrKillNotFound = 0x224;  ///< Erro: o processo a matar não foi encontrado.
inline constexpr uint32_t kErrMoveNotFound = 0x206;  ///< Erro: o processo a mover não foi encontrado.
} // namespace proc_vars

/// Offsets do PCB.
namespace pcb {
inline constexpr uint32_t kNext = 0;            ///< Próximo PCB da lista.
inline constexpr uint32_t kStackTop = 1;        ///< Topo da pilha local do processo.
inline constexpr uint32_t kPc = 2;              ///< PC de retomada.
inline constexpr uint32_t kId = 3;              ///< ID do processo.
inline constexpr uint32_t kSleep = 4;           ///< Quadros que faltam para acordar.
/// @name Registradores salvos ao adormecer
/// @{
inline constexpr uint32_t kR4 = 5;              ///< Registrador salvo no PCB quando o processo adormece.
inline constexpr uint32_t kR5 = 6;
inline constexpr uint32_t kR6 = 7;
inline constexpr uint32_t kR7 = 8;
inline constexpr uint32_t kAr4 = 9;
inline constexpr uint32_t kAr5 = 0xA;
inline constexpr uint32_t kAr6 = 0xB;
/// @}
inline constexpr uint32_t kStackOffset = 0x2F;  ///< Início da pilha local dentro do PCB.
} // namespace pcb

/// @brief `0xA8F5`: esvazia a lista ativa e encadeia os 0x69 PCBs na lista livre. Preserva R0, AR0, AR1.
void initLists(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0xA844`: cria um processo no início da lista ativa.
 * @details Entrada: AR2 = PC, R2 = ID, R4–R7 e AR4–AR6 iniciais.
 *          Saída: AR0 = PCB e carry limpo.
 *          Sem bloco livre: registra o erro 0x210 (`0xAB1A`) e liga o carry.
 *          Preserva R0.
 */
void createProcess(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief `0xA868`: cria (`0xA844`) e move o novo processo para logo depois do atual (AR7). Com carry, retorna.
void createAfterCurrent(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief `0xA8AC`: encerra o processo AR2 (outro). Se AR2 == AR7, registra 0x223 e morre (`0xA896`).
void killProcess(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0xA8CF`: encerra todos os processos com `ID & R1 == R0 & R1`, exceto o atual (AR7).
 * @details Saída: R0 &= R1; R2 alterado. Preserva AR1 e AR2.
 */
void killMatching(Tms32031State& cpu, MemoryBus& bus);

/// @brief `0xA8E6`: carry = existe processo com `ID & R2 == AR2 & R2`. Altera R0, RC e RS.
void existsMatching(Tms32031State& cpu, MemoryBus& bus);

/// @brief `0xA908`: redireciona o processo AR0 para o PC AR1, com sono 1. R0 = 1.
void redirect(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0xA912` (do início) e `0xA90E` (a partir de AR0): busca `ID & R1 == R0 & R1`.
 * @details Saída: AR0 = PCB, ou 0 se não houver. R0 &= R1. Preserva R2.
 *          O início inclui o pseudo-nó 0xC980, mas o teste só começa no seguinte.
 */
void findProcess(Tms32031State& cpu, MemoryBus& bus, bool fromHead);

/// @brief `0xA91F`: move o processo atual (AR7) para logo depois do AR2. Sem AR7 na lista: erro 0x206.
void moveAfter(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief `0x69D6`: R0 = número de processos com ID == R2.
 * @details Como o original, também compara o pseudo-nó (`[C983]`).
 */
void countById(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x894D`: carry = há ≥ 2 processos com `ID & 0x500` e `ID & 0xF0 == 0x40`.
 * @details R2 = contagem. O pseudo-nó também entra na contagem.
 */
void countClass40(Tms32031State& cpu, MemoryBus& bus);

/**
 * @brief `0x4DE6`: cria um processo (ID 0x810) para cada bit ligado nos 28 bits baixos de `[C8CB]`.
 * @details Zera `[C8CB]`. O PC vem de `[0x4DFB + bit]`; entradas 0 são puladas.
 */
void launchEventProcesses(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/// @brief `0x95CB`: reinicia os processos (`0xA8F5`), chama os inicializadores de subsistemas e zera `[C93E]`.
void resetSubsystems(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

/**
 * @brief Escalonador (`0xA872`), adormecer (`0xA874`) e morrer (`0xA896`).
 */
class Scheduler {
public:
    /// Como a chamada ao PC de um processo terminou.
    enum class Outcome { Returned, Slept, Died };

    /// @brief `0xA872`: roda os processos uma vez. Para logo se um processo ligar `cpu.jump`.
    void run(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);
    /// @brief `0xA874`: o processo adormece AR2 quadros e retoma em `cpu.ret`.
    void sleep(Tms32031State& cpu, MemoryBus& bus);
    /// @brief `0xA896`: o processo atual (AR7) morre. AR7 passa ao anterior.
    void die(Tms32031State& cpu, MemoryBus& bus, ISubroutines& subs);

    /// Execuções de processos não traduzidos (port: re-agendados para o próximo quadro), por PC.
    const std::map<uint32_t, uint64_t>& untranslatedRuns() const { return untranslated_; }
    /// Processos executados desde a criação do objeto.
    uint64_t dispatched() const { return dispatched_; }

    /// Teto de despachos por `run()` (guarda contra listas corrompidas).
    static constexpr uint32_t kMaxDispatchPerRun = 0x1000;

private:
    Outcome outcome_ = Outcome::Returned;  ///< Como terminou o último processo executado.
    std::map<uint32_t, uint64_t> untranslated_;  ///< PCs sem tradução encontrados → quantas vezes foram executados.
    uint64_t dispatched_ = 0;  ///< Processos executados desde a criação.
};

/// @brief Registra as rotinas deste módulo. `sched` precisa viver tanto quanto `d`.
void registerRoutines(SubroutineDispatcher& d, Scheduler& sched);

} // namespace rea::core::proc
