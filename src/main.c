#include "camera.h"
#include "chunk.h"
#include "chunk_mesh.h"
#include "mat4.h"
#include "mesh.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "triangle.glsl.h"
#include <stdio.h>

#define PERANTI_PI 3.14159265358979323846f
#define PERANTI_FOV_Y (60.0f * PERANTI_PI / 180.0f)
#define PERANTI_Z_NEAR 0.1f
#define PERANTI_Z_FAR 1000.0f

// 1. Define global state structure
static struct {
  sg_pipeline pip; // existing test-cube pipeline, UINT16
  sg_bindings bind;
  sg_pass_action pass_action;

  sg_pipeline chunk_pip; // new — chunks use UINT32 indices
  sg_bindings chunk_bind;
  uint32_t chunk_index_count;
} state;

static Chunk test_chunk;

/*static const Vec3 cube_positions[] = {
    {0.0f, 0.0f, 0.0f},
    {2.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 2.0f},
};
#define CUBE_COUNT (sizeof(cube_positions) / sizeof(cube_positions[0]))*/

static uint64_t last_time = 0;
static float fps = 0.0f;
int count = 0;

static Mat4 projection;
static FreeCamera
    camera; // needs your real struct — position/yaw/pitch, per memory

static bool keys_down[SAPP_MAX_KEYCODES];

void input_event(const sapp_event *ev) {
  if (ev->type == SAPP_EVENTTYPE_KEY_DOWN)
    keys_down[ev->key_code] = true;
  if (ev->type == SAPP_EVENTTYPE_KEY_UP)
    keys_down[ev->key_code] = false;
}

void init(void) {
  sg_setup(&(sg_desc){
      .environment = sglue_environment(),
      .logger.func = slog_func,
  });
  // in init(), after sg_setup():
  projection =
      mat4_perspective(PERANTI_FOV_Y, (float)sapp_width() / sapp_height(),
                       PERANTI_Z_NEAR, PERANTI_Z_FAR);

  camera = (FreeCamera){
      .position = {8.0f, 20.0f, 40.0f},
      .yaw = 0.0f,
      .pitch =
          -0.5f, // look down/toward origin — adjust to your pitch convention
  };

  stm_setup(); // Initialize sokol_time
  last_time = stm_now();

  sg_features features = sg_query_features(); // moved here, context now exists
  if (!features.compute) {
    printf("Compute shader support not found.\n");
#define NO_COMPUTE_SHADERS
  }

  // state.bind = mesh_cube_create();

  sg_shader shader = sg_make_shader(triangle_shader_desc(sg_query_backend()));

  /*state.pip = sg_make_pipeline(&(sg_pipeline_desc){
      .shader = shader,
      .index_type = SG_INDEXTYPE_UINT16,
      .cull_mode = SG_CULLMODE_BACK,
      .face_winding = SG_FACEWINDING_CCW,
      .depth =
          {
              .write_enabled = true,
              .compare = SG_COMPAREFUNC_LESS_EQUAL,
          },
      .layout =
          {
              .attrs =
                  {
                      [0] = {.format = SG_VERTEXFORMAT_FLOAT3},
                      [1] = {.format = SG_VERTEXFORMAT_FLOAT4},
                  },
          },
  });*/

  state.pass_action = (sg_pass_action){
      .colors[0] = {.load_action = SG_LOADACTION_CLEAR,
                    .clear_value = {0.5f, 0.8f, 0.9f, 1.0f}},
      // Add depth clear action here:
      .depth = {.load_action = SG_LOADACTION_CLEAR, .clear_value = 1.0f}};

  // Second pipeline: same shader, same vertex layout, only index_type differs.
  // Can't reuse state.pip since a pipeline's index_type is fixed at creation.
  state.chunk_pip = sg_make_pipeline(&(sg_pipeline_desc){
      .shader = shader, // same shader object, reused
      .index_type = SG_INDEXTYPE_UINT32,
      .cull_mode = SG_CULLMODE_BACK,
      .face_winding = SG_FACEWINDING_CCW,
      .depth =
          {
              .write_enabled = true,
              .compare = SG_COMPAREFUNC_LESS_EQUAL,
          },
      .layout =
          {
              .attrs =
                  {
                      [0] = {.format = SG_VERTEXFORMAT_FLOAT3},
                      [1] = {.format = SG_VERTEXFORMAT_FLOAT4},
                  },
          },
  });

  // Fill a test chunk -- fill however you like for now; simplest smoke test
  // is something with visible internal faces, e.g. floor half solid:
  test_chunk.coord = (ChunkCoord){0, 0, 0};
  for (int x = 0; x < CHUNK_SIZE; x++)
    for (int y = 0; y < CHUNK_SIZE / 2; y++)
      for (int z = 0; z < CHUNK_SIZE; z++)
        chunk_set_block(&test_chunk, x, y, z, /* some non-air block_id_t */ 2);

  ChunkMeshData mesh =
      chunk_mesh_build_naive(&test_chunk, NULL, NULL, NULL, NULL, NULL,
                             NULL); // no neighbors loaded yet

  state.chunk_bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
      .data = (sg_range){.ptr = mesh.vertices,
                         .size = mesh.vertex_count * sizeof(vertex_t)},
  });
  state.chunk_bind.index_buffer = sg_make_buffer(&(sg_buffer_desc){
      .usage = {.index_buffer = true, .immutable = true},
      .data = (sg_range){.ptr = mesh.indices,
                         .size = mesh.index_count * sizeof(uint32_t)},
  });
  state.chunk_index_count = mesh.index_count;

  chunk_mesh_free(&mesh); // CPU copy uploaded to GPU, no longer needed
}

