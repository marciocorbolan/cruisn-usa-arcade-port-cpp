#pragma once
/**
 * @file test_support.hpp
 * @brief Apoio comum da suíte de testes (sem framework): cabeçalhos do core, `check()` e dublês.
 */
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <fstream>
#include <filesystem>
#include <iterator>
#include <functional>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "core/boot/boot_sequence.hpp"
#include "core/cpu_coroutine.hpp"
#include "core/non_local_jump.hpp"
#include "core/service/service_ctx.hpp"
#include "core/service/service_menu.hpp"
#include "core/boot/reset_routine.hpp"
#include "core/boot/system_routines.hpp"
#include "core/boot/power_on_test.hpp"
#include "core/cmos/cmos_file.hpp"
#include "core/cmos/cmos_routines.hpp"
#include "core/frame_runner.hpp"
#include "core/gfx/lzw_loader.hpp"
#include "core/io/input_manager.hpp"
#include "core/io/input_routines.hpp"
#include "core/io/input_script.hpp"
#include "core/io/link_port.hpp"
#include "core/link/link_messages.hpp"
#include "core/link/link_packet.hpp"
#include "core/proc/process_manager.hpp"
#include "core/game/game_start.hpp"
#include "core/game/attract.hpp"
#include "core/game/hiscore_scene.hpp"
#include "core/video/palette_slots.hpp"
#include "core/math/trig.hpp"
#include "core/math/matrix.hpp"
#include "core/render/pipeline3d.hpp"
#include "core/video/text_overlay.hpp"
#include "core/scene/object_query.hpp"
#include "core/scene/node_pools.hpp"
#include "core/game/credits_display.hpp"
#include "core/game/car_proximity.hpp"
#include "core/math/float_math.hpp"
#include "core/io/cabinet_outputs.hpp"
#include "core/audio/sound_commands.hpp"
#include "core/scene/scene_object.hpp"
#include "core/boot/boot_screens.hpp"
#include "core/math/int_math.hpp"
#include "core/video/display.hpp"
#include "core/video/dma_renderer.hpp"
#include "core/video/video_routines.hpp"
#include "core/link/link_routines.hpp"
#include "core/irq/adc_handler.hpp"
#include "core/irq/vblank_handler.hpp"
#include "core/main/main_loop.hpp"
#include "core/main/profiler_routines.hpp"
#include "core/game_loop.hpp"
#include "core/memory_bus.hpp"
#include "core/resource_manager.hpp"
#include "core/rom_source.hpp"
#include "core/scene/object_list.hpp"
#include "core/game/attract_demo.hpp"
#include "core/game/demo_camera.hpp"
#include "core/game/drone_car.hpp"
#include "core/game/car_physics.hpp"
#include "core/game/car_dynamics.hpp"
#include "core/game/drone_ai.hpp"
#include "core/game/race_start.hpp"
#include "core/game/car_wheels.hpp"
#include "core/game/race_traffic.hpp"
#include "core/game/car_collision.hpp"
#include "core/game/scenery_collision.hpp"
#include "core/game/smoke_effects.hpp"
#include "core/game/track_props.hpp"
#include "core/game/roaming_objects.hpp"
#include "core/game/special_car.hpp"
#include "core/game/ending.hpp"
#include "core/game/roadside_events.hpp"
#include "core/game/credits.hpp"
#include "core/game/coin_start.hpp"
#include "core/game/game_flow.hpp"
#include "core/game/course_select.hpp"
#include "core/game/race_entry.hpp"
#include "core/game/player_car.hpp"
#include "core/game/race_events.hpp"
#include "core/game/race_hud.hpp"
#include "core/game/car_crash.hpp"
#include "core/game/object_effects.hpp"
#include "core/game/hiscore_entry.hpp"
#include "core/game/results_boards.hpp"
#include "core/game/traffic_cars.hpp"
#include "core/io/serial_printer.hpp"
#include "core/video/palette_blink.hpp"
#include "core/track/track_loader.hpp"
#include "core/track/track_stream.hpp"
#include "core/track/ground_height.hpp"
#include "core/video/sky_renderer.hpp"
#include "core/tms_float.hpp"
#include "core/link/link_car_sync.hpp"
#include "core/game/link_car.hpp"
#include "core/game/link_intro.hpp"
#include "core/audio/dcs/dcs_board.hpp"
#include "core/audio/audio_manager.hpp"
#include "core/video/texture_pack.hpp"
#include "core/vunit_hw.hpp"
#include "ui/localization_manager.hpp"

