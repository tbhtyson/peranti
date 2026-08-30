#ifndef PERANTI_GL_STATE_GUARD_H
#define PERANTI_GL_STATE_GUARD_H

#ifdef __cplusplus
extern "C" {
  #endif

  #include <stdint.h>

  // Snapshot of the specific GL state sg_setup() (and, later, every sokol
  // render pass) is known to change, which Irrlicht's own CacheHandler
  // independently caches and never re-queries from real GL state -- see the
  // investigation that led here: Irrlicht's cache is initialized once at
  // driver creation (DepthFunc=GL_LESS, DepthMask=true, Blend=false) and
  // only re-issues a GL call when a material's requested value DIFFERS from
  // its cached belief. Since normal materials keep requesting the same
  // values Irrlicht already believes are active, a change made behind its
  // back (by sokol) is never detected or corrected.
  //
  // Core-profile GL (EDT_OPENGL3) has no glPushAttrib/glPopAttrib -- that
  // mechanism is compatibility-profile only and was removed from core
  // entirely -- so this manual save/restore is the only way to hand GL
  // state back to Irrlicht without editing Irrlicht's own vendored source
  // (which would otherwise let us just flip its private ResetRenderStates
  // flag).
  //
  // This must bracket EVERY block of sokol calls that touches the shared GL
  // context, not just sg_setup(). Once render/ issues real per-frame draws,
  // wrap that whole pass (save once before, restore once after) -- restoring
  // around every individual sg_* call would be correct but needlessly slow.
  //
  // Plain integer/byte types here, not GLenum/GLboolean/GLuint, to keep this
  // header decoupled from requiring a GL header be visible to every
  // includer -- the .c file needs real GL declarations (available via
  // sokol_gfx.h's own <GL/gl.h> include, since this file must be #include'd
  // after sokol_impl_luanti.c in the same translation unit), but callers of
  // this header don't need to know that.
  typedef struct {
    int32_t depth_func;
    uint8_t depth_mask;
    uint8_t depth_test_enabled;
    uint8_t blend_enabled;
    int32_t blend_src_rgb, blend_dst_rgb;
    int32_t blend_src_alpha, blend_dst_alpha;
    uint8_t cull_face_enabled;
    int32_t cull_face_mode;
    int32_t front_face;
    int32_t bound_vao;
    // sokol's buffer-management routines explicitly rebind these during
    // sg_setup() (confirmed in sokol_gfx.h's buffer create/destroy paths),
    // separately from the VAO binding above -- GL_ARRAY_BUFFER/
    // GL_ELEMENT_ARRAY_BUFFER are per-context globals, not part of VAO state
    // itself, so restoring the VAO alone doesn't restore these.
    int32_t bound_array_buffer;
    int32_t bound_element_array_buffer;
    // sokol's own texture-pool initialization (during sg_setup()) rebinds
    // real GL texture IDs on one or more texture units. Irrlicht's own
    // texture-binding cache (Texture[index] in COpenGLCoreCacheHandler) is
    // keyed by C++ ITexture* pointer, not GL texture ID -- it only
    // re-issues glBindTexture when the POINTER changes, which it never does
    // for a texture Irrlicht keeps reusing every frame (e.g. a font atlas
    // or background image). So a real GL rebind behind Irrlicht's back on
    // one of these 4 units (MATERIAL_MAX_TEXTURES, confirmed in
    // SMaterial.h) is invisible to Irrlicht's cache and never gets
    // corrected on its own -- this is the same class of bug as the
    // depth-func/blend one, one layer removed.
    int32_t active_texture_unit;
    int32_t bound_texture_2d[4];
  } PerantiGLState;

  void peranti_gl_state_save(PerantiGLState *out_state);
  void peranti_gl_state_restore(const PerantiGLState *state);

  #ifdef __cplusplus
}
#endif

#endif
