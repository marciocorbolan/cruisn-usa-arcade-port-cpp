/**
 * @file link_shm.cpp
 * @brief Cabo de link entre processos: arquivo mapeado em memória (POSIX `mmap` ou Win32 `MapViewOfFile`).
 */
#include "platform/link_shm.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <chrono>
#include <thread>

namespace rea::platform {

namespace {
constexpr uint32_t kMagic = 0x4C494E4Bu;  ///< Assinatura do bloco compartilhado. — "LINK"
constexpr uint64_t kDeadNs = 1000000000ull;  ///< Sem batimento por mais de 1 s, o outro lado é considerado desligado.

/// Relógio monotônico em nanossegundos.
uint64_t nowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}
} // namespace

/// @brief Abre (ou cria) o arquivo e mapeia @p size bytes compartilhados; nulo em caso de erro.
void* ShmLinkWire::mapFile(const std::string& path, std::size_t size) {
#ifdef _WIN32
    HANDLE file = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return nullptr;
    // O mapeamento estende o arquivo até `size` se ele for menor.
    HANDLE mapping = ::CreateFileMappingA(file, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(size), nullptr);
    if (!mapping) {
        ::CloseHandle(file);
        return nullptr;
    }
    void* p = ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!p) {
        ::CloseHandle(mapping);
        ::CloseHandle(file);
        return nullptr;
    }
    file_ = file;
    mapping_ = mapping;
    return p;
#else
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd_ < 0) return nullptr;
    struct stat st {};
    if (::fstat(fd_, &st) != 0) return nullptr;
    if (static_cast<std::size_t>(st.st_size) < size && ::ftruncate(fd_, static_cast<off_t>(size)) != 0) return nullptr;
    void* p = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    return p == MAP_FAILED ? nullptr : p;
#endif
}

void ShmLinkWire::unmapFile() {
#ifdef _WIN32
    if (shared_) ::UnmapViewOfFile(shared_);
    if (mapping_) ::CloseHandle(mapping_);
    if (file_) ::CloseHandle(file_);
    mapping_ = file_ = nullptr;
#else
    if (shared_) ::munmap(shared_, sizeof(Shared));
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
#endif
    shared_ = nullptr;
}

ShmLinkWire::ShmLinkWire(const std::string& path, int side) : me_(side & 1) {
    shared_ = static_cast<Shared*>(mapFile(path, sizeof(Shared)));
    if (!shared_) return;
    shared_->magic.store(kMagic);
    Side& s = shared_->side[me_];
    s.lines.store(0);
    // Se o outro lado já está rodando, este começa no relógio dele. Começar em 0 deixaria o outro "à frente" e
    // parado até este alcançá-lo: reabrir um executável congelava o outro pelo tempo que ele já tinha rodado.
    const Side& peer = shared_->side[1 - me_];
    const uint64_t alive = peer.alive_ns.load();
    const bool peer_running = alive != 0 && nowNs() - alive <= kDeadNs;
    s.clock.store(peer_running ? peer.clock.load() : 0);
    beat();
}

ShmLinkWire::~ShmLinkWire() {
    if (shared_) {
        shared_->side[me_].alive_ns.store(0);  // desconecta na hora
        shared_->side[me_].lines.store(0);
    }
    unmapFile();
}

void ShmLinkWire::beat() { shared_->side[me_].alive_ns.store(nowNs()); }

void ShmLinkWire::publish(uint16_t lines) {
    if (!shared_) return;
    shared_->side[me_].lines.store(lines);
}

bool ShmLinkWire::waitPeer() {
    const Side& peer = shared_->side[1 - me_];
    const uint64_t mine = shared_->side[me_].clock.load();
    bool waited = false;
    for (uint32_t spins = 0;; ++spins) {
        const uint64_t other = peer.clock.load();
        if (other >= mine) break;
        const uint64_t alive = peer.alive_ns.load();
        if (alive == 0 || nowNs() - alive > kDeadNs) return false;
        waited = true;
        beat();
        if (spins < 64) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    if (waited) ++waits_;
    return true;
}

void ShmLinkWire::advance(uint64_t instructions) {
    if (!shared_) return;
    in_frame_ += instructions;
    shared_->side[me_].clock.fetch_add(instructions);
    beat();
    waitPeer();  // à frente do outro: cede a vez (como a co-simulação dos testes)
}

void ShmLinkWire::frameDone(bool topUp) {
    if (!shared_) return;
    if (topUp && in_frame_ < kInstructionsPerFrame) shared_->side[me_].clock.fetch_add(kInstructionsPerFrame - in_frame_);
    in_frame_ = 0;
    beat();
}

uint16_t ShmLinkWire::peerLines() {
    if (!shared_) return 0;
    const Side& peer = shared_->side[1 - me_];
    if (!waitPeer()) {
        ++disconnects_;
        return 0;
    }
    const uint64_t alive = peer.alive_ns.load();
    if (alive == 0 || nowNs() - alive > kDeadNs) {
        ++disconnects_;
        return 0;
    }
    return static_cast<uint16_t>(peer.lines.load());
}

} // namespace rea::platform
