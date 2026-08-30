#!/usr/bin/env bash
set -e

# There's no standalone runnable target right now (the dev harness and
# network stack were both dropped from this rewrite) -- this script's job
# is verification, not producing a binary:
#   1. compat_lint.sh -- the two known C-vs-C++ idioms
#   2. every core/render file compiles clean as plain C
#   3. src/all.c (the actual graft aggregation point) compiles clean as
#      BOTH C and C++, the second one being the real "does this survive
#      being pulled into Luanti's .cpp files" check -- this used to be
#      something run by hand in a sandbox; baking it in here means it
#      can't silently stop happening.
#
# If/when a standalone dev harness comes back, this script gains a real
# link step; until then, "everything here compiles" is the whole contract.

if ! command -v zig >/dev/null 2>&1; then
  echo "error: zig not found -- install zig (https://ziglang.org/download/) and ensure it's on PATH" >&2
  exit 1
fi

if [ -x ./compat_lint.sh ]; then
  echo "== running compat_lint.sh =="
  ./compat_lint.sh
else
  echo "error: compat_lint.sh not found or not executable at repo root" >&2
  exit 1
fi

mkdir -p src/generated
./compile_shaders.sh

SOKOL_INC="third_party/sokol"
WARN_FLAGS="-Wall -Wextra -Wpedantic -Wshadow -Wconversion"
INCLUDES="-Isrc -I${SOKOL_INC} -Ithird_party/gmp/mini-gmp"

OUT_DIR=/tmp/peranti_build_check
rm -rf "$OUT_DIR" && mkdir -p "$OUT_DIR"

echo
echo "== compiling core/ and render/ as plain C =="
FAILED=0
while IFS= read -r -d '' src_file; do
  base="$(basename "${src_file%.c}")"
  echo "  cc  $src_file"
  if ! zig cc $WARN_FLAGS $INCLUDES -c "$src_file" -o "$OUT_DIR/${base}.o" 2>&1; then
    echo "!!! FAILED: $src_file"
    FAILED=1
  fi
done < <(find src/core src/render -name "*.c" -print0 2>/dev/null)

echo
echo "== compiling src/sokol_impl_luanti.c and src/sokol_impl_standalone.c as plain C =="
for f in src/sokol_impl_luanti.c src/sokol_impl_standalone.c; do
  [ -f "$f" ] || continue
  echo "  cc  $f"
  if ! zig cc $WARN_FLAGS $INCLUDES -c "$f" -o "$OUT_DIR/$(basename "${f%.c}").o" 2>&1; then
    echo "!!! FAILED: $f"
    FAILED=1
  fi
done

echo
echo "== graft dry run: src/all.c as plain C =="
if ! zig cc $WARN_FLAGS $INCLUDES -c src/all.c -o "$OUT_DIR/all_c.o" 2>&1; then
  echo "!!! FAILED: src/all.c (as C)"
  FAILED=1
fi

echo
echo "== graft dry run: src/all.c as C++, no extern \"C\" wrapping =="
echo "== (this is the actual check that matters -- confirms every file"
echo "==  under core/ and render/ survives being #include'd into a"
echo "==  Luanti .cpp exactly the way the real graft call sites do)"
cp src/all.c "$OUT_DIR/all_dryrun.cpp"
if ! zig c++ $WARN_FLAGS $INCLUDES -c "$OUT_DIR/all_dryrun.cpp" -o "$OUT_DIR/all_cpp.o" 2>&1; then
  echo "!!! FAILED: src/all.c (as C++ -- this WILL break inside Luanti's real"
  echo "!!! build too, since this dry run mirrors the actual graft exactly)"
  FAILED=1
fi

echo
if [ "$FAILED" -eq 0 ]; then
  echo "build-zig.sh: all checks passed."
else
  echo "build-zig.sh: FAILED -- see above." >&2
  exit 1
fi
