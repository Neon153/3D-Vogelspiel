#!/bin/bash
# Holt und baut die Abhaengigkeiten (volk, Vulkan-Headers, GLFW 3.4) nach ../deps.
# Ubuntu/Debian:  sudo apt install g++ cmake git glslang-tools python3-pil libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
#                                  g++-mingw-w64-x86-64-posix
set -e
DEPS=${DEPS:-$(cd "$(dirname "$0")/.." && pwd)/deps}
mkdir -p "$DEPS" && cd "$DEPS"
[ -d volk ] || git clone --depth 1 https://github.com/zeux/volk.git
[ -d Vulkan-Headers ] || git clone --depth 1 --branch vulkan-sdk-1.4.304.0 https://github.com/KhronosGroup/Vulkan-Headers.git
[ -d glfw ] || git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git
cat > mingw-toolchain.cmake <<'EOT'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOT
OPTS="-DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release"
cmake -S glfw -B glfw-win $OPTS -DCMAKE_TOOLCHAIN_FILE=$PWD/mingw-toolchain.cmake -DCMAKE_INSTALL_PREFIX=$PWD/win && cmake --build glfw-win -j --target install
cmake -S glfw -B glfw-linux $OPTS -DGLFW_BUILD_WAYLAND=OFF -DCMAKE_INSTALL_PREFIX=$PWD/linux && cmake --build glfw-linux -j --target install
echo "Abhaengigkeiten fertig in $DEPS"