void frame(void) {
  uint64_t frame_ticks = stm_laptime(&last_time);
  float delta_time = (float)stm_sec(frame_ticks);
  if (delta_time > 0.0f)
    fps = 1.0f / delta_time;

  if (count < 600 * 60 * delta_time) {
    count++;
  } else {
    printf("%f fps, coords: %f, %f, %f\n", fps, camera.position.x,
           camera.position.y, camera.position.z);
    count = 0;
  }

  FreeCameraInput input = {
      .move_forward = keys_down[SAPP_KEYCODE_W],
      .move_backward = keys_down[SAPP_KEYCODE_S],
      .move_up = keys_down[SAPP_KEYCODE_SPACE],
      .move_down = keys_down[SAPP_KEYCODE_LEFT_SHIFT] ||
                   keys_down[SAPP_KEYCODE_RIGHT_SHIFT],
      .strafe_left = keys_down[SAPP_KEYCODE_A],
      .strafe_right = keys_down[SAPP_KEYCODE_D],
      .look_left = keys_down[SAPP_KEYCODE_LEFT],
      .look_right = keys_down[SAPP_KEYCODE_RIGHT],
      .look_up = keys_down[SAPP_KEYCODE_UP],
      .look_down = keys_down[SAPP_KEYCODE_DOWN],
  };

  free_camera_update(&camera, input, delta_time);
  Mat4 view = free_camera_view_matrix(&camera);
  /*Mat4 model = mat4_identity();
  Mat4 mvp = mat4_multiply(projection, mat4_multiply(view, model));*/

  sg_begin_pass(
      &(sg_pass){.action = state.pass_action, .swapchain = sglue_swapchain()});
  /*sg_apply_pipeline(state.pip);
  sg_apply_bindings(&state.bind);
  for (size_t i = 0; i < CUBE_COUNT; i++) {
    Mat4 model = mat4_translate(cube_positions[i]);
    Mat4 mvp = mat4_multiply(projection, mat4_multiply(view, model));
    sg_apply_uniforms(UB_vs_params, &SG_RANGE(mvp));
    sg_draw(0, 36, 1);
  }*/

  sg_apply_pipeline(state.chunk_pip);
  sg_apply_bindings(&state.chunk_bind);
  Mat4 chunk_model = mat4_translate((Vec3){
      0.0f, 0.0f,
      0.0f}); // test_chunk.coord * CHUNK_SIZE, once you have multiple chunks
  Mat4 chunk_mvp = mat4_multiply(projection, mat4_multiply(view, chunk_model));
  sg_apply_uniforms(UB_vs_params, &SG_RANGE(chunk_mvp));
  sg_draw(0, state.chunk_index_count, 1);

  sg_end_pass();
  sg_commit();
}

void cleanup(void) { sg_shutdown(); }

sapp_desc sokol_main(int argc, char **argv) {
  return (sapp_desc){
      .init_cb = init,
      .frame_cb = frame,
      .cleanup_cb = cleanup,
      .event_cb = input_event, // missing — WASD/arrows currently do nothing
      .width = 1280,
      .height = 720,
      .window_title = "Voxel Engine",
  };
}
