#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "matrix.h"

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
  fprintf(stderr, "m = | %04d %04d %04d |\n", m->x[0], m->x[1], m->x[2]);
  fprintf(stderr, "    | %04d %04d %04d |\n", m->x[3], m->x[4], m->x[5]);
  fprintf(stderr, "    | %04d %04d %04d |\n", m->x[6], m->x[7], m->x[8]);
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
