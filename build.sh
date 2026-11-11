#!/bin/bash
# Baut Vogelflug. Aufruf:  ./build.sh linux | windows | all
# Voraussetzungen (Ubuntu): g++, g++-mingw-w64-x86-64-posix, glslang-tools, python3 (+Pillow fuer den Font),
# sowie in DEPS: volk, Vulkan-Headers und statisch gebautes GLFW 3.4 (siehe README).
set -e
cd "$(dirname "$0")"
DEPS=${DEPS:-../deps}
mkdir -p build/spv dist
SH="sky.comp rt.comp temporal.comp atrous.comp composite.comp taa.comp bloom.comp exposure.comp final.vert final.frag"
SPV=""
for f in $SH; do glslangValidator --quiet -V --target-env vulkan1.3 -o build/spv/$f.spv shaders/$f; SPV="$SPV build/spv/$f.spv"; done
python3 tools/embed.py src/shaders.h $SPV
[ -f src/font.h ] || python3 tools/mkfont.py src/font.h
INC="-I$DEPS/volk -I$DEPS/Vulkan-Headers/include"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers"
if [ "$1" = "linux" ] || [ "$1" = "all" ]; then
  gcc -O2 $INC -c $DEPS/volk/volk.c -o build/volk_linux.o
  g++ $CXXFLAGS $INC -I$DEPS/linux/include src/main.cpp src/world.cpp build/volk_linux.o -o build/vogelflug $DEPS/linux/lib/libglfw3.a -lX11 -ldl -lpthread -lm
  echo "OK: build/vogelflug"
fi
if [ "$1" = "windows" ] || [ "$1" = "all" ]; then
  x86_64-w64-mingw32-gcc-posix -O2 $INC -DVK_USE_PLATFORM_WIN32_KHR -c $DEPS/volk/volk.c -o build/volk_win.o
  x86_64-w64-mingw32-g++-posix $CXXFLAGS $INC -I$DEPS/win/include src/main.cpp src/world.cpp build/volk_win.o -o dist/Vogelflug.exe \
    $DEPS/win/lib/libglfw3.a -lgdi32 -luser32 -lshell32 -static -static-libgcc -static-libstdc++ -mwindows -s
  echo "OK: dist/Vogelflug.exe"
fi
