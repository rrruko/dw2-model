#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <math.h>

#include "iso_reader.h"
#define CGLTF_WRITE_IMPLEMENTATION
#include "cgltf_write.h"

typedef struct model_s {
  iso_t* iso;
  uint32_t file_sector;
  uint32_t texture_sheet_offset;
  uint32_t object_count;
  uint32_t* skeleton;
  int32_t* node_tree;
  uint32_t* vertex_offsets;
  uint32_t* normal_offsets;
  uint32_t* face_offsets;
} model_t;

typedef struct vertex_s {
  int16_t x;
  int16_t y;
  int16_t z;
} vertex_t;

typedef struct face_quad_s {
  uint8_t vertex_a;
  uint8_t vertex_b;
  uint8_t vertex_c;
  uint8_t vertex_d;
  uint8_t normal_a;
  uint8_t normal_b;
  uint8_t normal_c;
  uint8_t normal_d;
  uint8_t tex_a_x;
  uint8_t tex_a_y;
  uint8_t tex_b_x;
  uint8_t tex_b_y;
  uint8_t tex_c_x;
  uint8_t tex_c_y;
  uint8_t tex_d_x;
  uint8_t tex_d_y;
  uint8_t palette;
  uint8_t clut;
  uint8_t pad_1;
  uint8_t pad_2;
} face_quad_t;

typedef struct face_tri_s {
  uint8_t vertex_a;
  uint8_t vertex_b;
  uint8_t vertex_c;
  uint8_t normal_a;
  uint8_t normal_b;
  uint8_t normal_c;
  uint8_t tex_a_x;
  uint8_t tex_a_y;
  uint8_t tex_b_x;
  uint8_t tex_b_y;
  uint8_t tex_c_x;
  uint8_t tex_c_y;
  uint8_t palette;
  uint8_t clut;
  uint8_t pad_1;
  uint8_t pad_2;
} face_tri_t;

typedef struct paletted_texture_s {
  uint16_t* palette; // 16 entries of 16 bits each
  uint8_t* texture; // Size is 128 x 256 x 4 bpp = 16384 bytes
} paletted_texture_t;

void die(char* message) {
  fprintf(stderr, "Fatal: %s\n", message);
  exit(1);
}

uint8_t base64_table[64] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
  'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
  'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
  'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
  'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
  'w', 'x', 'y', 'z', '0', '1', '2', '3',
  '4', '5', '6', '7', '8', '9', '+', '/'
};

char* base64_encode(unsigned char* bytes, size_t size) {
  char* buf = malloc(4 * (size / 3) + 5);
  buf[4 * (size / 3)] = '\0';
  for (int i = 0; i < size / 3; i++) {
    buf[4 * i + 0] = base64_table[
      bytes[3 * i + 0] >> 2
    ];
    buf[4 * i + 1] = base64_table[
      ((bytes[3 * i + 0] & 0x03) << 4) |
      (bytes[3 * i + 1] >> 4)
    ];
    buf[4 * i + 2] = base64_table[
      ((bytes[3 * i + 1] & 0x0f) << 2) |
      (bytes[3 * i + 2] >> 6)
    ];
    buf[4 * i + 3] = base64_table[
      bytes[3 * i + 2] & 0x3f
    ];
  }
  if (size % 3 == 1) {
    buf[4 * (size / 3)] = base64_table[
      bytes[3 * (size / 3)] >> 2
    ];
    buf[4 * (size / 3) + 1] = base64_table[
      (bytes[3 * (size / 3)] & 0x03) << 4
    ];
    buf[4 * (size / 3) + 2] = '=';
    buf[4 * (size / 3) + 3] = '=';
    buf[4 * (size / 3) + 4] = '\0';
  }
  if (size % 3 == 2) {
    buf[4 * (size / 3)] = base64_table[
      bytes[3 * (size / 3)] >> 2
    ];
    buf[4 * (size / 3) + 1] = base64_table[
      ((bytes[3 * (size / 3)] & 0x03) << 4) |
      (bytes[3 * (size / 3) + 1] >> 4)
    ];
    buf[4 * (size / 3) + 2] = base64_table[
      (bytes[3 * (size / 3) + 1] & 0x0f) << 2
    ];
    buf[4 * (size / 3) + 3] = '=';
    buf[4 * (size / 3) + 4] = '\0';
  }
  return buf;
}

char* octet_stream_encode(void* bytes, size_t size) {
  static char* header = "data:application/octet-stream;base64,";
  char* buf = malloc(4 * (size / 3) + strlen(header) + 5);
  strcpy(buf, header);
  char* encoded = base64_encode(bytes, size);
  strcat(buf, encoded);
  free(encoded);
  return buf;
}

uint16_t fake_palette[16] = {
  0x0001, 0x0421, 0x0842, 0x0c63,
  0x1084, 0x14a5, 0x18c6, 0x1ce7,
  0x2108, 0x2529, 0x294a, 0x2d6b,
  0x318c, 0x35ad, 0x39ce, 0x3def
};

paletted_texture_t load_texture(model_t* model) {
  paletted_texture_t new_texture;
  memset(&new_texture, 0, sizeof(paletted_texture_t));
  iso_seek_to_sector(model->iso, model->file_sector);
  iso_seek_forward(model->iso, model->texture_sheet_offset);
  iso_seek_forward(model->iso, 64);
  new_texture.texture = malloc(0x4000);
  iso_fread(model->iso, new_texture.texture, sizeof(uint8_t), 0x4000);
  new_texture.palette = fake_palette;
  return new_texture;
}

// This is 5 * 1024 * 1024, should be plenty big enough to fit a
// 1024x1024 rgb png
#define PNG_BUFFER_SIZE 5242880
unsigned char png_buffer[PNG_BUFFER_SIZE];

