#include "mat4.h"
#include <math.h>

/*
 * Mat4 is COLUMN-MAJOR!!!
 * remember that!
 *
 *
 * */

Mat4 mat4_identity(void) {
  // all zero except m[0], m[5], m[10], m[15] = 1.0f
  Mat4 mat = {0};
  mat = (Mat4){{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  return mat;
}

Mat4 mat4_multiply(Mat4 a, Mat4 b) {
  Mat4 result;
  // 16 explicit element writes, each a dot product of a row of `a`
  // (careful: rows in a column-major layout are strided by 4)
  // with a column of `b`. Write all 16 by loop.
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      result.m[col * 4 + row] = a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                                a.m[3 * 4 + row] * b.m[col * 4 + 3];
    }
  }
  return result;
}

Mat4 mat4_rotate_y(float radians) {
  Mat4 mat = mat4_identity();
  float c = cosf(radians);
  float s = sinf(radians);
  mat.m[0] = c;
  mat.m[2] = -s;
  mat.m[8] = s;
  mat.m[10] = c;
  return mat;
}

Mat4 mat4_perspective(float fov_y_radians, float aspect, float z_near,
                      float z_far) {
  Mat4 mat = {0}; // every unlisted entry must genuinely be 0 here
  float f = 1.0f / tanf(fov_y_radians / 2.0f);

  mat.m[0] = f / aspect;
  mat.m[5] = f;
  mat.m[10] = z_far / (z_near - z_far);
  mat.m[11] = -1.0f;
  mat.m[14] = (z_near * z_far) / (z_near - z_far);
  // m[15] stays 0 -- this row produces w, not a second z term

  return mat;
}

// helpers for the next thing

float vec3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 vec3_cross(Vec3 a, Vec3 b) {
  return (Vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

Vec3 vec3_normalize(Vec3 v) {
  float len = sqrtf(vec3_dot(v, v));
  return (Vec3){v.x / len, v.y / len, v.z / len};
}

Vec3 vec3_subtract(Vec3 a, Vec3 b) {
  return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

// next thing

Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up) {
  Vec3 forward = vec3_normalize(vec3_subtract(target, eye));
  Vec3 right = vec3_normalize(vec3_cross(forward, up));
  Vec3 true_up = vec3_cross(right, forward);

  Mat4 mat = {0};

  // column 0
  mat.m[0] = right.x;
  mat.m[1] = true_up.x;
  mat.m[2] = -forward.x;
  mat.m[3] = 0.0f;

  // column 1
  mat.m[4] = right.y;
  mat.m[5] = true_up.y;
  mat.m[6] = -forward.y;
  mat.m[7] = 0.0f;

  // column 2
  mat.m[8] = right.z;
  mat.m[9] = true_up.z;
  mat.m[10] = -forward.z;
  mat.m[11] = 0.0f;

  // column 3 (translation)
  mat.m[12] = -vec3_dot(right, eye);
  mat.m[13] = -vec3_dot(true_up, eye);
  mat.m[14] = vec3_dot(forward, eye);
  mat.m[15] = 1.0f;

  return mat;
}
Mat4 mat4_translate(Vec3 t) {
  Mat4 result = mat4_identity();
  result.m[12] = t.x;
  result.m[13] = t.y;
  result.m[14] = t.z;
  return result;
}
