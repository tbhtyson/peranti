#!/usr/bin/env bash
set -e

GLFW_INC="third_party/glfw/include"
GLFW_LIB="third_party/glfw/build/src"

CFLAGS="-Wall -Wextra -Wpedantic -Wshadow -Wconversion -I${GLFW_INC}"
LDFLAGS="-L${GLFW_LIB} -lglfw3 -lm -ldl -lpthread -lX11 -lvulkan"

cmake -S third_party/glfw -B third_party/glfw/build -DBUILD_SHARED_LIBS=OFF -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF -DGLFW_BUILD_WAYLAND=OFF
cmake --build third_party/glfw/build
gcc $CFLAGS -c src/main.c -o build/main.o
gcc build/main.o $LDFLAGS -o build/peranti