uint8_t* expand_texture_paletted(paletted_texture_t* tex, uint8_t column, uint8_t row) {
  uint32_t stride = 64;
  uint8_t* expanded = malloc(4 * 128 * 256);
  for (int i = 0; i < 16384; i++) {
    uint8_t lower = tex->texture[i] & 0x0f;
    uint8_t upper = (tex->texture[i] & 0xf0) >> 4;
    uint8_t* palette = &tex->texture[row * stride + column * 32];
    uint16_t color_lower;
    uint16_t color_upper;
    memcpy(&color_lower, &palette[2 * lower], sizeof(uint16_t));
    memcpy(&color_upper, &palette[2 * upper], sizeof(uint16_t));

    expanded[8 * i + 0] = (color_lower & 0x001f) << 3;
    expanded[8 * i + 1] = (color_lower & 0x03e0) >> 2;
    expanded[8 * i + 2] = (color_lower & 0x7c00) >> 7;
    expanded[8 * i + 3] = (color_lower == 0x0000) ? 0 : 255;
    expanded[8 * i + 4] = (color_upper & 0x001f) << 3;
    expanded[8 * i + 5] = (color_upper & 0x03e0) >> 2;
    expanded[8 * i + 6] = (color_upper & 0x7c00) >> 7;
    expanded[8 * i + 7] = (color_upper == 0x0000) ? 0 : 255;
  }
  return expanded;
}

char* googa = "googa.png";

// This is the buffer where raw pixels will be blitted to, used to generate the
// png. 1024x1024 pixels RGBA
uint8_t png_write_buffer[4*1024*1024];

void blit_to_png_write_buffer(paletted_texture_t* tex, uint8_t column, uint8_t row, size_t offset_x, size_t offset_y) {
  uint8_t* texture_expanded = expand_texture_paletted(tex, column, row);
  for (int j = 0; j < 256; j++) {
    for (int i = 0; i < 128; i++) {
      size_t to_x = 4 * (offset_x + i);
      size_t to_y = offset_y + j;
      png_write_buffer[4 * 1024 * to_y + to_x + 0] = texture_expanded[4 * (128 *
      j + i) + 0];
      png_write_buffer[4 * 1024 * to_y + to_x + 1] = texture_expanded[4 * (128 *
      j + i) + 1];
      png_write_buffer[4 * 1024 * to_y + to_x + 2] = texture_expanded[4 * (128 *
      j + i) + 2];
      png_write_buffer[4 * 1024 * to_y + to_x + 3] = texture_expanded[4 * (128 *
      j + i) + 3];
    }
  }
}

png_alloc_size_t save_png_write_buffer() {
  png_image png;
  memset(&png, 0, sizeof(png_image));
  png.version = PNG_IMAGE_VERSION;
  png.width = 1024;
  png.height = 1024;
  png.colormap_entries = 0;
  png.format = PNG_FORMAT_RGBA;
  png.flags = 0;
  char* filename = "mega-texture.png";
  png_image_write_to_file(
    &png,
    filename,
    0,
    png_write_buffer,
    0,
    NULL);
  png_alloc_size_t memory_bytes = PNG_BUFFER_SIZE;
  png_image_write_to_memory(
    &png,
    png_buffer,
    &memory_bytes,
    0,
    png_write_buffer,
    0,
    NULL);
  return memory_bytes;

}

vertex_t* load_vertices(model_t* model, uint32_t object, uint32_t* num_read) {
  uint32_t vertex_offset = model->vertex_offsets[object];
  iso_seek_to_sector(model->iso, model->file_sector);
  iso_seek_forward(model->iso, vertex_offset);
  uint32_t count;
  iso_fread(model->iso, &count, sizeof(uint32_t), 1);
  vertex_t* verts = malloc(sizeof(vertex_t) * count);
  iso_seek_forward(model->iso, sizeof(uint16_t));
  iso_fread(model->iso, verts, sizeof(vertex_t), count);
  *num_read = count;
  return verts;
}

typedef struct polys_s {
  face_quad_t* quads;
  face_tri_t* tris;
} polys_t;

polys_t load_faces(model_t* model, uint32_t object, uint32_t* num_quads_read, uint32_t* num_tris_read) {
  uint32_t face_offset = model->face_offsets[object];
  iso_seek_to_sector(model->iso, model->file_sector);
  iso_seek_forward(model->iso, face_offset);
  iso_seek_forward(model->iso, 4);
  uint32_t count;
  iso_fread(model->iso, &count, sizeof(uint32_t), 1);
  face_quad_t* quads = malloc(sizeof(face_quad_t) * count);
  iso_fread(model->iso, quads, sizeof(face_quad_t), count);
  *num_quads_read = count;
  iso_seek_forward(model->iso, 4);
  iso_fread(model->iso, &count, sizeof(uint32_t), 1);
  face_tri_t* tris = malloc(sizeof(face_tri_t) * count);
  iso_fread(model->iso, tris, sizeof(face_tri_t), count);
  *num_tris_read = count;
  return (polys_t) {
    .quads = quads,
    .tris = tris
  };
}

typedef struct animation_s {
  iso_t* iso;
  uint32_t file_sector;
  uint32_t* offsets;
  uint8_t* frame_table;
  uint32_t frame_count;
} animation_t;

animation_t load_animation(iso_t* iso, uint32_t sector, uint32_t object_count) {
  animation_t animation;
  animation.iso = iso;
  animation.file_sector = sector;
  iso_seek_to_sector(iso, sector);
  iso_seek_forward(iso, sizeof(uint32_t)); // Unused (always zero)?
  size_t items_read;
  animation.offsets = malloc(object_count * sizeof(uint32_t));
  items_read = iso_fread(
    iso,
    animation.offsets,
    sizeof(uint32_t),
    object_count);
  if (items_read != object_count) {
    die("fread failure, an error occured or EOF (animation offsets)");
  }
  iso_seek_forward(iso, sizeof(uint32_t) * 2); // Don't know what these bytes mean

  // Make space for 256 frames, should be enough, error if we run out
  animation.frame_table = malloc(object_count * 256);
  int frames_left = 1;
  uint32_t frame_count = 0;
  while (frames_left) {
    items_read = iso_fread(
      iso,
      &animation.frame_table[object_count * frame_count],
      sizeof(uint8_t),
      object_count);
    if (items_read != object_count) {
      die("fread failure, an error occured or EOF (frame table)");
    }
    if (animation.frame_table[object_count * frame_count] == 0xfe) {
      frames_left = 0;
    } else {
      frame_count++;
    }
    if (frame_count >= 256) {
      die("Too many frames. Could this be a bug?");
    }
  }
  fprintf(stderr, "Frame count: %d\n", frame_count);
  animation.frame_count = frame_count;
  return animation;
}

