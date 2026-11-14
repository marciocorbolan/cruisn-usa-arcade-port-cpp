/**
 * @file cpu_coroutine.cpp
 * @brief Implementação da corrotina da CPU (passagem de bastão entre duas threads).
 */
#include "core/cpu_coroutine.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace rea::core {

#ifdef __EMSCRIPTEN__

// --- Navegador: fibra na mesma thread (ASYNCIFY) ------------------------------------------------
// As pilhas são grandes porque o código traduzido aninha muitas rotinas (a thread nativa tem 8 MB).
namespace {
constexpr std::size_t kCpuCStack = 4u << 20;      ///< Pilha C da fibra da CPU.
constexpr std::size_t kAsyncifyStack = 1u << 20;  ///< Pilha de cada contexto do ASYNCIFY.
}  // namespace

CpuCoroutine::~CpuCoroutine() {
    if (!started_ || done_) return;
    stopping_ = true;  // yield() lança Stop e a fibra desenrola a pilha até o fim de run()
    resume();
}

bool CpuCoroutine::resume() {
    if (done_) return false;
    if (!started_) {
        started_ = true;
        main_async_stack_.resize(kAsyncifyStack);
        cpu_c_stack_.resize(kCpuCStack);
        cpu_async_stack_.resize(kAsyncifyStack);
        emscripten_fiber_init_from_current_context(&main_fiber_, main_async_stack_.data(), main_async_stack_.size());
        emscripten_fiber_init(&cpu_fiber_, &CpuCoroutine::fiberEntry, this, cpu_c_stack_.data(), cpu_c_stack_.size(),
                              cpu_async_stack_.data(), cpu_async_stack_.size());
    }
    in_cpu_ = true;
    running_ = this;
    resume_ms_ = emscripten_get_now();
    emscripten_fiber_swap(&main_fiber_, &cpu_fiber_);
    running_ = nullptr;
    in_cpu_ = false;
    if (stalled_) return false;
    if (error_) {
        std::exception_ptr e = error_;
        error_ = nullptr;
        std::rethrow_exception(e);
    }
    return !done_;
}

void CpuCoroutine::yield() {
    emscripten_fiber_swap(&cpu_fiber_, &main_fiber_);
    if (stopping_) throw Stop{};
}

void CpuCoroutine::checkDeadline() const {
    if (stall_limit_.count() > 0 && emscripten_get_now() - resume_ms_ > static_cast<double>(stall_limit_.count()))
        throw Stalled{};
}

void CpuCoroutine::fiberEntry(void* arg) {
    static_cast<CpuCoroutine*>(arg)->run();
}

void CpuCoroutine::run() {
    try {
        body_(*this);
    } catch (const Stop&) {
        // encerramento pedido pela plataforma
    } catch (const Stalled&) {
        stalled_ = true;  // a CPU girou além do prazo: a pilha já foi desenrolada
    } catch (...) {
        error_ = std::current_exception();
    }
    done_ = true;
    // A função de entrada de uma fibra não pode retornar: volta à plataforma para sempre.
    for (;;) emscripten_fiber_swap(&cpu_fiber_, &main_fiber_);
}

#else  // --- Nativo: thread própria com passagem de bastão --------------------------------------

CpuCoroutine::~CpuCoroutine() {
    if (!started_) return;
    if (stalled_) {  // a CPU está presa num laço: não há como esperá-la nem interrompê-la
        thread_.detach();
        return;
    }
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!done_) {
            stopping_ = true;
            cpu_turn_ = true;
            cv_.notify_all();
            cv_.wait(lock, [this] { return done_; });
        }
    }
    thread_.join();
}

bool CpuCoroutine::resume() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (done_ || stalled_) return false;
    cpu_turn_ = true;
    if (!started_) {
        started_ = true;
        thread_ = std::thread([this] { run(); });
    } else {
        cv_.notify_all();
    }
    if (stall_limit_.count() > 0) {
        if (!cv_.wait_for(lock, stall_limit_, [this] { return !cpu_turn_; })) {
            stalled_ = true;
            return false;
        }
    } else {
        cv_.wait(lock, [this] { return !cpu_turn_; });
    }
    if (error_) {
        std::exception_ptr e = error_;
        error_ = nullptr;
        std::rethrow_exception(e);
    }
    return !done_;
}

void CpuCoroutine::yield() {
    std::unique_lock<std::mutex> lock(mutex_);
    cpu_turn_ = false;
    cv_.notify_all();
    cv_.wait(lock, [this] { return cpu_turn_; });
    if (stopping_) throw Stop{};
}

void CpuCoroutine::run() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return cpu_turn_; });
    }
    try {
        body_(*this);
    } catch (const Stop&) {
        // encerramento pedido pela plataforma
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = std::current_exception();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    done_ = true;
    cpu_turn_ = false;
    cv_.notify_all();
}

#endif  // __EMSCRIPTEN__

} // namespace rea::core