namespace rea::test {
/// π em double (M_PI não é C++ padrão: o MinGW no modo estrito não o define).
inline constexpr double kPi = 3.14159265358979323846;
inline int g_failures = 0;
inline void check(bool cond, const char* what) {
    std::printf("[%s] %s\n", cond ? " OK " : "FAIL", what);
    if (!cond) ++g_failures;
}

/// Relógio falso: avança exatamente o que pedirem no sleep.
/// @brief Caminho único na pasta temporária do sistema para @p name.
/// @details Não depende da pasta de build (antes alguns testes gravavam em `<fonte>/build/`, que pode não existir)
///          e não colide entre duas suítes rodando ao mesmo tempo (ex.: o build normal e o build com sanitizers).
inline std::filesystem::path tempPath(const std::string& name) {
    static const auto token = std::to_string(static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count() ^ reinterpret_cast<std::uintptr_t>(&name)));
    return std::filesystem::temp_directory_path() / ("rea_" + token + "_" + name);
}

struct FakeClock : rea::core::IClock {
    double t = 0.0;
    double now() override { return t; }
    void sleep(double s) override { t += s > 0.001 ? s : 0.001; }
};
struct FakeInput : rea::core::IInput {
    rea::core::ArcadeInputState poll() override { return {}; }
};

/// FNV-1a de 32 bits (comparação compacta com a simulação de referência em Python).
inline uint32_t fnv1a(const rea::core::MemoryBus& bus, std::size_t first_byte, std::size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (std::size_t i = 0; i < n; ++i) h = (h ^ bus.textureByte(first_byte + i)) * 0x01000193u;
    return h;
}

/**
 * @brief Codificador LZW de teste no formato do jogo (códigos MSB primeiro, 0x101 = +1 bit).
 * @param data Bytes a comprimir.
 * @param grow_first Emite 0x101 após o 1º código (exercita a troca de largura).
 * @return Palavras de 32 bits do fluxo.
 */
inline std::vector<uint32_t> lzwEncode(const std::vector<uint8_t>& data, bool grow_first) {
    std::vector<uint32_t> words;
    uint64_t acc = 0;
    int acc_bits = 0;
    unsigned width = 9;
    auto emit = [&](uint32_t code) {
        acc = (acc << width) | code;
        acc_bits += static_cast<int>(width);
        while (acc_bits >= 32) {
            words.push_back(static_cast<uint32_t>(acc >> (acc_bits - 32)));
            acc_bits -= 32;
        }
    };
    std::map<std::string, uint32_t> dict;
    uint32_t next = 0x103;
    std::string w;
    for (uint8_t b : data) {
        std::string wc = w + static_cast<char>(b);
        if (wc.size() == 1 || dict.count(wc)) { w = wc; continue; }
        emit(w.size() == 1 ? static_cast<uint8_t>(w[0]) : dict[w]);
        // 0x101 só é reconhecido a partir do 2º código (o 1º, lido em 0xA338, é sempre literal).
        if (grow_first) { emit(0x101); width = 10; grow_first = false; }
        dict[wc] = next++;
        w = std::string(1, static_cast<char>(b));
    }
    emit(w.size() == 1 ? static_cast<uint8_t>(w[0]) : dict[w]);
    emit(0x100);
    if (acc_bits > 0) words.push_back(static_cast<uint32_t>(acc << (32 - acc_bits)));
    words.push_back(0);
    return words;
}

/**
 * @brief Monta uma lista de objetos sintética em RAM e lê a ordem resultante.
 * @details Objeto i em 0x10000 + 0x40·i; célula da cabeça em 0xC982 ([0x40]).
 */
struct ObjectListFixture {
    rea::core::MemoryBus& bus;
    static constexpr uint32_t kCell = 0xC982;
    static uint32_t obj(int i) { return 0x10000u + 0x40u * static_cast<uint32_t>(i); }
    /// Define os campos e encadeia na ordem dada.
    void build(const std::vector<int>& order) {
        bus.writeWord(0x40, kCell);
        uint32_t prev = kCell;
        for (int i : order) { bus.writeWord(prev, obj(i)); prev = obj(i); }
        bus.writeWord(prev, 0);
    }
    void set(int i, uint32_t type, int32_t key, double coord = 0.0) {
        bus.writeWord(obj(i) + 0x0F, type);
        bus.writeWord(obj(i) + 0x1C, static_cast<uint32_t>(key));
        bus.writeWord(obj(i) + 0x01, rea::core::doubleToTms(coord));
    }
    /// @return Índices na ordem da lista (no máximo 64, para não travar em ciclos).
    std::vector<int> order() const {
        std::vector<int> out;
        for (uint32_t a = bus.readWord(kCell); a != 0 && out.size() < 64; a = bus.readWord(a))
            out.push_back(static_cast<int>((a - 0x10000u) / 0x40u));
        return out;
    }
};