typedef struct matrix_s {
  int16_t x[9];
} matrix_t;

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

typedef struct quaternion_s {
  float w;
  float x;
  float y;
  float z;
} quaternion_t;

typedef struct fmatrix_s {
  float x[9];
} fmatrix_t;

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

void serialize_animation(animation_t* animation, uint32_t object_count, float** rotation_out, float** translation_out) {
  float* rotation = malloc(animation->frame_count * 16 * object_count); // N quaternions, each quaternion is 4 floats, times object_count
  float* translation = malloc(animation->frame_count * 12 * object_count); // N translation vectors of 3 floats each, times object_count
  uint32_t object_start_rot = 0;
  uint32_t object_start_trans = 0;
  for (int object = 0; object < object_count; object++) {
    uint32_t offset = animation->offsets[object];
    iso_seek_to_sector(animation->iso, animation->file_sector);
    iso_seek_forward(animation->iso, offset);
    matrix_t m;
    for (int frame = 0; frame < animation->frame_count; frame++) {
      size_t items_read = iso_fread(
        animation->iso,
        &m,
        sizeof(matrix_t),
        1);
      if (items_read != 1) {
        fprintf(stderr, "items read: %lu\n", items_read);
        die("fread failure, an error occured or EOF (rotation matrix)");
      }
      fmatrix_t fm = matrix_to_fmatrix(m);
      quaternion_t q = matrix_to_quaternion(fm);
      normalize_quaternion_inplace(&q);
      fprintf(stderr, "object %d/%d, keyframe %d/%d\n",
        object, object_count,
        frame, animation->frame_count);
      display_matrix_debug(&m);
      display_quaternion_debug(&q);
      vertex_t t = {0};
      items_read = iso_fread(
        animation->iso,
        &t,
        sizeof(vertex_t),
        1);
      if (items_read != 1) {
        die("fread failure, an error occured or EOF (translation)");
      }
      // Spec says component order is XYZW
      rotation[object_start_rot + frame * 4 + 0] = q.x;
      rotation[object_start_rot + frame * 4 + 1] = q.y;
      rotation[object_start_rot + frame * 4 + 2] = -q.z;
      rotation[object_start_rot + frame * 4 + 3] = q.w;
      translation[object_start_trans + frame * 3 + 0] = -t.x / 4096.0;
      translation[object_start_trans + frame * 3 + 1] = -t.y / 4096.0;
      translation[object_start_trans + frame * 3 + 2] = t.z / 4096.0;
    }
    object_start_rot += animation->frame_count * 4;
    object_start_trans += animation->frame_count * 3;
  }
  float* rotation_final = malloc(animation->frame_count * 16 * object_count);
  float* translation_final = malloc(animation->frame_count * 16 * object_count);
  for (int object = 0; object < object_count; object++) {
    object_start_rot = animation->frame_count * 4 * object;
    object_start_trans = animation->frame_count * 3 * object;
    for (int frame = 0; frame < animation->frame_count; frame++) {
      uint8_t from = animation->frame_table[frame * object_count + object];
      memcpy(
        &rotation_final[object_start_rot + frame * 4],
        &rotation[object_start_rot + from * 4],
        4 * sizeof(float));
      memcpy(
        &translation_final[object_start_trans + frame * 3],
        &translation[object_start_trans + from * 3],
        3 * sizeof(float));
    }
  }
  *rotation_out = rotation_final;
  *translation_out = translation_final;
}

// We transform a vertex by looking up its object's transform matrix and
// translation on the given frame number, and then potentially recursing with the parent object
vertex_t transform_vertex(vertex_t v, model_t* model, animation_t* animation, uint32_t object, uint32_t frame) {
  uint32_t offset = animation->offsets[object];
  iso_seek_to_sector(animation->iso, animation->file_sector);
  // We should actually be looking up the correct frame in the frame table but
  // it doesn't make much difference for our test model
  iso_seek_forward(animation->iso, offset + frame * 24);
  matrix_t m;
  size_t items_read = iso_fread(
    animation->iso,
    &m,
    sizeof(matrix_t),
    1);
  if (items_read != 1) {
    die("fread failure, an error occured or EOF (matrix)");
  }
  vertex_t translation = {0};
  items_read = iso_fread(
    animation->iso,
    &translation,
    sizeof(vertex_t),
    1);
  if (items_read != 1) {
    die("fread failure, an error occured or EOF (translation)");
  }
  if (sizeof(matrix_t) + sizeof(vertex_t) != 24) {
    die("alignment issue");
  }
  vertex_t v_rotated = rotate(m, v);
  vertex_t v_translated = translate(translation, v_rotated);
  if (model->skeleton[object] > 0) {
    uint32_t parent = -1;
    for (int i = object - 1; i >= 0; i--) {
      if (model->skeleton[i] + 1 == model->skeleton[object]) {
        parent = i;
        break;
      }
    }
    if (parent == -1) {
      die("Couldn't find the parent object");
    }
    return transform_vertex(v_translated, model, animation, parent, frame);
  } else {
    return v_translated;
  }
}

