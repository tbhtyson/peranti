#include "camera.h"
#include <math.h>

#define MOUSE_SENSITIVITY_RADIANS_PER_PIXEL 0.0025f
#define MOVE_SPEED_UNITS_PER_SEC 3.0f
#define PERANTI_PI 3.14159265358979323846f
#define PITCH_LIMIT_RADIANS (89.0f * PERANTI_PI / 180.0f)

FreeCamera free_camera_init(Vec3 position, float yaw, float pitch) {
  return (FreeCamera){.position = position, .yaw = yaw, .pitch = pitch};
}

// Full look direction, including pitch -- used for the view matrix's target.
static Vec3 free_camera_forward(const FreeCamera *camera) {
  float cp = cosf(camera->pitch);
  return (Vec3){
      cp * sinf(camera->yaw),
      sinf(camera->pitch),
      -cp * cosf(camera->yaw),
  };
}

// Horizontal-only forward, used for WASD movement -- pitch deliberately
// excluded so looking up/down doesn't change ground speed or fly you
// into the floor/sky. See note above on this being a real design choice.
static Vec3 free_camera_flat_forward(const FreeCamera *camera) {
  return (Vec3){sinf(camera->yaw), 0.0f, -cosf(camera->yaw)};
}

void free_camera_update(FreeCamera *camera, FreeCameraInput input,
                        float delta_time) {
  camera->yaw += input.delta_x * MOUSE_SENSITIVITY_RADIANS_PER_PIXEL;
  camera->pitch -= input.delta_y * MOUSE_SENSITIVITY_RADIANS_PER_PIXEL;

  // clamp away from +/-90deg -- forward/up go parallel there, same
  // gimbal case flagged when look_at's normalize was first written
  if (camera->pitch > PITCH_LIMIT_RADIANS)
    camera->pitch = PITCH_LIMIT_RADIANS;
  if (camera->pitch < -PITCH_LIMIT_RADIANS)
    camera->pitch = -PITCH_LIMIT_RADIANS;

  Vec3 flat_forward = free_camera_flat_forward(camera);
  Vec3 up = {0.0f, 1.0f, 0.0f};
  Vec3 right = vec3_cross(
      flat_forward,
      up); // already unit length -- both inputs are orthogonal unit vectors

  float move_amount = MOVE_SPEED_UNITS_PER_SEC * delta_time;

  if (input.move_forward) {
    camera->position.x += flat_forward.x * move_amount;
    camera->position.z += flat_forward.z * move_amount;
  }
  if (input.move_backward) {
    camera->position.x -= flat_forward.x * move_amount;
    camera->position.z -= flat_forward.z * move_amount;
  }
  if (input.move_up) {
    camera->position.y += move_amount;
  }
  if (input.move_down) {
    camera->position.y -= move_amount;
  }
  if (input.strafe_right) {
    camera->position.x += right.x * move_amount;
    camera->position.z += right.z * move_amount;
  }
  if (input.strafe_left) {
    camera->position.x -= right.x * move_amount;
    camera->position.z -= right.z * move_amount;
  }
}

Mat4 free_camera_view_matrix(const FreeCamera *camera) {
  Vec3 forward = free_camera_forward(camera);
  Vec3 target = {
      camera->position.x + forward.x,
      camera->position.y + forward.y,
      camera->position.z + forward.z,
  };
  Vec3 up = {0.0f, 1.0f, 0.0f};
  return mat4_look_at(camera->position, target, up);
}
