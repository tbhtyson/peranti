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
#include "world.h"
#include <stdio.h>

#define PERANTI_PI 3.14159265358979323846f
#define PERANTI_FOV_Y (60.0f * PERANTI_PI / 180.0f)
#define PERANTI_Z_NEAR 0.1f
#define PERANTI_Z_FAR 1000.0f
bool NO_COMPUTE_SHADERS;

static float mouse_delta_x = 0.0f;
static float mouse_delta_y = 0.0f;

// 1. Define global state structure
static struct {
  sg_pass_action pass_action;

  sg_pipeline chunk_pip; // new — chunks use UINT32 indices
  sg_bindings chunk_bind;
  uint32_t chunk_index_count;
} state;

// in main.c's state, alongside chunk_pip:
typedef struct {
  sg_bindings bind;
  uint32_t index_count;
  Vec3 world_offset; // chunk coord * CHUNK_SIZE, precomputed once at build time
} ChunkRenderData;

static ChunkRenderData chunk_renders[WORLD_CHUNKS_X][WORLD_CHUNKS_Y]
                                    [WORLD_CHUNKS_Z];

static uint64_t last_time = 0;
static float fps = 0.0f;
static float count = 0.0f;
static World world;

static Mat4 projection;
static FreeCamera camera;

static bool keys_down[SAPP_MAX_KEYCODES];

void input_event(const sapp_event *ev) {
  if (ev->type == SAPP_EVENTTYPE_KEY_DOWN)
    keys_down[ev->key_code] = true;
  if (ev->type == SAPP_EVENTTYPE_KEY_UP)
    keys_down[ev->key_code] = false;
  if (ev->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
    mouse_delta_x += ev->mouse_dx;
    mouse_delta_y += ev->mouse_dy;
  }
}

void init(void) {
  sg_setup(&(sg_desc){
      .environment = sglue_environment(),
      .logger.func = slog_func,
      .buffer_pool_size = 4096,
  });

  projection = mat4_perspective(PERANTI_FOV_Y,
                                (float)sapp_width() / (float)sapp_height(),
                                PERANTI_Z_NEAR, PERANTI_Z_FAR);

  camera = (FreeCamera){
      .position = {8.0f, 20.0f, 40.0f},
      .yaw = 0.0f,
      .pitch =
          -0.5f, // look down/toward origin — adjust to your pitch convention
  };

  stm_setup(); // Initialize sokol_time
  last_time = stm_now();

  sapp_lock_mouse(true);

  sg_features features = sg_query_features();
  if (!features.compute) {
    printf("Compute shader support not found.\n");
    NO_COMPUTE_SHADERS = true;
  }

  sg_shader shader = sg_make_shader(triangle_shader_desc(sg_query_backend()));

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

  world_init_flat_test(&world);

  for (int cx = 0; cx < WORLD_CHUNKS_X; cx++) {
    for (int cy = 0; cy < WORLD_CHUNKS_Y; cy++) {
      for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++) {
        const Chunk *chunk = world_chunk_at(&world, cx, cy, cz);
        ChunkMeshData mesh = chunk_mesh_build_naive(
            chunk, world_chunk_at(&world, cx + 1, cy, cz),
            world_chunk_at(&world, cx - 1, cy, cz),
            world_chunk_at(&world, cx, cy + 1, cz),
            world_chunk_at(&world, cx, cy - 1, cz),
            world_chunk_at(&world, cx, cy, cz + 1),
            world_chunk_at(&world, cx, cy, cz - 1));

        ChunkRenderData *render = &chunk_renders[cx][cy][cz];
        if (mesh.vertex_count == 0) {
          render->index_count = 0;
        } else {
          render->bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
              .data = (sg_range){.ptr = mesh.vertices,
                                 .size = mesh.vertex_count * sizeof(vertex_t)},
          });
          render->bind.index_buffer = sg_make_buffer(&(sg_buffer_desc){
              .usage = {.index_buffer = true, .immutable = true},
              .data = (sg_range){.ptr = mesh.indices,
                                 .size = mesh.index_count * sizeof(uint32_t)},
          });
          render->index_count = mesh.index_count;
          render->world_offset =
              (Vec3){(float)(cx * CHUNK_SIZE), (float)(cy * CHUNK_SIZE),
                     (float)(cz * CHUNK_SIZE)};

          chunk_mesh_free(&mesh);
        }
      }
    }
  }
}

void frame(void) {
  uint64_t frame_ticks = stm_laptime(&last_time);
  float delta_time = (float)stm_sec(frame_ticks);
  if (delta_time > 0.0f)
    fps = 1.0f / delta_time;

  count += delta_time;
  if (count > 1.0f) {
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
      .delta_x = mouse_delta_x,
      .delta_y = mouse_delta_y,
  };
  mouse_delta_x = 0.0f;
  mouse_delta_y = 0.0f;
  if(keys_down[SAPP_KEYCODE_ESCAPE]) {
    sapp_lock_mouse(false);
  }

  free_camera_update(&camera, input, delta_time);
  Mat4 view = free_camera_view_matrix(&camera);

  sg_begin_pass(
      &(sg_pass){.action = state.pass_action, .swapchain = sglue_swapchain()});

  sg_apply_pipeline(state.chunk_pip);
  for (int cx = 0; cx < WORLD_CHUNKS_X; cx++) {
    for (int cy = 0; cy < WORLD_CHUNKS_Y; cy++) {
      for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++) {
        ChunkRenderData *render = &chunk_renders[cx][cy][cz];
        if (render->index_count == 0)
          continue; // fully interior, nothing to render
        sg_apply_bindings(&render->bind);
        Mat4 model = mat4_translate(render->world_offset);
        Mat4 mvp = mat4_multiply(projection, mat4_multiply(view, model));
        sg_apply_uniforms(UB_vs_params, &SG_RANGE(mvp));
        sg_draw(0, (int)render->index_count, 1);
      }
    }
  }

  sg_end_pass();
  sg_commit();
}

void cleanup(void) { sg_shutdown(); }

sapp_desc sokol_main(int argc, char **argv) {
  return (sapp_desc){
      .init_cb = init,
      .frame_cb = frame,
      .cleanup_cb = cleanup,
      .event_cb = input_event,
      .width = 1280,
      .height = 720,
      .window_title = "Peranti 1",
  };
}
