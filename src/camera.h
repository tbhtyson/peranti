#ifndef PERANTI_CAMERA_H
#define PERANTI_CAMERA_H

#include "mat4.h"
#include <stdbool.h>

typedef struct {
    Vec3 position;
    float yaw;   // radians; 0 = facing -Z
    float pitch; // radians; clamped away from +/-90deg
} FreeCamera;

typedef struct {
    bool move_forward;
    bool move_backward;
    bool strafe_left;
    bool strafe_right;
    bool move_up;
    bool move_down;
    float delta_x;
    float delta_y;
} FreeCameraInput;

FreeCamera free_camera_init(Vec3 position, float yaw, float pitch);
void free_camera_update(FreeCamera *camera, FreeCameraInput input, float delta_time);
Mat4 free_camera_view_matrix(const FreeCamera *camera);

#endif // PERANTI_CAMERA_H