model_t load_model(iso_t* iso, uint32_t sector) {
  model_t new_model;
  new_model.iso = iso;
  new_model.file_sector = sector;
  iso_seek_to_sector(iso, sector);
  size_t items_read;
  items_read = iso_fread(iso, &new_model.texture_sheet_offset, sizeof(uint32_t), 1);
  if (items_read != 1) {
    die("fread failure, an error occured or EOF (texture_sheet_offset)");
  }
  iso_seek_forward(iso, sizeof(uint32_t)); // Skip ahead to object count
  items_read = iso_fread(iso, &new_model.object_count, sizeof(uint32_t), 1);
  if (items_read != 1) {
    die("fread failure, an error occured or EOF (object_count)");
  }
  new_model.vertex_offsets = malloc(new_model.object_count * sizeof(uint32_t));
  items_read = iso_fread(
    iso,
    new_model.vertex_offsets,
    sizeof(uint32_t),
    new_model.object_count);
  if (items_read != new_model.object_count) {
    die("fread failure, an error occured or EOF (vertex_offsets)");
  }
  new_model.normal_offsets = malloc(new_model.object_count * sizeof(uint32_t));
  items_read = iso_fread(
    iso,
    new_model.normal_offsets,
    sizeof(uint32_t),
    new_model.object_count);
  if (items_read != new_model.object_count) {
    die("fread failure, an error occured or EOF (normal_offsets)");
  }
  new_model.face_offsets = malloc(new_model.object_count * sizeof(uint32_t));
  items_read = iso_fread(
    iso,
    new_model.face_offsets,
    sizeof(uint32_t),
    new_model.object_count);
  if (items_read != new_model.object_count) {
    die("fread failure, an error occured or EOF (face_offsets)");
  }
  new_model.skeleton = malloc(new_model.object_count * sizeof(uint32_t));
  items_read = iso_fread(
    iso,
    new_model.skeleton,
    sizeof(uint32_t),
    new_model.object_count);
  if (items_read != new_model.object_count) {
    die("fread failure, an error occured or EOF (skeleton)");
  }

  // Construct the node tree used for animation. It's just a different
  // view of the skeleton data, where node_tree[i] is the index of object i's
  // parent node, or -1 if object i is the root:
  // Skeleton:   0  1  2  3  1  2  3
  // Node tree: -1  0  1  2  0  4  5
  new_model.node_tree = malloc(new_model.object_count * sizeof(uint32_t));
  for (int i = 0; i < new_model.object_count; i++) {
    new_model.node_tree[i] = -1;
    // Seek backwards to find this object's parent, i.e. the latest node with a
    // level that is smaller by 1
    for (int j = i - 1; j >= 0; j--) {
      if (new_model.skeleton[j] == new_model.skeleton[i] - 1) {
        new_model.node_tree[i] = j;
        break;
      }
    }
  }
  return new_model;
}

