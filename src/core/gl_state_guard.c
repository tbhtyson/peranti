#include "gl_state_guard.h"

// Raw GL function/constant declarations are already visible in this
// translation unit via sokol_gfx.h's own <GL/gl.h> include under
// SOKOL_GLCORE -- this file must only ever be #include'd (via all.c)
// AFTER sokol_impl_luanti.c in the same translation unit, or these won't
// resolve. No separate GL header include here on purpose, to avoid
// disagreeing with whichever GL loader sokol itself already pulled in.

#ifdef __cplusplus
extern "C" {
  #endif

  void peranti_gl_state_save(PerantiGLState *out_state) {
    glGetIntegerv(GL_DEPTH_FUNC, &out_state->depth_func);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &out_state->depth_mask);
    out_state->depth_test_enabled = (uint8_t)glIsEnabled(GL_DEPTH_TEST);
    out_state->blend_enabled = (uint8_t)glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_BLEND_SRC_RGB, &out_state->blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &out_state->blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &out_state->blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &out_state->blend_dst_alpha);
    out_state->cull_face_enabled = (uint8_t)glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CULL_FACE_MODE, &out_state->cull_face_mode);
    glGetIntegerv(GL_FRONT_FACE, &out_state->front_face);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &out_state->bound_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &out_state->bound_array_buffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &out_state->bound_element_array_buffer);

    // Save the currently-active unit, then walk all 4 units querying each
    // one's bound 2D texture, restoring the active unit selector afterward
    // so this function itself doesn't leave GL_ACTIVE_TEXTURE changed.
    glGetIntegerv(GL_ACTIVE_TEXTURE, &out_state->active_texture_unit);
    for (int i = 0; i < 4; i++) {
      glActiveTexture((GLenum)(GL_TEXTURE0 + i));
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &out_state->bound_texture_2d[i]);
    }
    glActiveTexture((GLenum)out_state->active_texture_unit);
  }

  void peranti_gl_state_restore(const PerantiGLState *state) {
    glDepthFunc((GLenum)state->depth_func);
    glDepthMask((GLboolean)state->depth_mask);
    if (state->depth_test_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (state->blend_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFuncSeparate((GLenum)state->blend_src_rgb, (GLenum)state->blend_dst_rgb,
                        (GLenum)state->blend_src_alpha, (GLenum)state->blend_dst_alpha);
    if (state->cull_face_enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glCullFace((GLenum)state->cull_face_mode);
    glFrontFace((GLenum)state->front_face);
    glBindVertexArray((GLuint)state->bound_vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)state->bound_array_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)state->bound_element_array_buffer);

    for (int i = 0; i < 4; i++) {
      glActiveTexture((GLenum)(GL_TEXTURE0 + i));
      glBindTexture(GL_TEXTURE_2D, (GLuint)state->bound_texture_2d[i]);
    }
    glActiveTexture((GLenum)state->active_texture_unit);
  }

  #ifdef __cplusplus
}
#endif
