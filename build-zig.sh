#!/usr/bin/env bash
set -e

TARGET="${1:-}"
if [ -z "$TARGET" ]; then
  echo "usage: $0 <linux|linux_glcore|linux_vulkan|macos|windows|all>" >&2
  exit 1
fi

if ! command -v zig >/dev/null 2>&1; then
  echo "error: zig not found -- install zig (https://ziglang.org/download/) and ensure it's on PATH" >&2
  exit 1
fi

mkdir -p generated
SOKOL_INC="third_party/sokol"
WARN_FLAGS="-Wall -Wextra -Wpedantic -Wshadow -Wconversion"

build_shaders() {
  cd third_party/sokol_tools
  ./fibs build
  cd ../..
  SHDC_BIN="$(find third_party/sokol_tools/.fibs/dist -name sokol-shdc -type f | head -1)"
  if [ -z "$SHDC_BIN" ]; then
    echo "error: could not find built sokol-shdc binary under third_party/sokol_tools/.fibs/dist" >&2
    exit 1
  fi
  "$SHDC_BIN" -i shaders/triangle.glsl -o generated/triangle.glsl.h \
    -l spirv_vk:glsl410:metal_macos:hlsl5
}

compile_sources() {
  local zig_target="$1" cflags="$2" out_dir="$3" impl_cflags="$4"
  mkdir -p "$out_dir"
  local target_flag=()
  [ -n "$zig_target" ] && target_flag=(-target "$zig_target")
  zig cc "${target_flag[@]}" $impl_cflags $cflags -c src/sokol_impl.c -o "$out_dir/sokol_impl.o"
  for src_file in src/*.c; do
    [ "$(basename "$src_file")" = "sokol_impl.c" ] && continue
    obj_file="$out_dir/$(basename "${src_file%.c}").o"
    zig cc "${target_flag[@]}" $cflags -c "$src_file" -o "$obj_file"
  done
}

build_linux_vulkan() {
  echo "== building linux (vulkan) =="
  local target="x86_64-linux-gnu"
  local cflags="$WARN_FLAGS -O3 -march=native -flto -DSOKOL_USE_VULKAN -I${SOKOL_INC} -Igenerated"
  compile_sources "" "$cflags" build/linux-vulkan " "
  zig cc build/linux-vulkan/*.o -lm -lpthread -lX11 -lXi -lXcursor -lvulkan -ldl -flto -o build/linux-vulkan/peranti # -target "$target"
}

build_linux_glcore() {
  echo "== building linux (glcore) =="
  local target="x86_64-linux-gnu"
  local cflags="$WARN_FLAGS -O3 -march=native -flto -I${SOKOL_INC} -Igenerated"
  compile_sources "" "$cflags" build/linux-glcore " "
  zig cc build/linux-glcore/*.o -lm -lpthread -lX11 -lXi -lXcursor -lGL -ldl -flto -o build/linux-glcore/peranti-gl # -target "$target"
}

build_macos() {
  echo "== building macos (cross-compiled via zig) =="
  local target="x86_64-macos"
  local cflags="$WARN_FLAGS -O3 -flto -I${SOKOL_INC} -Igenerated"
  compile_sources "$target" "$cflags" build/macos "-x objective-c"
  zig cc -target "$target" build/macos/*.o -lm -framework Cocoa -framework QuartzCore -framework Metal -framework AudioToolbox -flto -o build/macos/peranti
}

build_windows() {
  echo "== building windows (cross-compiled via zig) =="
  local target="x86_64-windows-gnu"
  local cflags="$WARN_FLAGS -O3 -flto -I${SOKOL_INC} -Igenerated"
  compile_sources "$target" "$cflags" build/windows " "
  zig cc -target "$target" build/windows/*.o -lkernel32 -luser32 -lshell32 -ld3d11 -ldxgi -flto -o build/windows/peranti.exe
}

build_shaders
case "$TARGET" in
  linux)        build_linux_vulkan; build_linux_glcore ;;
  linux_glcore) build_linux_glcore ;;
  linux_vulkan) build_linux_vulkan ;;
  # macos)        build_macos ;;
  windows)      build_windows ;;
  all)          build_linux_vulkan; build_linux_glcore; build_windows ;; # build_macos if on mac
  *)
    echo "error: unknown target '$TARGET' (expected linux|linux_glcore|linux_vulkan|macos|windows|all)" >&2
    exit 1
    ;;
esac
echo "This build script is designed to run on a linux box. If not on linux, good luck. This build was also for x86_64, not arm. If building for Asahi linux, edit this script."
echo "If you are building for mac, don't bother. Just Docker GUI + XQuartz with the linux build."
