build_shaders() {
  cd src/third_party/sokol_tools
  ./fibs config
  ./fibs build
  cd ../../..
  SHDC_BIN="$(find src/third_party/sokol_tools/.fibs/dist -name sokol-shdc -type f | head -1)"
  if [ -z "$SHDC_BIN" ]; then
    echo "error: could not find built sokol-shdc binary under src/third_party/sokol_tools/.fibs/dist" >&2
    exit 1
  fi
  # glsl430, not glsl410 -- sokol-shdc's own docs explicitly state storage
  # buffers are NOT supported for glsl300es or glsl410 output. Vertex
  # pulling needs storage buffers, so glsl430 is a hard requirement, not
  # a style choice. This also raises the real runtime floor to GL 4.3 --
  # EDT_OPENGL3 alone (already checked at sg_setup time) doesn't guarantee
  # the underlying context is actually 4.3+, worth an explicit runtime
  # version check before relying on this shader.
  "$SHDC_BIN" -i shaders/terrain.glsl -o src/generated/terrain.glsl.h -l glsl430
}
build_shaders