void make_epic_gltf_file(float** vertices, size_t* vertex_count, uint32_t**
tri_indices, size_t* triangle_count, float** texcoords, size_t* texcoord_count,
animation_t* animation, int32_t* node_tree, size_t object_count, size_t
png_alloc) {
  size_t total_vertices = 0;
  for (int i = 0; i < object_count; i++) {
    total_vertices += vertex_count[i];
  }
  float* all_vertices = malloc(sizeof(float) * 3 * total_vertices);
  size_t vertex_array_offset = 0;
  for (int i = 0; i < object_count; i++) {
    memcpy(&all_vertices[vertex_array_offset], vertices[i], 4 * 3 * vertex_count[i]);
    vertex_array_offset += 3 * vertex_count[i];
  }

  size_t total_triangles = 0;
  for (int i = 0; i < object_count; i++) {
    total_triangles += triangle_count[i];
  }
  uint32_t* all_triangles = malloc(sizeof(uint32_t) * 3 * total_triangles);
  size_t index_array_offset = 0;
  for (int i = 0; i < object_count; i++) {
    memcpy(&all_triangles[index_array_offset], tri_indices[i], 4 * 3 * triangle_count[i]);
    index_array_offset += 3 * triangle_count[i];
  }

  size_t total_texcoords = 0;
  for (int i = 0; i < object_count; i++) {
    total_texcoords += texcoord_count[i];
  }
  float* all_texcoords = malloc(sizeof(float) * 2 * total_texcoords);
  size_t texcoord_array_offset = 0;
  for (int i = 0; i < object_count; i++) {
    memcpy(&all_texcoords[texcoord_array_offset], texcoords[i], 4 * 2 * texcoord_count[i]);
    texcoord_array_offset += 2 * texcoord_count[i];
  }

  // Get all the animations
  float* rotation_anim;
  float* translation_anim;
  serialize_animation(animation, object_count, &rotation_anim, &translation_anim);

  char* vertex_encoded = octet_stream_encode(all_vertices, 4 * 3 * total_vertices);
  char* index_encoded = octet_stream_encode(all_triangles, 4 * 3 * total_triangles);
  char* rotation_encoded = octet_stream_encode(rotation_anim,
    object_count * animation->frame_count * 4 * sizeof(float));
  char* translation_encoded = octet_stream_encode(translation_anim,
    object_count * animation->frame_count * 3 * sizeof(float));

  char* texcoord_encoded = octet_stream_encode(all_texcoords, 4 * 2 * total_texcoords);

  float animation_input[animation->frame_count];
  for (int i = 0; i < animation->frame_count; i++) {
    animation_input[i] = (float) (i * 0.0333333); // 30 FPS
  }
  char* animation_input_encoded = octet_stream_encode(animation_input, animation->frame_count * sizeof(float));

  char* png_encoded = octet_stream_encode(png_buffer, png_alloc);

  cgltf_buffer buffers[7] = {
    {
      .name = "vertex_buffer",
      .size = 4 * 3 * total_vertices,
      .uri = vertex_encoded
    },
    {
      .name = "vertex_index_buffer",
      .size = 4 * 3 * total_triangles,
      // 3 indices of 32-bit size, little-endian, "0 1 2"
      .uri = index_encoded
    },
    {
      .name = "animation_rotation_input",
      .size = animation->frame_count * sizeof(float),
      .uri = animation_input_encoded
    },
    {
      .name = "animation_rotation_output",
      .size = object_count * animation->frame_count * 4 * sizeof(float),
      .uri = rotation_encoded
    },
    {
      .name = "animation_translation_output",
      .size = object_count * animation->frame_count * 3 * sizeof(float),
      .uri = translation_encoded
    },
    {
      .name = "texcoord",
      .size = 4 * 2 * total_texcoords,
      .uri = texcoord_encoded
    },
    {
      .name = "texture_buffer",
      .size = png_alloc,
      .uri = png_encoded
    }
  };
  cgltf_buffer_view buffer_views[7] = {
    {
      .name = "vertex_buffer_view",
      .buffer = &buffers[0],
      .offset = 0,
      .size = 4 * 3 * total_vertices,
      .stride = 12,
      .type = cgltf_buffer_view_type_vertices
    },
    {
      .name = "vertex_index_buffer_view",
      .buffer = &buffers[1],
      .offset = 0,
      .size = 4 * 3 * total_triangles,
      //.stride = 0,
      .type = cgltf_buffer_view_type_indices
    },
    {
      .name = "animation_rotation_input",
      .buffer = &buffers[2],
      .offset = 0,
      .size = animation->frame_count * sizeof(float)
    },
    {
      .name = "animation_rotation_output_view",
      .buffer = &buffers[3],
      .offset = 0,
      .size = object_count * animation->frame_count * 4 * sizeof(float),
      .stride = 16
    },
    {
      .name = "animation_translation_output_view",
      .buffer = &buffers[4],
      .offset = 0,
      .size = object_count * animation->frame_count * 3 * sizeof(float),
      .stride = 12
    },
    {
      .name = "texcoord_view",
      .buffer = &buffers[5],
      .offset = 0,
      .size = 4 * 2 * total_texcoords
    },
    {
      .name = "texture_view",
      .buffer = &buffers[6],
      .offset = 0,
      .size = png_alloc
    }
  };
  cgltf_accessor accessors[5 * object_count + 1];
  size_t object_vertex_offset = 0;
  for (int i = 0; i < object_count; i++) {
    accessors[i] = (cgltf_accessor) {
      .name = "vertex",
      .component_type = cgltf_component_type_r_32f,
      .type = cgltf_type_vec3,
      .offset = object_vertex_offset,
      .count = vertex_count[i],
      .stride = 12, // 3 * 32 bit
      .buffer_view = &buffer_views[0],
      .has_min = 1,
      .has_max = 1,
    };
    object_vertex_offset += 12 * vertex_count[i];

    float min_x = +999999;
    float min_y = +999999;
    float min_z = +999999;
    float max_x = -999999;
    float max_y = -999999;
    float max_z = -999999;
    for (int j = 0; j < vertex_count[i]; j++) {
      float vx = vertices[i][3 * j + 0];
      float vy = vertices[i][3 * j + 1];
      float vz = vertices[i][3 * j + 2];
      if (vx < min_x) min_x = vx;
      if (vy < min_y) min_y = vy;
      if (vz < min_z) min_z = vz;
      if (vx > max_x) max_x = vx;
      if (vy > max_y) max_y = vy;
      if (vz > max_z) max_z = vz;
    }
    accessors[i].min[0] = min_x;
    accessors[i].min[1] = min_y;
    accessors[i].min[2] = min_z;
    accessors[i].max[0] = max_x;
    accessors[i].max[1] = max_y;
    accessors[i].max[2] = max_z;
  }

  size_t accessor_offset = 0;
  for (int i = 0; i < object_count; i++) {
    accessors[i+object_count] = (cgltf_accessor) {
      .name = "vertex_index",
      .component_type = cgltf_component_type_r_32u,
      .normalized = 0, // ???
      .type = cgltf_type_scalar,
      .offset = accessor_offset,
      .count = 3 * triangle_count[i],
      .stride = 4, // 32 bit
      .buffer_view = &buffer_views[1],
      .has_min = 0,
      .has_max = 0,
      .is_sparse = 0
    };
    accessor_offset += 4 * 3 * triangle_count[i];
  }

  for (int i = 0; i < object_count; i++) {
    accessors[2 * object_count + i] = (cgltf_accessor) {
      .name = "animation_rotation_output",
      .component_type = cgltf_component_type_r_32f,
      .normalized = 0,
      .type = cgltf_type_vec4,
      .offset = animation->frame_count * 16 * i,
      .count = animation->frame_count,
      .stride = 16,
      .buffer_view = &buffer_views[3]
    };
    accessors[3 * object_count + i] = (cgltf_accessor) {
      .name = "animation_translation_output",
      .component_type = cgltf_component_type_r_32f,
      .normalized = 0,
      .type = cgltf_type_vec3,
      .offset = animation->frame_count * 12 * i,
      .count = animation->frame_count,
      .stride = 12,
      .buffer_view = &buffer_views[4]
    };
  }

  size_t texcoord_offset = 0;
  for (int i = 0; i < object_count; i++) {
    accessors[4 * object_count + i] = (cgltf_accessor) {
      .name = "texcoord",
      .component_type = cgltf_component_type_r_32f,
      .normalized = 0,
      .type = cgltf_type_vec2,
      .offset = texcoord_offset,
      .count = texcoord_count[i],
      .stride = 8,
      .buffer_view = &buffer_views[5]
    };
    texcoord_offset += 4 * 2 * texcoord_count[i];
  }

  accessors[5 * object_count] = (cgltf_accessor) {
    .name = "animation_rotation_input",
    .component_type = cgltf_component_type_r_32f,
    .normalized = 0,
    .type = cgltf_type_scalar,
    .offset = 0,
    .count = animation->frame_count,
    .stride = 4,
    .buffer_view = &buffer_views[2],
    .has_min = 1,
    .has_max = 1
  };
  accessors[5 * object_count].min[0] = 0;
  accessors[5 * object_count].max[0] = (animation->frame_count - 1) * 0.0333333;

  cgltf_image images[1];
  images[0] = (cgltf_image) {
    .name = "texture_image",
    .buffer_view = &buffer_views[6],
    .mime_type = "image/png"
  };

  cgltf_sampler texture_samplers[1];
  texture_samplers[0] = (cgltf_sampler) {
    .name = "texture_sampler",
    .mag_filter = 9728, // NEAREST
    .min_filter = 9728, // NEAREST
    .wrap_s = 33071, // CLAMP_TO_EDGE
    .wrap_t = 33071 // CLAMP_TO_EDGE
  };

  cgltf_texture textures[1];
  textures[0] = (cgltf_texture) {
    .name = "texture",
    .image = &images[0],
    .sampler = &texture_samplers[0]
  };

  cgltf_texture_view texture_view = {
    .texture = &textures[0]
  };

  cgltf_pbr_metallic_roughness metallic_roughness = {
    .base_color_texture = texture_view,
    .metallic_factor = 0,
    .roughness_factor = 1
  };
  metallic_roughness.base_color_factor[0] = 1.0;
  metallic_roughness.base_color_factor[1] = 1.0;
  metallic_roughness.base_color_factor[2] = 1.0;
  metallic_roughness.base_color_factor[3] = 1.0;

  cgltf_material materials[1];
  materials[0] = (cgltf_material) {
    .name = "material",
    .has_pbr_metallic_roughness = 1,
    .pbr_metallic_roughness = metallic_roughness,
    .double_sided = 1,
    .alpha_mode = cgltf_alpha_mode_mask,
    .alpha_cutoff = 0.5
  };

  cgltf_attribute attributes[2 * object_count];
  for (int i = 0; i < object_count; i++) {
    attributes[2 * i] = (cgltf_attribute) {
      .name = "POSITION",
      .type = cgltf_attribute_type_position,
      .index = 0,
      .data = &accessors[i]
    };
    attributes[2 * i + 1] = (cgltf_attribute) {
      .name = "TEXCOORD_0",
      .type = cgltf_attribute_type_texcoord,
      .index = 0,
      .data = &accessors[4 * object_count + i]
    };
  }

  cgltf_primitive prims[object_count];
  for (int i = 0; i < object_count; i++) {
    prims[i] = (cgltf_primitive) {
      .type = cgltf_primitive_type_triangles,
      .indices = &accessors[i+object_count],
      .attributes = &attributes[2 * i],
      .attributes_count = 2,
      .material = &materials[0]
    };
  }

  cgltf_mesh meshes[object_count];
  for (int i = 0; i < object_count; i++) {
    meshes[i] = (cgltf_mesh) {
      .primitives = (cgltf_primitive*) { &prims[i] },
      .primitives_count = 1
    };
  }

  cgltf_animation_sampler samplers[2 * object_count];
  for (int i = 0; i < object_count; i++) {
    samplers[i] = (cgltf_animation_sampler) {
      .input = &accessors[5 * object_count],
      .output = &accessors[2 * object_count + i],
      .interpolation = cgltf_interpolation_type_step
    };

    samplers[object_count + i] = (cgltf_animation_sampler) {
      .input = &accessors[5 * object_count],
      .output = &accessors[3 * object_count + i],
      .interpolation = cgltf_interpolation_type_step
    };

  }

  cgltf_node nodes[object_count];
  for (int i = 0; i < object_count; i++) {
    cgltf_node* parent;
    if (node_tree[i] >= 0) {
      parent = &nodes[node_tree[i]];
    } else {
      parent = NULL;
    }
    cgltf_node** children = malloc(object_count * sizeof(cgltf_node*));
    size_t children_count = 0;
    for (int child_ix = i; child_ix < object_count; child_ix++) {
      if (node_tree[child_ix] == i) {
        children[children_count++] = &nodes[child_ix];
      }
    }
    if (children_count == 0) {
      children = NULL;
    }
    nodes[i] = (cgltf_node) {
      .name = "node",
      .parent = parent,
      .children = children,
      .children_count = children_count,
      .skin = NULL,
      .mesh = &meshes[i],
      .has_translation = 1
    };
    nodes[i].translation[0] = 0.0;
    nodes[i].translation[1] = 0.0;
    nodes[i].translation[2] = 0.0;
  }

  cgltf_node* root_nodes[object_count];
  int root_node_count = 0;
  for (int i = 0; i < object_count; i++) {
    if (node_tree[i] < 0) {
      root_nodes[root_node_count++] = &nodes[i];
    }
  }

  cgltf_animation_channel channels[2 * object_count];
  for (int i = 0; i < object_count; i++) {
    channels[i] = (cgltf_animation_channel) {
      .sampler = &samplers[i],
      .target_node = &nodes[i],
      .target_path = cgltf_animation_path_type_rotation
    };
    channels[object_count + i] = (cgltf_animation_channel) {
      .sampler = &samplers[object_count + i],
      .target_node = &nodes[i],
      .target_path = cgltf_animation_path_type_translation
    };
  }

  cgltf_animation animations[object_count];
  animations[0] = (cgltf_animation) {
    .name = "animation",
    .samplers = samplers,
    .samplers_count = 2 * object_count,
    .channels = channels,
    .channels_count = 2 * object_count
  };

  cgltf_scene scenes[1] = {
    {
      .name = "scene",
      .nodes = root_nodes,
      .nodes_count = root_node_count
    }
  };

  cgltf_data data = {0};
  data.meshes = meshes;
  data.meshes_count = object_count;

  data.animations = animations;
  data.animations_count = 1;

  data.accessors = accessors;
  data.accessors_count = 5 * object_count + 1;

  data.buffer_views = buffer_views;
  data.buffer_views_count = 7;

  data.buffers = buffers;
  data.buffers_count = 7;

  data.materials = materials;
  data.materials_count = 1;

  data.images = images;
  data.images_count = 1;

  data.textures = textures;
  data.textures_count = 1;

  data.samplers = texture_samplers;
  data.samplers_count = 1;

  data.nodes = nodes;
  data.nodes_count = object_count;

  data.scenes = scenes;
  data.scenes_count = 1;

  data.asset = (cgltf_asset) {
    .copyright = "",
    .generator = "bukosoft corporation",
    .version = "2.0"
  };

  cgltf_options options = {0};
  cgltf_result result = cgltf_write_file(&options, "out.gltf", &data);
  if (result != cgltf_result_success) {
    fprintf(stderr, "Bad cgltf result: %d\n", result);
  }
}

