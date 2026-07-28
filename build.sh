#!/usr/bin/env bash
set -e
rm -rf build/*
mkdir -p build
mkdir -p generated

cd third_party/sokol_tools
./fibs build
cd ../..

SHDC_BIN="$(find third_party/sokol_tools/.fibs/dist -name sokol-shdc -type f | head -1)"
if [ -z "$SHDC_BIN" ]; then
    echo "error: could not find built sokol-shdc binary under third_party/sokol_tools/.fibs/dist" >&2
    exit 1
fi

GLFW_INC="third_party/glfw/include"
GLFW_LIB="third_party/glfw/build/src"

SOKOL_INC="third_party/sokol"
SOKOL_LIB="third_party/sokol"

read -p "Are you on MacOS? (y/N) " platform

"$SHDC_BIN" -i shaders/triangle.glsl -o generated/triangle.glsl.h -l spirv_vk:glsl410:metal_macos:hlsl5

if [ "$platform" == "y" ]; then
    EXTRA_CFLAGS="-x objective-c"
    EXTRA_LDFLAGS="-framework Cocoa -framework QuartzCore -framework Metal -framework AudioToolbox"
else
  EXTRA_CFLAGS=" "
    EXTRA_LDFLAGS="-lvulkan -lGL -ldl -lX11 -lXi -lXcursor"
fi

CFLAGS="-Wall -Wextra -Wpedantic -Wshadow -Wconversion -I${GLFW_INC} -I${SOKOL_INC} -Igenerated"
LDFLAGS="-L${GLFW_LIB} -lm -lpthread -lX11"

cmake -S third_party/glfw -B third_party/glfw/build -DBUILD_SHARED_LIBS=OFF -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF -DGLFW_BUILD_WAYLAND=OFF
cmake --build third_party/glfw/build

gcc $EXTRA_CFLAGS $CFLAGS -c "src/sokol_impl.c" -o "sokol_impl.o"

for src_file in src/*.c; do
    obj_file="build/$(basename "${src_file%.c}").o"
    if [ "$(basename "$src_file")" != "sokol_impl.c" ]; then
        gcc $CFLAGS -c "$src_file" -o "$obj_file"
    fi
done

gcc build/*.o sokol_impl.o $LDFLAGS $EXTRA_LDFLAGS -o build/peranti
