#!/usr/bin/env bash
set -e
rm -rf build/*
mkdir -p build

GLFW_INC="third_party/glfw/include"
GLFW_LIB="third_party/glfw/build/src"

# GLAD_INC="third_party/glad/include"
# GLAD_SRC="third_party/glad/src/gl.c"

WGPU_INC="third_party/wgpu-native/ffi"
WGPU_LIB="third_party/wgpu-native/target/release"

CFLAGS="-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Ithird_party/wgpu-native/ffi/webgpu-headers -I${GLFW_INC} -I${WGPU_INC}"
LDFLAGS="-L${GLFW_LIB} -L${WGPU_LIB} -lglfw3 -l:libwgpu_native.a -lm -ldl -lpthread -lX11"

cmake -S third_party/glfw -B third_party/glfw/build -DBUILD_SHARED_LIBS=OFF -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF -DGLFW_BUILD_WAYLAND=OFF
cmake --build third_party/glfw/build
cargo build --release --manifest-path third_party/wgpu-native/Cargo.toml

gcc $CFLAGS -c src/main.c -o build/main.o
gcc build/*.o $LDFLAGS -o build/peranti