int main(int argc, char** argv) {
  if (argc < 6) {
    die("Usage: ./rip_model.c INFILE MODEL_SECTOR ANIMATION_SECTOR FRAME OUTFILE");
  }
  FILE* fp;
  fp = fopen(argv[1], "r");
  if (!fp) {
    die("Failed to open file");
  }

  iso_t iso;
  iso_open(&iso, fp);

  model_t new_model;
  uint32_t model_sector = strtoul(argv[2], NULL, 16);
  new_model = load_model(&iso, model_sector);

  animation_t animation;
  uint32_t animation_sector = strtoul(argv[3], NULL, 16);
  animation = load_animation(&iso, animation_sector, new_model.object_count);

  fprintf(stderr, "Frame table:\n");
  for (int frame = 0; frame < animation.frame_count; frame++) {
    for (int object = 0; object < new_model.object_count; object++) {
      fprintf(stderr, "%02d ",
        animation.frame_table[frame * new_model.object_count + object]);
    }
    fprintf(stderr, "\n");
  }

  uint32_t frame = atoi(argv[4]);

  fprintf(stderr, "Loaded model\n");
  fprintf(stderr, "Model texture_sheet_offset: %xh\n", new_model.texture_sheet_offset);
  fprintf(stderr, "Model object_count: %d\n", new_model.object_count);
  fprintf(stderr, "Model skeleton:");
  for (int i = 0; i < new_model.object_count; i++) {
    fprintf(stderr, " %d", new_model.skeleton[i]);
  }
  fprintf(stderr, "\n");
  fprintf(stderr, "Model node tree:");
  for (int i = 0; i < new_model.object_count; i++) {
    fprintf(stderr, " %d", new_model.node_tree[i]);
  }
  fprintf(stderr, "\n");

  uint32_t verts_seen = 0;
  uint32_t texcoords_seen = 0;

  for (int i = 0; i < new_model.object_count; i++) {
    fprintf(stderr, "offsets[%d]: (%xh, %xh, %xh)\n",
      i,
      new_model.vertex_offsets[i],
      new_model.normal_offsets[i],
      new_model.face_offsets[i]);
  }

  float* flat_vert_table[new_model.object_count];
  memset(flat_vert_table, 0, new_model.object_count * sizeof(float*));
  size_t flat_vert_counts[new_model.object_count];
  memset(flat_vert_counts, 0, new_model.object_count * sizeof(size_t));
  uint32_t* flat_tri_table[new_model.object_count];
  memset(flat_tri_table, 0, new_model.object_count * sizeof(uint32_t*));
  size_t flat_tri_counts[new_model.object_count];
  memset(flat_tri_counts, 0, new_model.object_count * sizeof(size_t));
  float* texcoord_table[new_model.object_count];
  memset(texcoord_table, 0, new_model.object_count * sizeof(float*));
  size_t texcoord_counts[new_model.object_count];
  memset(texcoord_counts, 0, new_model.object_count * sizeof(size_t));

  uint16_t exported_palettes[16];
  size_t exported_palettes_count = 0;

  for (int j = 0; j < new_model.object_count; j++) {
    uint32_t num_read;
    vertex_t* verts;
    verts = load_vertices(&new_model, j, &num_read);
    uint32_t num_quads_read;
    uint32_t num_tris_read;
    polys_t polys;
    polys = load_faces(&new_model, j, &num_quads_read, &num_tris_read);
    uint32_t* flat_tris = calloc(
      num_quads_read * 2 + num_tris_read,
      3 * sizeof(uint32_t)
    );
    float* texcoords = calloc(
      6 * (num_quads_read * 2 + num_tris_read),
      sizeof(float)
    );
    float* flat_verts = calloc(
      6 * num_quads_read + 3 * num_tris_read,
      3 * sizeof(float));
    flat_vert_table[j] = flat_verts;
    flat_vert_counts[j] = 6 * num_quads_read + 3 * num_tris_read;

    for (int i = 0; i < num_quads_read; i++) {
      face_quad_t* quads = polys.quads;
      uint8_t this_palette = quads[i].palette;
      uint8_t this_clut = quads[i].clut;
      uint16_t pal_clut_packed = (this_clut << 8) | this_palette;
      int palette_is_new = 1;
      int pal;
      for (pal = 0; pal < exported_palettes_count; pal++) {
        if (exported_palettes[pal] == pal_clut_packed) {
          palette_is_new = 0;
          break;
        }
      }
      if (palette_is_new) {
        if (exported_palettes_count >= 16) {
          die("Too many palettes referenced in file!");
        }
        exported_palettes[exported_palettes_count++] = pal_clut_packed;
        fprintf(stderr,
          "Encountered new palette %04x (y=%02x, x=%02x)\n",
          pal_clut_packed,
          pal_clut_packed >> 6,
          pal_clut_packed & 0x3f);
      }

      int tex_page_x = pal % 8;
      int tex_page_y = pal / 8;
      float tex_offs_x = (float) tex_page_x / 8;
      float tex_offs_y = (float) tex_page_y / 4;
      texcoords[12 * i +  0] = quads[i].tex_c_x / 1024.0 + tex_offs_x;
      texcoords[12 * i +  1] = quads[i].tex_c_y / 1024.0 + tex_offs_y;
      texcoords[12 * i +  2] = quads[i].tex_b_x / 1024.0 + tex_offs_x;
      texcoords[12 * i +  3] = quads[i].tex_b_y / 1024.0 + tex_offs_y;
      texcoords[12 * i +  4] = quads[i].tex_a_x / 1024.0 + tex_offs_x;
      texcoords[12 * i +  5] = quads[i].tex_a_y / 1024.0 + tex_offs_y;
      texcoords[12 * i +  6] = quads[i].tex_b_x / 1024.0 + tex_offs_x;
      texcoords[12 * i +  7] = quads[i].tex_b_y / 1024.0 + tex_offs_y;
      texcoords[12 * i +  8] = quads[i].tex_c_x / 1024.0 + tex_offs_x;
      texcoords[12 * i +  9] = quads[i].tex_c_y / 1024.0 + tex_offs_y;
      texcoords[12 * i + 10] = quads[i].tex_d_x / 1024.0 + tex_offs_x;
      texcoords[12 * i + 11] = quads[i].tex_d_y / 1024.0 + tex_offs_y;
      flat_verts[18 * i +  0] = -verts[quads[i].vertex_c].x / 4096.0;
      flat_verts[18 * i +  1] = -verts[quads[i].vertex_c].y / 4096.0;
      flat_verts[18 * i +  2] = verts[quads[i].vertex_c].z / 4096.0;
      flat_verts[18 * i +  3] = -verts[quads[i].vertex_b].x / 4096.0;
      flat_verts[18 * i +  4] = -verts[quads[i].vertex_b].y / 4096.0;
      flat_verts[18 * i +  5] = verts[quads[i].vertex_b].z / 4096.0;
      flat_verts[18 * i +  6] = -verts[quads[i].vertex_a].x / 4096.0;
      flat_verts[18 * i +  7] = -verts[quads[i].vertex_a].y / 4096.0;
      flat_verts[18 * i +  8] = verts[quads[i].vertex_a].z / 4096.0;
      flat_verts[18 * i +  9] = -verts[quads[i].vertex_b].x / 4096.0;
      flat_verts[18 * i + 10] = -verts[quads[i].vertex_b].y / 4096.0;
      flat_verts[18 * i + 11] = verts[quads[i].vertex_b].z / 4096.0;
      flat_verts[18 * i + 12] = -verts[quads[i].vertex_c].x / 4096.0;
      flat_verts[18 * i + 13] = -verts[quads[i].vertex_c].y / 4096.0;
      flat_verts[18 * i + 14] = verts[quads[i].vertex_c].z / 4096.0;
      flat_verts[18 * i + 15] = -verts[quads[i].vertex_d].x / 4096.0;
      flat_verts[18 * i + 16] = -verts[quads[i].vertex_d].y / 4096.0;
      flat_verts[18 * i + 17] = verts[quads[i].vertex_d].z / 4096.0;
      flat_tris[6 * i + 0] = 6 * i + 0;
      flat_tris[6 * i + 1] = 6 * i + 1;
      flat_tris[6 * i + 2] = 6 * i + 2;
      flat_tris[6 * i + 3] = 6 * i + 3;
      flat_tris[6 * i + 4] = 6 * i + 4;
      flat_tris[6 * i + 5] = 6 * i + 5;
    }
    for (int i = 0; i < num_tris_read; i++) {
      face_tri_t* tris = polys.tris;
      uint8_t this_palette = tris[i].palette;
      uint8_t this_clut = tris[i].clut;
      uint16_t pal_clut_packed = (this_clut << 8) | this_palette;
      int palette_is_new = 1;
      int pal;
      for (pal = 0; pal < exported_palettes_count; pal++) {
        if (exported_palettes[pal] == pal_clut_packed) {
          palette_is_new = 0;
          break;
        }
      }
      if (palette_is_new) {
        if (exported_palettes_count >= 16) {
          die("Too many palettes referenced in file!");
        }
        exported_palettes[exported_palettes_count++] = pal_clut_packed;
        fprintf(stderr,
          "Encountered new palette %04x (y=%02x, x=%02x)\n",
          pal_clut_packed,
          pal_clut_packed >> 6,
          pal_clut_packed & 0x3f);
      }

      int tex_page_x = pal % 8;
      int tex_page_y = pal / 8;
      float tex_offs_x = (float) tex_page_x / 8;
      float tex_offs_y = (float) tex_page_y / 4;

      texcoords[6 * i + 0 + (12 * num_quads_read)] = tris[i].tex_a_x / 1024.0 + tex_offs_x;
      texcoords[6 * i + 1 + (12 * num_quads_read)] = tris[i].tex_a_y / 1024.0 + tex_offs_y;
      texcoords[6 * i + 2 + (12 * num_quads_read)] = tris[i].tex_c_x / 1024.0 + tex_offs_x;
      texcoords[6 * i + 3 + (12 * num_quads_read)] = tris[i].tex_c_y / 1024.0 + tex_offs_y;
      texcoords[6 * i + 4 + (12 * num_quads_read)] = tris[i].tex_b_x / 1024.0 + tex_offs_x;
      texcoords[6 * i + 5 + (12 * num_quads_read)] = tris[i].tex_b_y / 1024.0 + tex_offs_y;
      size_t this_tri_offset = 18 * num_quads_read + 9 * i;
      flat_verts[this_tri_offset + 0] = -verts[tris[i].vertex_a].x / 4096.0;
      flat_verts[this_tri_offset + 1] = -verts[tris[i].vertex_a].y / 4096.0;
      flat_verts[this_tri_offset + 2] = verts[tris[i].vertex_a].z / 4096.0;
      flat_verts[this_tri_offset + 3] = -verts[tris[i].vertex_c].x / 4096.0;
      flat_verts[this_tri_offset + 4] = -verts[tris[i].vertex_c].y / 4096.0;
      flat_verts[this_tri_offset + 5] = verts[tris[i].vertex_c].z / 4096.0;
      flat_verts[this_tri_offset + 6] = -verts[tris[i].vertex_b].x / 4096.0;
      flat_verts[this_tri_offset + 7] = -verts[tris[i].vertex_b].y / 4096.0;
      flat_verts[this_tri_offset + 8] = verts[tris[i].vertex_b].z / 4096.0;
      flat_tris[3 * i + 0 + (6 * num_quads_read)] = 3 * i + 0 + (6 * num_quads_read);
      flat_tris[3 * i + 1 + (6 * num_quads_read)] = 3 * i + 1 + (6 * num_quads_read);
      flat_tris[3 * i + 2 + (6 * num_quads_read)] = 3 * i + 2 + (6 * num_quads_read);
    }
    flat_tri_table[j] = flat_tris;
    flat_tri_counts[j] = 2 * num_quads_read + num_tris_read;
    texcoord_table[j] = texcoords;
    texcoord_counts[j] = num_quads_read * 6 + num_tris_read * 3;
    texcoords_seen += num_quads_read * 4 + num_tris_read * 3;
    verts_seen += num_read;
  }
  paletted_texture_t tex = load_texture(&new_model);
  for (int pal = 0; pal < exported_palettes_count; pal++) {
    uint16_t clut = exported_palettes[pal];
    fprintf(stderr, "Loading the texture with %02x,%02x\n", clut & 0x3f, clut >> 6);
    size_t offset_x = 128 * (pal % 8);
    size_t offset_y = 256 * (pal / 8);
    blit_to_png_write_buffer(&tex, clut & 0x3f, clut >> 6, offset_x, offset_y);
  }
  png_alloc_size_t png_alloc = save_png_write_buffer();
  make_epic_gltf_file(
    flat_vert_table,
    flat_vert_counts,
    flat_tri_table,
    flat_tri_counts,
    texcoord_table,
    texcoord_counts,
    &animation,
    new_model.node_tree,
    new_model.object_count,
    png_alloc
  );
}
