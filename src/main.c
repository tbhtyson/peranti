#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#include "sokol_time.h"
#include "triangle.glsl.h"
#include <stdio.h>
// 1. Define global state structure
static struct {
    sg_pipeline pip;
    sg_bindings bind;
    sg_pass_action pass_action;
} state;

typedef struct {
    float x, y, z;
    float r, g, b, a;
} vertex_t;

static uint64_t last_time = 0;
static float fps = 0.0f;
int count = 0;

static float identity_mvp[16] = {
  1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, 1.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f,
};

void init(void) {
  sg_setup(&(sg_desc){
      .environment = sglue_environment(),
      .logger.func = slog_func,
  });

  stm_setup(); // Initialize sokol_time
  last_time = stm_now();


  sg_features features = sg_query_features(); // moved here, context now exists
  if (!features.compute) {
    printf("Compute shader support not found.\n");
    #define NO_COMPUTE_SHADERS
  }
  
  

  vertex_t vertices[] = {
      {0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
      {-0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
      {0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
  };

  state.bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
      .data = SG_RANGE(vertices),
  });

  sg_shader shader = sg_make_shader(triangle_shader_desc(sg_query_backend()));

  state.pip = sg_make_pipeline(&(sg_pipeline_desc){
      .shader = shader,
      .layout = {
          .attrs = {
              [0] = {.format = SG_VERTEXFORMAT_FLOAT3},
              [1] = {.format = SG_VERTEXFORMAT_FLOAT4},
          },
      },
  });

  state.pass_action = (sg_pass_action){
      .colors[0] = {.load_action = SG_LOADACTION_CLEAR,
                    .clear_value = {0.5f, 0.8f, 0.9f, 1.0f}},
  };
}

void frame(void) {
  // Measure frame time
  uint64_t frame_ticks = stm_laptime(&last_time);
  float frame_seconds = (float) stm_sec(frame_ticks);
    
  if (frame_seconds > 0.0f) {
      fps = 1.0f / frame_seconds;
  }
  if(count < 15) {
    count++;
  } else {
    printf("%f\n", fps);
    count = 0;
  }
  sg_begin_pass(&(sg_pass){.action = state.pass_action, .swapchain = sglue_swapchain()});
  sg_apply_pipeline(state.pip);
  sg_apply_bindings(&state.bind);
  sg_apply_uniforms(UB_vs_params, &SG_RANGE(identity_mvp));
  sg_draw(0, 3, 1);
  sg_end_pass();
  sg_commit();
}

void cleanup(void) {
  sg_shutdown();
}

sapp_desc sokol_main(int argc, char **argv) {
  
  
  
  return (sapp_desc){
    .init_cb = init,
    .frame_cb = frame,
    .cleanup_cb = cleanup,
    .width = 1280,
    .height = 720,
    .window_title = "Voxel Engine",
  };
}
