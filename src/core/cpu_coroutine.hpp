#pragma once
/**
 * @file cpu_coroutine.hpp
 * @brief Corrotina do código principal da CPU: alterna com a plataforma a cada quadro.
 *
 * @details No hardware o código principal nunca devolve o controle: ele espera ocupado
 * (busy-wait) enquanto as interrupções avançam o estado. O loop principal (`0x4BAF`) foi
 * traduzido como máquina de estados retomável, mas várias rotinas esperam por conta própria
 * sem voltar a ele — o menu de serviço (`0x0699`), por exemplo, nunca retorna.
 *
 * A corrotina roda esse código numa thread própria, em alternância estrita com a thread da
 * plataforma (nunca as duas ao mesmo tempo):
 * - a plataforma chama resume() uma vez por quadro e fica bloqueada até a CPU chamar yield();
 * - a CPU chama yield() quando o quadro acabou (no fim do passo do loop principal ou dentro de
 *   uma espera ocupada) e fica bloqueada até o próximo resume().
 *
 * Como só uma das threads executa por vez (passagem de bastão com mutex), não há acesso
 * concorrente à memória emulada.
 *
 * No navegador (Emscripten) a página não pode ficar bloqueada esperando outra thread: lá a CPU roda
 * numa fibra (`emscripten_fiber_t`, com ASYNCIFY) na mesma thread, e resume()/yield() trocam de
 * contexto diretamente. O comportamento visto de fora é o mesmo. O prazo de setStallLimit() é
 * conferido de dentro da CPU: MemoryBus::readWord() chama checkStall(), que a cada 2^20 leituras olha
 * o relógio e, vencido o prazo, lança Stalled; a fibra desenrola a pilha e resume() retorna false.
 */

#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/fiber.h>
#endif

namespace rea::core {

/// @brief Roda o código principal da CPU numa thread própria, em alternância estrita com a plataforma (detalhes no cabeçalho do arquivo).
class CpuCoroutine {
public:
    /// Lançada por yield() quando a plataforma encerra: desenrola a pilha da CPU.
    struct Stop {};
    /// Navegador: lançada por checkStall() quando a CPU passou do prazo sem yield(); desenrola a pilha da CPU.
    struct Stalled {};

    /// @param body Código da CPU; recebe a própria corrotina para chamar yield().
    explicit CpuCoroutine(std::function<void(CpuCoroutine&)> body) : body_(std::move(body)) {}
    ~CpuCoroutine();

    CpuCoroutine(const CpuCoroutine&) = delete;
    CpuCoroutine& operator=(const CpuCoroutine&) = delete;

    /**
     * @brief Plataforma: executa a CPU até o próximo yield() (ou até o corpo terminar).
     * @return false se o corpo já terminou.
     * @note Uma exceção que escape do corpo é relançada aqui.
     */
    bool resume();

    /**
     * @brief Limita quanto resume() espera pelo yield() (0 = sem limite, o padrão).
     * @details Se a CPU não devolver o controle no prazo — ex.: o programa de outra versão da ROM
     * faz uma rotina traduzida da L4.1 girar sem fim —, resume() retorna false e stalled() passa a
     * true. A thread da CPU não pode ser interrompida: o destrutor a solta (detach) e cabe ao
     * chamador encerrar o processo. No navegador (fibra) a CPU confere o prazo sozinha (checkStall())
     * e, vencido, a pilha dela é desenrolada; resume() retorna false do mesmo jeito.
     */
    void setStallLimit(std::chrono::milliseconds limit) { stall_limit_ = limit; }

    /// @return true se um resume() estourou o prazo de setStallLimit() (a CPU continua presa).
    bool stalled() const { return stalled_; }

    /// @brief CPU: devolve o controle à plataforma. Lança Stop se ela estiver encerrando.
    void yield();

    /// @return true se o chamador está na thread da CPU (no navegador: dentro da fibra da CPU).
#ifdef __EMSCRIPTEN__
    bool inCpuThread() const { return in_cpu_; }

    /// @brief Navegador: chamada a cada leitura de memória; a cada 2^20 confere o prazo da CPU em execução.
    /// @details Fora da fibra da CPU (ex.: a plataforma compondo a tela) não faz nada.
    static void checkStall() {
        if ((++stall_reads_ & 0xFFFFFu) == 0 && running_) running_->checkDeadline();
    }
#else
    bool inCpuThread() const { return std::this_thread::get_id() == thread_.get_id(); }
#endif

private:
    /// Corpo da thread da CPU: espera o primeiro resume(), executa @c body_ e guarda uma exceção que escape.
    void run();

    std::function<void(CpuCoroutine&)> body_;  ///< Código da CPU executado na thread.
    std::thread thread_;  ///< Thread da CPU.
    std::mutex mutex_;  ///< Protege o bastão e as flags abaixo.
    std::condition_variable cv_;  ///< Sinaliza a troca de bastão entre as threads.
#ifndef __EMSCRIPTEN__
    bool cpu_turn_ = false;  ///< bastão: true = a CPU executa
#endif
    bool started_ = false;  ///< true depois que a thread foi criada.
    bool stopping_ = false;  ///< true quando a plataforma está encerrando (yield() lança Stop).
    bool done_ = false;  ///< true quando o corpo terminou.
    std::chrono::milliseconds stall_limit_{0};  ///< Prazo de resume() (0 = sem limite).
    bool stalled_ = false;  ///< true se a CPU estourou o prazo (thread solta no destrutor).
    std::exception_ptr error_;  ///< Exceção que escapou do corpo, relançada em resume().
#ifdef __EMSCRIPTEN__
    /// Ponto de entrada da fibra da CPU (@p arg = a corrotina).
    static void fiberEntry(void* arg);
    emscripten_fiber_t main_fiber_{};  ///< Contexto da plataforma (o laço de quadros).
    emscripten_fiber_t cpu_fiber_{};   ///< Contexto da CPU.
    std::vector<char> main_async_stack_;  ///< Pilha do ASYNCIFY do contexto da plataforma.
    std::vector<char> cpu_c_stack_;       ///< Pilha C da fibra da CPU.
    std::vector<char> cpu_async_stack_;   ///< Pilha do ASYNCIFY da fibra da CPU.
    bool in_cpu_ = false;  ///< true enquanto a fibra da CPU executa.
    double resume_ms_ = 0.0;  ///< Instante (emscripten_get_now) do resume() corrente.
    /// Lança Stalled se a CPU está há mais de @c stall_limit_ sem devolver o controle.
    void checkDeadline() const;
    static inline CpuCoroutine* running_ = nullptr;  ///< Corrotina cuja fibra está executando (ou nullptr).
    static inline uint32_t stall_reads_ = 0;         ///< Contador de leituras de memória (checkStall).
#endif
};

} // namespace rea::core
