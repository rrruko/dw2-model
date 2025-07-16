#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "matrix.h"

// Decompose M = SR into S (scale) and R (rotation)
void decompose(fmatrix_t m, fmatrix_t* s_out, fmatrix_t* r_out) {
  float scale_x = sqrt(m.x[0]*m.x[0] + m.x[3]*m.x[3] + m.x[6]*m.x[6]);
  float scale_y = sqrt(m.x[1]*m.x[1] + m.x[4]*m.x[4] + m.x[7]*m.x[7]);
  float scale_z = sqrt(m.x[2]*m.x[2] + m.x[5]*m.x[5] + m.x[8]*m.x[8]);

  memset(s_out, 0, sizeof(fmatrix_t));
  s_out->x[0] = scale_x;
  s_out->x[4] = scale_y;
  s_out->x[8] = scale_z;

  float inv_scale_x = scale_x == 0 ? 1.0 : (1.0 / scale_x);
  float inv_scale_y = scale_y == 0 ? 1.0 : (1.0 / scale_y);
  float inv_scale_z = scale_z == 0 ? 1.0 : (1.0 / scale_z);

  memcpy(r_out, &m, sizeof(fmatrix_t));
  r_out->x[0] *= inv_scale_x;
  r_out->x[1] *= inv_scale_y;
  r_out->x[2] *= inv_scale_z;
  r_out->x[3] *= inv_scale_x;
  r_out->x[4] *= inv_scale_y;
  r_out->x[5] *= inv_scale_z;
  r_out->x[6] *= inv_scale_x;
  r_out->x[7] *= inv_scale_y;
  r_out->x[8] *= inv_scale_z;
}

vertex_t rotate(matrix_t m, vertex_t v) {
  return (vertex_t) {
    .x = ((int32_t) v.x * m.x[0]) / 4096
       + ((int32_t) v.y * m.x[1]) / 4096
       + ((int32_t) v.z * m.x[2]) / 4096,
    .y = ((int32_t) v.x * m.x[3]) / 4096
       + ((int32_t) v.y * m.x[4]) / 4096
       + ((int32_t) v.z * m.x[5]) / 4096,
    .z = ((int32_t) v.x * m.x[6]) / 4096
       + ((int32_t) v.y * m.x[7]) / 4096
       + ((int32_t) v.z * m.x[8]) / 4096
  };
}

vertex_t translate(vertex_t a, vertex_t b) {
  return (vertex_t) {
    .x = a.x + b.x,
    .y = a.y + b.y,
    .z = a.z + b.z
  };
}

fmatrix_t matrix_to_fmatrix(matrix_t m) {
  fmatrix_t fm;
  fm.x[0] = m.x[0] / 4096.0;
  fm.x[1] = m.x[1] / 4096.0;
  fm.x[2] = m.x[2] / 4096.0;
  fm.x[3] = m.x[3] / 4096.0;
  fm.x[4] = m.x[4] / 4096.0;
  fm.x[5] = m.x[5] / 4096.0;
  fm.x[6] = m.x[6] / 4096.0;
  fm.x[7] = m.x[7] / 4096.0;
  fm.x[8] = m.x[8] / 4096.0;
  return fm;
}

// See https://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
quaternion_t matrix_to_quaternion(fmatrix_t m) {
  float trace = m.x[0] + m.x[4] + m.x[8];
  if (trace > 0) {
    float S = sqrt(trace + 1.0) * 2;
    quaternion_t q = {
      .w = 0.25 * S,
      .x = (m.x[5] - m.x[7]) / S,
      .y = (m.x[6] - m.x[2]) / S,
      .z = (m.x[1] - m.x[3]) / S
    };
    return q;
  } else if ((m.x[0] > m.x[4]) && (m.x[0] > m.x[8])) {
    float S = sqrt(1.0 + m.x[0] - m.x[4] - m.x[8]) * 2;
    quaternion_t q = {
      .w = (m.x[5] - m.x[7]) / S,
      .x = 0.25 * S,
      .y = (m.x[3] + m.x[1]) / S,
      .z = (m.x[6] + m.x[2]) / S
    };
    return q;
  } else if (m.x[4] > m.x[8]) {
    float S = sqrt(1.0 + m.x[4] - m.x[0] - m.x[8]) * 2;
    quaternion_t q = {
      .w = (m.x[6] - m.x[2]) / S,
      .x = (m.x[3] + m.x[1]) / S,
      .y = 0.25 * S,
      .z = (m.x[7] + m.x[5]) / S
    };
    return q;
  } else {
    float S = sqrt(1.0 + m.x[8] - m.x[0] - m.x[4]) * 2;
    quaternion_t q = {
      .w = (m.x[1] - m.x[3]) / S,
      .x = (m.x[6] + m.x[2]) / S,
      .y = (m.x[7] + m.x[5]) / S,
      .z = 0.25 * S
    };
    return q;
  }
}

void display_matrix_debug(matrix_t* m) {
  fprintf(stderr, "m = | %05d %05d %05d |\n", m->x[0], m->x[1], m->x[2]);
  fprintf(stderr, "    | %05d %05d %05d |\n", m->x[3], m->x[4], m->x[5]);
  fprintf(stderr, "    | %05d %05d %05d |\n", m->x[6], m->x[7], m->x[8]);
}

void display_quaternion_debug(quaternion_t* q) {
  fprintf(stderr, "q = %f + %fi + %fj + %fk (length=%f)\n",
    q->w, q->x, q->y, q->z, sqrt(q->w*q->w + q->x*q->x + q->y*q->y + q->z*q->z));
}

void normalize_quaternion_inplace(quaternion_t* q) {
  float r = 1.0 / sqrt(q->w*q->w + q->x*q->x + q->y*q->y + q->z*q->z);
  q->w *= r;
  q->x *= r;
  q->y *= r;
  q->z *= r;
}
