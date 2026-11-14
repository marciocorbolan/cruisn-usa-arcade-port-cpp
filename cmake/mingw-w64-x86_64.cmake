# Toolchain de compilação cruzada para Windows x86_64 com MinGW-w64 (serviço "win" do docker-compose.yml).
#   cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Variante POSIX do MinGW: tem std::thread/std::mutex (a variante win32 do GCC 13 não tem).
set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# Bibliotecas e cabeçalhos só do sysroot do MinGW e do SDL2 para MinGW (nunca os do Linux).
set(REA_SDL2_MINGW_DIR "/opt/sdl2-mingw" CACHE PATH "SDL2 para MinGW (pasta x86_64-w64-mingw32 do pacote oficial)")
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 ${REA_SDL2_MINGW_DIR})
set(CMAKE_PREFIX_PATH ${REA_SDL2_MINGW_DIR})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Executáveis de teste rodam pelo Wine (ctest, add_test).
set(CMAKE_CROSSCOMPILING_EMULATOR wine)