/// Relógio de teste: "quadros" curtos; cada quadro com GIE ligado faz o papel da INT0 ([C930]++).
struct FakeFrames : rea::core::IBusyWait {
    rea::core::MemoryBus& bus;
    uint32_t per_frame;
    uint64_t acc = 0;
    uint32_t frames = 0;
    FakeFrames(rea::core::MemoryBus& b, uint32_t pf) : bus(b), per_frame(pf) {}
    bool spin(rea::core::Tms32031State& cpu, uint32_t n) override {
        acc += n;
        while (acc >= per_frame) {
            acc -= per_frame;
            ++frames;
            if (cpu.st & rea::core::Tms32031State::kStGie) bus.writeWord(0xC930, bus.readWord(0xC930) + 1);
        }
        return frames < 100000;  // trava de segurança do teste
    }
};


/// Relógio do menu de serviço: cada quadro incrementa [C930] e aplica as teclas do roteiro em [C8C9].
struct ServiceFrames : rea::core::IBusyWait {
    rea::core::MemoryBus& bus;
    std::function<uint32_t(uint32_t)> keys;  ///< quadro → teclas apertadas (bits em 1)
    uint32_t limit;
    uint64_t acc = 0;
    uint32_t frames = 0;
    ServiceFrames(rea::core::MemoryBus& b, std::function<uint32_t(uint32_t)> k, uint32_t lim)
        : bus(b), keys(std::move(k)), limit(lim) {
        bus.writeWord(0xC8C9, ~keys(0));
    }
    bool spin(rea::core::Tms32031State&, uint32_t n) override {
        acc += n;
        while (acc >= 431600) {
            acc -= 431600;
            ++frames;
            bus.writeWord(0xC930, bus.readWord(0xC930) + 1);
            bus.writeWord(0xC8C9, ~keys(frames));
        }
        return frames < limit;
    }
};

/**
 * @brief Co-simulação de dois gabinetes: cada um roda numa thread, mas só um
 *        executa por vez (revezamento pelo relógio de instruções). Assim os
 *        acessos ao cabo são serializados e a temporização relativa (atrasos
 *        RPTS × leituras da porta) é respeitada.
 */
struct LinkCoSim {
    std::mutex m;
    std::condition_variable cv;
    int turn = 0;
    uint64_t clock[2] = {0, 0};
    bool done[2] = {false, false};

    struct Side : rea::core::IBusyWait {
        LinkCoSim& sim;
        int me;
        FakeFrames frames;
        Side(LinkCoSim& s, int id, rea::core::MemoryBus& bus) : sim(s), me(id), frames(bus, 431577) {}
        bool spin(rea::core::Tms32031State& cpu, uint32_t n) override {
            frames.spin(cpu, n);
            std::unique_lock<std::mutex> lk(sim.m);
            sim.clock[me] += n;
            const int other = 1 - me;
            if (!sim.done[other] && sim.clock[me] > sim.clock[other]) {
                sim.turn = other;
                sim.cv.notify_all();
                sim.cv.wait(lk, [&] { return sim.turn == me; });
            }
            return sim.clock[me] < 50000000ull;  // trava de segurança
        }
    };

    template <typename Fn>
    void run(Fn&& master, Fn&& slave) {
        auto body = [&](int me, Fn& fn) {
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&] { return turn == me; });
            }
            fn();
            std::lock_guard<std::mutex> lk(m);
            done[me] = true;
            turn = 1 - me;
            cv.notify_all();
        };
        std::thread a(body, 0, std::ref(master));
        std::thread b(body, 1, std::ref(slave));
        a.join();
        b.join();
    }
};

/// Prepara a RAM de um gabinete para a 0x7F5D (Linking ligada).
inline void setupLinkMachine(rea::core::MemoryBus& b, bool slave, const std::vector<uint8_t>& tx) {
    using namespace rea::core::link::link_vars;
    b.writeWord(kRxBufferPtr, 0x1F258);
    b.writeWord(kPortPtr, 0x997000);
    b.writeWord(kTxBufferPtr, 0x1F000);
    // Como a 0x7F06 no boot: Master 0xC000 / Slave 0x3000 e COMCOE ligado.
    b.writeWord(0x997000, slave ? 0x30000000u : 0xC0000000u);
    b.writeWord(0x997001, 0x20000000u);
    b.writeWord(kDipShadow, slave ? 1u : 0u);  // bit 2 = 0: Linking ligada
    b.writeWord(kAdcState, 3);
    b.writeWord(kTxLength, static_cast<uint32_t>(tx.size()));
    for (std::size_t i = 0; i < tx.size(); ++i) b.writeWord(0x1F000 + static_cast<uint32_t>(i), tx[i]);
}
} // namespace rea::test

// Um executor por arquivo de teste (chamados pelo test_main.cpp, nesta ordem).
void runBasicsTests();
void runVideoTests();
void runSceneTests();
void runLinkTests();
void runProcessesTests();
void runIoTests();
void runAudioTests();
void runGameTests();
void runResourcesTests();
void runServiceTests();
void runRomGameTests(const std::optional<std::filesystem::path>& dir);
void runRomSystemTests(const std::optional<std::filesystem::path>& dir, rea::core::MemoryBus& bus);
