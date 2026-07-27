#ifndef PERANTI_MAT4_H
#define PERANTI_MAT4_H

// Column-major 4x4 matrix, matching WGSL's mat4x4f memory layout.
// m[col * 4 + row] — column 0 is m[0..3], column 1 is m[4..7], etc.
typedef struct {
    float m[16];
} Mat4;

typedef struct {
    float x, y, z;
} Vec3;

Mat4 mat4_identity(void);

// Returns a * b, applied as (a * b) * v i.e. b is applied to v first.
Mat4 mat4_multiply(Mat4 a, Mat4 b);

Mat4 mat4_rotate_y(float radians);

// z_near/z_far, not near/far -- Windows SDK macro collision.
// WebGPU NDC z range is [0, 1], not OpenGL's [-1, 1] -- don't reuse an
// OpenGL-derived formula here without checking the z rows.
Mat4 mat4_perspective(float fov_y_radians, float aspect, float z_near, float z_far);

Vec3 vec3_cross(Vec3 a, Vec3 b);
Vec3 vec3_normalize(Vec3 v);
Vec3 vec3_subtract(Vec3 a, Vec3 b);
float vec3_dot(Vec3 a, Vec3 b);

Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up);

#endif // PERANTI_MAT4_H
