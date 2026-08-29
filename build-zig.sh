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

# CONVENTIONS.md rule enforcement gates the build, not just an optional
# habit -- catching a compound-literal-address-of or an uncast malloc here
# is a lint failure; catching it later inside Luanti's tree is a confusing
# C++ compile error far from the file that actually caused it.
if [ -x ./compat_lint.sh ]; then
  echo "== running compat_lint.sh =="
  ./compat_lint.sh
else
  echo "error: compat_lint.sh not found or not executable at repo root" >&2
  exit 1
fi

mkdir -p generated
SOKOL_INC="third_party/sokol"
WARN_FLAGS="-Wall -Wextra -Wpedantic -Wshadow -Wconversion"

# This is the standalone dev binary only. It builds:
#   - src/standalone/*.c        (sokol_app-owning entry point + main loop)
#   - src/core/*.c              (pure data-layer, zero sokol dependency)
#   - src/render/*.c            (sokol_gfx calls), EXCEPT sokol_impl_luanti.c
#   - src/net/*.c               (dormant network stack, if present)
#
# src/render/sokol_impl_luanti.c is NEVER part of this build -- it's the
# graft target for embedding inside a host application (Luanti) that owns
# its own window/GL context, which is exactly what this standalone binary
# is not. Building both sokol_impl_standalone.c and sokol_impl_luanti.c
# into the same binary would double-define sokol_gfx's implementation.
INCLUDES="-Isrc/core -Isrc/render -Isrc/standalone -I${SOKOL_INC} -Igenerated -Ithird_party/gmp/mini-gmp"

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

# Collects every .c file that belongs in the standalone build for the
# current platform, honoring:
#   - sokol_impl_luanti.c is always excluded (see comment above)
#   - net_udp_win32.c / net_udp_posix.c are mutually exclusive by platform
#     (CONVENTIONS.md: platform-split source files, not #ifdef'd in one
#     file, matching the existing project convention)
#   - src/net/ is optional -- the network stack is dormant per the roadmap,
#     but still builds if the directory exists and has content, so it
#     doesn't bit-rot silently.
collect_sources() {
  local wrong_platform_net_file="$1"
  local srcs=()

  while IFS= read -r -d '' f; do srcs+=("$f"); done \
    < <(find src/core -name "*.c" -print0 2>/dev/null)

  while IFS= read -r -d '' f; do
    [ "$(basename "$f")" = "sokol_impl_luanti.c" ] && continue
    srcs+=("$f")
  done < <(find src/render -name "*.c" -print0 2>/dev/null)

  if [ -d src/net ]; then
    while IFS= read -r -d '' f; do
      [ -n "$wrong_platform_net_file" ] && [ "$(basename "$f")" = "$wrong_platform_net_file" ] && continue
      srcs+=("$f")
    done < <(find src/net -name "*.c" -print0 2>/dev/null)
    # srp/mini-gmp only needed if the (dormant) network/auth code is present
    srcs+=("third_party/csrp/srp.c" "third_party/gmp/mini-gmp/mini-gmp.c")
  fi

  printf '%s\n' "${srcs[@]}"
}

compile_sources() {
  local zig_target="$1" cflags="$2" out_dir="$3" impl_cflags="$4" wrong_platform_net_file="$5"
  mkdir -p "$out_dir"
  local target_flag=()
  [ -n "$zig_target" ] && target_flag=(-target "$zig_target")

  zig cc "${target_flag[@]}" $impl_cflags $cflags -c src/standalone/sokol_impl_standalone.c \
    -o "$out_dir/sokol_impl_standalone.o"

  while IFS= read -r src_file; do
    [ -z "$src_file" ] && continue
    local base obj_file
    base="$(basename "$src_file")"
    obj_file="$out_dir/${base%.c}.o"
    zig cc "${target_flag[@]}" $cflags -c "$src_file" -o "$obj_file"
  done < <(collect_sources "$wrong_platform_net_file")
}

build_linux_vulkan() {
  echo "== building linux (vulkan) =="
  rm -rf build/linux-vulkan/*.o
  local cflags="$WARN_FLAGS -O3 -march=native -flto -DSOKOL_USE_VULKAN $INCLUDES"
  compile_sources "" "$cflags" build/linux-vulkan " " "net_udp_win32.c"
  zig cc build/linux-vulkan/*.o -lm -lpthread -lX11 -lXi -lXcursor -lvulkan -ldl -flto -o build/linux-vulkan/peranti
}

build_linux_glcore() {
  echo "== building linux (glcore) =="
  rm -rf build/linux-glcore/*.o
  local cflags="$WARN_FLAGS -O3 -march=native -flto $INCLUDES"
  compile_sources "" "$cflags" build/linux-glcore " " "net_udp_win32.c"
  zig cc build/linux-glcore/*.o -lm -lpthread -lX11 -lXi -lXcursor -lGL -ldl -flto -o build/linux-glcore/peranti-gl
}

build_macos() {
  echo "== building macos (cross-compiled via zig) =="
  rm -rf build/macos/*.o
  local target="x86_64-macos"
  local cflags="$WARN_FLAGS -O3 -flto $INCLUDES"
  compile_sources "$target" "$cflags" build/macos "-x objective-c" "net_udp_win32.c"
  zig cc -target "$target" build/macos/*.o -lm -framework Cocoa -framework QuartzCore -framework Metal -framework AudioToolbox -flto -o build/macos/peranti
}

build_windows() {
  echo "== building windows (cross-compiled via zig) =="
  rm -rf build/windows/*.o
  local target="x86_64-windows-gnu"
  local cflags="$WARN_FLAGS -O3 -flto $INCLUDES"
  compile_sources "$target" "$cflags" build/windows " " "net_udp_posix.c"
  zig cc -target "$target" build/windows/*.o -lbcrypt -lkernel32 -luser32 -lshell32 -ld3d11 -ldxgi -flto -o build/windows/peranti.exe
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
