cmake_minimum_required(VERSION 3.16)
project(rea_cruisn VERSION 0.1.0 LANGUAGES CXX)

# `cmake --install build` sem --prefix monta a pasta distribuível em build/dist
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/dist" CACHE PATH "Pasta de instalação" FORCE)
endif()

# ---------------------------------------------------------------------------
# Padrão da linguagem: C++17 estrito (sem extensões GNU)
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Padrão RelWithDebInfo (-O2 -g): roda em tempo real e ainda dá para depurar. Em Debug (-O0) o jogo fica
# abaixo da velocidade do arcade (~77 % numa corrida) e o som falha; use -DCMAKE_BUILD_TYPE=Debug só para depurar.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Tipo de build" FORCE)
endif()

# Binários em build/bin (todas as configs, inclusive multi-config do MSVC)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

# Avisos por compilador
add_library(rea_warnings INTERFACE)
if(MSVC)
    target_compile_options(rea_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(rea_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Dependências: SDL2 (a única biblioteca externa)
# Procura primeiro os pacotes CONFIG (Ubuntu 24.04, vcpkg, MSYS2) e cai para
# pkg-config quando não existirem.
# ---------------------------------------------------------------------------
find_package(PkgConfig QUIET)

function(rea_find_sdl_module cmake_name pkg_name target_alias required)
    find_package(${cmake_name} CONFIG QUIET)
    if(TARGET ${cmake_name}::${cmake_name})
        add_library(${target_alias} ALIAS ${cmake_name}::${cmake_name})
    elseif(TARGET ${cmake_name}::${cmake_name}-static)
        add_library(${target_alias} ALIAS ${cmake_name}::${cmake_name}-static)
    elseif(PkgConfig_FOUND)
        pkg_check_modules(${cmake_name}_PC QUIET IMPORTED_TARGET ${pkg_name})
        if(${cmake_name}_PC_FOUND)
            add_library(${target_alias} ALIAS PkgConfig::${cmake_name}_PC)
        endif()
    endif()
    if(TARGET ${target_alias})
        message(STATUS "Encontrado: ${cmake_name}")
        set(${cmake_name}_AVAILABLE ON PARENT_SCOPE)
    elseif(required)
        message(FATAL_ERROR "${cmake_name} não encontrado (instale ${pkg_name}).")
    else()
        message(STATUS "Opcional ausente: ${cmake_name}")
        set(${cmake_name}_AVAILABLE OFF PARENT_SCOPE)
    endif()
endfunction()

if(EMSCRIPTEN)
    # Navegador: o SDL2 vem do próprio Emscripten (port "sdl2"); exceções C++ em todo o código (os desvios
    # não locais da ROM e o encerramento da corrotina usam exceções).
    add_library(rea_sdl2_web INTERFACE)
    target_compile_options(rea_sdl2_web INTERFACE -sUSE_SDL=2)
    target_link_options(rea_sdl2_web INTERFACE -sUSE_SDL=2)
    add_library(rea::sdl2 ALIAS rea_sdl2_web)
    add_compile_options(-fexceptions)
    add_link_options(-fexceptions)
else()
    rea_find_sdl_module(SDL2       sdl2        rea::sdl2       ON)
endif()

# SDL2main (apenas Windows) para o ponto de entrada WinMain
if(WIN32 AND TARGET SDL2::SDL2main)
    set(REA_SDL_MAIN SDL2::SDL2main)
endif()

# ---------------------------------------------------------------------------
# rea_core: lógica do jogo traduzida da ROM — NÃO conhece SDL2
# ---------------------------------------------------------------------------
add_library(rea_core STATIC
    src/core/memory_bus.cpp
    src/core/boot/reset_routine.cpp
    src/core/boot/system_routines.cpp
    src/core/boot/boot_sequence.cpp
    src/core/boot/power_on_test.cpp
    src/core/main/profiler_routines.cpp
    src/core/main/main_loop.cpp
    src/core/cmos/cmos_routines.cpp
    src/core/gfx/lzw_loader.cpp
    src/core/scene/object_list.cpp
    src/core/irq/vblank_handler.cpp
    src/core/irq/adc_handler.cpp
    src/core/io/cabinet_io.cpp
    src/core/io/cabinet_outputs.cpp
    src/core/audio/sound_commands.cpp
    src/core/audio/audio_manager.cpp
    src/core/audio/sound_overrides.cpp
    src/core/audio/dcs/dcs_board.cpp
    src/core/audio/dcs/dcs_selftest.cpp
    src/core/audio/dcs/dcs_resident.cpp
    src/core/audio/dcs/dcs_overlays.cpp
    src/core/io/input_manager.cpp
    src/core/io/input_script.cpp
    src/core/io/input_routines.cpp
    src/core/io/link_port.cpp
    src/core/link/link_routines.cpp
    src/core/link/link_packet.cpp
    src/core/link/link_messages.cpp
    src/core/link/link_car_sync.cpp
    src/core/proc/process_manager.cpp
    src/core/math/int_math.cpp
    src/core/boot/boot_screens.cpp
    src/core/video/video_routines.cpp
    src/core/video/display.cpp
    src/core/video/dma_renderer.cpp
    src/core/video/texture_pack.cpp
    src/core/video/palette_slots.cpp
    src/core/game/game_start.cpp
    src/core/game/attract.cpp
    src/core/game/hiscore_scene.cpp
    src/core/math/trig.cpp
    src/core/math/matrix.cpp
    src/core/math/float_math.cpp
    src/core/video/text_overlay.cpp
    src/core/game/title_screen.cpp
    src/core/game/credits_display.cpp
    src/core/game/car_proximity.cpp
    src/core/game/attract_demo.cpp
    src/core/game/demo_camera.cpp
    src/core/track/track_loader.cpp
    src/core/track/track_stream.cpp
    src/core/track/ground_height.cpp
    src/core/video/sky_renderer.cpp
    src/core/game/drone_car.cpp
    src/core/game/car_physics.cpp
    src/core/game/car_dynamics.cpp
    src/core/game/drone_ai.cpp
    src/core/game/race_start.cpp
    src/core/game/car_wheels.cpp
    src/core/game/race_traffic.cpp
    src/core/game/car_collision.cpp
    src/core/game/scenery_collision.cpp
    src/core/game/smoke_effects.cpp
    src/core/game/track_props.cpp
    src/core/game/coin_start.cpp
    src/core/game/game_flow.cpp
    src/core/game/course_select.cpp
    src/core/game/car_select.cpp
    src/core/game/race_entry.cpp
    src/core/game/player_car.cpp
    src/core/game/player_effects.cpp
    src/core/game/race_hud.cpp
    src/core/game/race_events.cpp
    src/core/game/object_effects.cpp
    src/core/game/roaming_objects.cpp
    src/core/game/special_car.cpp
    src/core/game/music_title.cpp
    src/core/game/link_car.cpp
    src/core/game/link_intro.cpp
    src/core/game/ending.cpp
    src/core/game/roadside_events.cpp
    src/core/game/credits.cpp
    src/core/game/car_crash.cpp
    src/core/game/post_race.cpp
    src/core/game/results_boards.cpp
    src/core/game/screen_doors.cpp
    src/core/game/hiscore_entry.cpp
    src/core/game/continue_screen.cpp
    src/core/game/traffic_cars.cpp
    src/core/io/serial_printer.cpp
    src/core/video/palette_blink.cpp
    src/core/render/pipeline3d.cpp
    src/core/scene/object_query.cpp
    src/core/scene/node_pools.cpp
    src/core/scene/scene_object.cpp
    src/core/frame_runner.cpp
    src/core/cpu_coroutine.cpp
    src/core/service/service_ctx.cpp
    src/core/service/service_menu.cpp
    src/core/service/service_diagnostics.cpp
    src/core/service/service_adjustments.cpp
    src/core/service/service_audits.cpp
    src/core/service/service_motion.cpp
    src/core/service/service_calibration.cpp
    src/core/cmos/cmos_file.cpp
    src/core/rom_source.cpp
    src/core/rom_identity.cpp
    src/core/resource_manager.cpp
    src/core/game_loop.cpp
    src/ui/localization_manager.cpp
)
target_include_directories(rea_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(rea_core PRIVATE rea_warnings)
if(NOT EMSCRIPTEN)  # no navegador a corrotina da CPU é uma fibra (sem threads)
    find_package(Threads REQUIRED)  # corrotina da CPU (core/cpu_coroutine) e co-simulação do link nos testes
    target_link_libraries(rea_core PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# rea_platform_sdl: implementações concretas das interfaces puras
# ---------------------------------------------------------------------------
add_library(rea_platform_sdl STATIC
    src/platform/sdl/sdl_renderer.cpp
    src/platform/sdl/sdl_input.cpp
    src/platform/sdl/sdl_audio.cpp
    src/platform/link_shm.cpp
)
target_link_libraries(rea_platform_sdl PUBLIC rea_core rea::sdl2 PRIVATE rea_warnings)

# ---------------------------------------------------------------------------
# Executável do jogo
# ---------------------------------------------------------------------------
add_executable(rea_cruisn src/platform/sdl/main.cpp)
target_link_libraries(rea_cruisn PRIVATE rea_platform_sdl ${REA_SDL_MAIN} rea_warnings)
target_compile_definitions(rea_cruisn PRIVATE
    REA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# Windows: programa de janela (sem a janela de texto do console; o SDL2main fornece o WinMain).
# `rea_cruisn --console` abre a janela de texto com as mensagens, para depurar.
if(WIN32)
    set_target_properties(rea_cruisn PROPERTIES WIN32_EXECUTABLE ON)
endif()
# libstdc++ e libgcc dentro do executável (GCC/Clang fora do macOS): a pasta distribuída não depende da versão
# delas no sistema (o GCC 13 exige GLIBCXX_3.4.32, ausente no Ubuntu 22.04 / Debian 12). O mínimo fica na glibc 2.34.
option(REA_STATIC_CXX_RUNTIME "Liga a libstdc++/libgcc estaticamente no rea_cruisn" ON)
if(REA_STATIC_CXX_RUNTIME AND MINGW)
    # Windows (MinGW): libstdc++, libgcc e winpthread dentro do .exe; ao lado dele só a SDL2.dll.
    target_link_options(rea_cruisn PRIVATE -static)
elseif(REA_STATIC_CXX_RUNTIME AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_options(rea_cruisn PRIVATE -static-libstdc++ -static-libgcc)
endif()

# ---------------------------------------------------------------------------
# Navegador (Emscripten): build-web/bin/index.html + index.js + index.wasm + index.data
#   emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release && cmake --build build-web
# A página pede o .zip da ROM do usuário (nada da ROM vai junto); index.data leva só assets/lang.
# ASYNCIFY: o laço de quadros cede ao navegador a cada quadro (emscripten_sleep) e a CPU roda numa
# fibra. As pilhas são grandes porque o código traduzido aninha muitas rotinas.
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    set(REA_WEB_SHELL ${CMAKE_SOURCE_DIR}/src/platform/web/shell.html)
    set_target_properties(rea_cruisn PROPERTIES OUTPUT_NAME index SUFFIX ".html"
        LINK_DEPENDS ${REA_WEB_SHELL})
    target_link_options(rea_cruisn PRIVATE
        -sASYNCIFY=1
        -sASYNCIFY_STACK_SIZE=1048576
        -sSTACK_SIZE=8388608
        -sINITIAL_MEMORY=134217728
        -sALLOW_MEMORY_GROWTH=1
        -sINVOKE_RUN=0
        -sEXIT_RUNTIME=1
        -sFORCE_FILESYSTEM=1
        -sEXPORTED_RUNTIME_METHODS=callMain,FS
        -lidbfs.js
        --shell-file ${REA_WEB_SHELL}
        --preload-file ${CMAKE_SOURCE_DIR}/assets/lang@/rea/assets/lang)
endif()

# ---------------------------------------------------------------------------
# Instalação: pasta pronta para rodar (executável + assets/ + rom/ + save/).
# Os recursos do jogo saem da ROM do usuário (rom/*.zip) durante a execução;
# o executável acha a pasta a partir de onde ele está (ResourceManager::findRoot).
#   cmake --install build            -> build/dist/
#   cmake --build build --target package  -> rea_cruisn-<versão>-<sistema>.zip
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    # Pasta da página: copie para qualquer servidor HTTP estático (não abre direto do disco, file://).
    install(FILES ${CMAKE_BINARY_DIR}/bin/index.html ${CMAKE_BINARY_DIR}/bin/index.js
                  ${CMAKE_BINARY_DIR}/bin/index.wasm ${CMAKE_BINARY_DIR}/bin/index.data DESTINATION .)
    install(FILES packaging/web/LEIA-ME.txt LICENSE packaging/LICENCAS-TERCEIROS.txt DESTINATION .)
    # Licenças do que vai dentro do index.js/index.wasm: runtime do Emscripten, musl, libc++ (LLVM) e SDL2.
    install(FILES packaging/licencas/emscripten.txt packaging/licencas/musl.txt packaging/licencas/llvm-libcxx.txt
                  packaging/licencas/SDL2.txt DESTINATION licencas)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME "rea_cruisn-${PROJECT_VERSION}-Web")
    include(CPack)
    return()  # sem testes nem documentação no build do navegador
endif()
install(TARGETS rea_cruisn RUNTIME DESTINATION .)
install(DIRECTORY assets/lang DESTINATION assets)
install(DIRECTORY DESTINATION assets/textures)
install(DIRECTORY DESTINATION assets/audio)
install(DIRECTORY DESTINATION assets/config)
install(DIRECTORY DESTINATION save)
install(FILES packaging/LEIA-ME.txt DESTINATION .)
install(FILES packaging/rom/LEIA-ME.txt DESTINATION rom)
# Licenças: a do projeto (GPL v3) e as dos componentes de terceiros que vão no pacote (packaging/LICENCAS-TERCEIROS.txt).
install(FILES LICENSE packaging/LICENCAS-TERCEIROS.txt DESTINATION .)
install(FILES packaging/licencas/gcc-runtime-exception.txt DESTINATION licencas)  # libstdc++/libgcc no executável
if(WIN32)
    install(FILES packaging/licencas/SDL2.txt packaging/licencas/mingw-w64.txt DESTINATION licencas)  # SDL2.dll e runtime do MinGW-w64
endif()
# Windows: copia as DLLs de que o executável depende (SDL2.dll) para junto dele.
if(WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.21)
    install(CODE "
        set(_rea_dlls \"$<TARGET_RUNTIME_DLLS:rea_cruisn>\")
        if(_rea_dlls)
            file(INSTALL \${_rea_dlls} DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
        endif()")
endif()
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "rea_cruisn-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
include(CPack)

# ---------------------------------------------------------------------------
# Testes (sem dependência de SDL2)
# ---------------------------------------------------------------------------
enable_testing()
add_executable(rea_core_tests
    tests/test_main.cpp
    tests/test_basics.cpp
    tests/test_video.cpp
    tests/test_scene.cpp
    tests/test_link.cpp
    tests/test_processes.cpp
    tests/test_io.cpp
    tests/test_audio.cpp
    tests/test_game.cpp
    tests/test_resources.cpp
    tests/test_service.cpp
    tests/test_rom_game.cpp
    tests/test_rom_system.cpp
    src/platform/link_shm.cpp  # cabo de link entre processos (não depende do SDL)
)
target_link_libraries(rea_core_tests PRIVATE rea_core rea_warnings Threads::Threads)
if(MINGW)
    target_link_options(rea_core_tests PRIVATE -static)  # roda no Wine/Windows sem as DLLs do MinGW
endif()
target_compile_definitions(rea_core_tests PRIVATE
    REA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME core_tests COMMAND rea_core_tests)

# ---------------------------------------------------------------------------
# Documentação (Doxygen): `cmake --build build --target docs`
# Saída em build/docs/html; avisos de itens sem Docblock em build/docs/warnings.log.
# ---------------------------------------------------------------------------
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "Cruis'n USA - port REA")
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/docs)
    set(DOXYGEN_OUTPUT_LANGUAGE Brazilian)
    set(DOXYGEN_INPUT_ENCODING UTF-8)
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_FILE_PATTERNS *.hpp *.cpp)
    set(DOXYGEN_EXTRACT_ALL NO)
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_EXTRACT_ANON_NSPACES YES)
    set(DOXYGEN_EXTRACT_LOCAL_CLASSES YES)
    set(DOXYGEN_DISTRIBUTE_GROUP_DOC YES)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
    set(DOXYGEN_WARN_NO_PARAMDOC NO)
    set(DOXYGEN_WARN_LOGFILE ${CMAKE_BINARY_DIR}/docs/warnings.log)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_QUIET YES)
    set(DOXYGEN_STRIP_FROM_PATH ${CMAKE_SOURCE_DIR})
    doxygen_add_docs(docs ${CMAKE_SOURCE_DIR}/src COMMENT "Gerando documentação Doxygen")
endif()
