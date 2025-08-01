#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "matrix.h"

#include "iso_reader.h"
#define CGLTF_WRITE_IMPLEMENTATION
#include "cgltf_write.h"

typedef struct blink_s {
  uint8_t start_x;
  uint8_t start_y;
  uint8_t extent_x;
  uint8_t extent_y;
  uint8_t eye_0_x;
  uint8_t eye_0_y;
  uint8_t eye_1_x;
  uint8_t eye_1_y;
  uint8_t eye_2_x;
  uint8_t eye_2_y;
} blink_t;

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
  blink_t blink[6];
  size_t blink_count;
} model_t;

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
  uint8_t cmd_upper;
  uint8_t cmd_lower;
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
  uint8_t cmd_upper;
  uint8_t cmd_lower;
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

uint8_t* expand_texture_paletted(paletted_texture_t* tex, uint8_t column, uint8_t row, int semitransparent) {
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

    uint8_t opacity = semitransparent ? 127 : 255;

    expanded[8 * i + 0] = (color_lower & 0x001f) << 3;
    expanded[8 * i + 1] = (color_lower & 0x03e0) >> 2;
    expanded[8 * i + 2] = (color_lower & 0x7c00) >> 7;
    expanded[8 * i + 3] = (color_lower == 0x0000) ? 0 : opacity;
    expanded[8 * i + 4] = (color_upper & 0x001f) << 3;
    expanded[8 * i + 5] = (color_upper & 0x03e0) >> 2;
    expanded[8 * i + 6] = (color_upper & 0x7c00) >> 7;
    expanded[8 * i + 7] = (color_upper == 0x0000) ? 0 : opacity;

    if (semitransparent && color_lower == 0x8000) {
      expanded[8 * i + 3] = 0;
    }
    if (semitransparent && color_upper == 0x8000) {
      expanded[8 * i + 7] = 0;
    }
  }
  return expanded;
}

char* googa = "googa.png";

// This is the buffer where raw pixels will be blitted to, used to generate the
// png. 1024x1024 pixels RGBA
uint8_t png_write_buffer[4*1024*1024];

void blit_to_png_write_buffer(paletted_texture_t* tex, uint8_t column, uint8_t row, int semitransparent, size_t offset_x, size_t offset_y) {
  uint8_t* texture_expanded = expand_texture_paletted(tex, column, row, semitransparent);
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
  free(texture_expanded);
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
  /*
  png_image_write_to_file(
    &png,
    filename,
    0,
    png_write_buffer,
    0,
    NULL);
  */
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
  uint32_t count = 0;

  iso_fread(model->iso, &count, sizeof(uint32_t), 1);
  fprintf(stderr, "%u semi-transparent quads to read\n", count);
  face_quad_t* semi_transparent_quads = malloc(sizeof(face_quad_t) * count);
  iso_fread(model->iso, semi_transparent_quads, sizeof(face_quad_t), count);
  uint32_t semi_transparent_quad_count = count;

  iso_fread(model->iso, &count, sizeof(uint32_t), 1);
  fprintf(stderr, "%u opaque quads to read\n", count);
  face_quad_t* opaque_quads = malloc(sizeof(face_quad_t) * count);
  iso_fread(model->iso, opaque_quads, sizeof(face_quad_t), count);
  uint32_t opaque_quad_count = count;

  uint32_t quad_count = semi_transparent_quad_count + opaque_quad_count;
  *num_quads_read = quad_count;

  face_quad_t* all_quads = malloc(sizeof(face_quad_t) * quad_count);
  memcpy(
    all_quads,
    semi_transparent_quads,
    sizeof(face_quad_t) * semi_transparent_quad_count);
  free(semi_transparent_quads);
  memcpy(
    &all_quads[semi_transparent_quad_count],
    opaque_quads,
    sizeof(face_quad_t) * opaque_quad_count);
  free(opaque_quads);

  iso_fread(model->iso, &count, sizeof(uint32_t), 1);
  fprintf(stderr, "%u semi-transparent tris to read\n", count);
  face_tri_t* semi_transparent_tris = malloc(sizeof(face_tri_t) * count);
  iso_fread(model->iso, semi_transparent_tris, sizeof(face_tri_t), count);
  uint32_t semi_transparent_tri_count = count;

  iso_fread(model->iso, &count, sizeof(uint32_t), 1);
  fprintf(stderr, "%u opaque tris to read\n", count);
  face_tri_t* opaque_tris = malloc(sizeof(face_tri_t) * count);
  iso_fread(model->iso, opaque_tris, sizeof(face_tri_t), count);
  uint32_t opaque_tri_count = count;

  uint32_t tri_count = semi_transparent_tri_count + opaque_tri_count;
  *num_tris_read = tri_count;

  face_tri_t* all_tris = malloc(sizeof(face_tri_t) * tri_count);
  memcpy(
    all_tris,
    semi_transparent_tris,
    sizeof(face_tri_t) * semi_transparent_tri_count);
  free(semi_transparent_tris);
  memcpy(
    &all_tris[semi_transparent_tri_count],
    opaque_tris,
    sizeof(face_tri_t) * opaque_tri_count);
  free(opaque_tris);

  return (polys_t) {
    .quads = all_quads,
    .tris = all_tris
  };
}

typedef struct animation_s {
  iso_t* iso;
  uint32_t file_sector;
  uint32_t* transform_offsets;
  uint32_t* keyframe_offsets;
  uint8_t** frame_tables;
  size_t animation_count;
  uint32_t* frame_counts;
  size_t max_keyframe;
  char animation_labels[8];
} animation_t;

void free_animation(animation_t* animation) {
  free(animation->transform_offsets);
  free(animation->keyframe_offsets);
  for (int i = 0; i < animation->animation_count; i++) {
    free(animation->frame_tables[i]);
  }
  free(animation->frame_tables);
  free(animation->frame_counts);
}

animation_t load_animation(iso_t* iso, uint32_t sector, uint32_t object_count) {
  animation_t animation;
  animation.iso = iso;
  animation.file_sector = sector;
  animation.keyframe_offsets = NULL;
  animation.frame_counts = malloc(256);
  animation.max_keyframe = 0;
  iso_seek_to_sector(iso, sector);
  uint32_t unused;
  iso_fread(iso, &unused, sizeof(uint32_t), 1);
  if (unused != 0) {
    fprintf(stderr, "Expected the first word of animation to be 0,"
      "but it was %x\n", unused);
    exit(1);
  }
  size_t items_read;
  animation.transform_offsets = malloc(object_count * sizeof(uint32_t));
  items_read = iso_fread(
    iso,
    animation.transform_offsets,
    sizeof(uint32_t),
    object_count);
  if (items_read != object_count) {
    die("fread failure, an error occured or EOF (animation offsets)");
  }

  uint32_t keyframe_offset = 0;
  size_t animation_count = 0;
  char animation_label = '0';
  while (1) {
    items_read = iso_fread(iso, &keyframe_offset, sizeof(uint32_t), 1);
    if (items_read != 1) {
      die("fread failure, an error occured or EOF (keyframe offsets)");
    }
    animation_label++;
    if (keyframe_offset == 0) {
      continue;
    }
    if (keyframe_offset == 1) {
      break;
    }
    animation_count += 1;
    animation.keyframe_offsets = realloc(animation.keyframe_offsets, sizeof(uint32_t)*animation_count);
    animation.keyframe_offsets[animation_count - 1] = keyframe_offset;
    animation.animation_labels[animation_count - 1] = animation_label;
  }

  animation.frame_tables = malloc(sizeof(uint8_t*) * animation_count);
  for (int i = 0; i < animation_count; i++) {
    fprintf(stderr, "Animation: %d\n", i);
    iso_seek_to_sector(animation.iso, animation.file_sector);
    iso_seek_forward(animation.iso, animation.keyframe_offsets[i]);

    // Make space for 256 frames, should be enough, error if we run out
    animation.frame_tables[i] = malloc(object_count * 256);

    int frames_left = 1;
    uint32_t frame_count = 0;
    while (frames_left) {
      items_read = iso_fread(
        iso,
        &animation.frame_tables[i][object_count * frame_count],
        sizeof(uint8_t),
        object_count);

      if (items_read != object_count) {
        die("fread failure, an error occured or EOF (frame table)");
      }
      // What is the difference between fe and ff?
      if (animation.frame_tables[i][object_count * frame_count] == 0xfe ||
          animation.frame_tables[i][object_count * frame_count] == 0xff) {
        frames_left = 0;
        fprintf(stderr, "Frame count: %d\n", frame_count);
        animation.frame_counts[i] = frame_count;
      } else {
        // Update the max keyframe so that later we know how many transform
        // matrices to read
        for (int j = object_count * frame_count; j < (object_count + 1) * frame_count; j++) {
          if (animation.frame_tables[i][j] > animation.max_keyframe) {
            animation.max_keyframe = animation.frame_tables[i][j];
          }
        }
        frame_count++;
      }
      if (frame_count >= 256) {
        die("Too many frames. Could this be a bug?");
      }
    }
  }
  fprintf(stderr, "Max keyframe: %ld\n", animation.max_keyframe);
  animation.animation_count = animation_count;
  return animation;
}

void serialize_animation(animation_t* animation, size_t animation_index, uint32_t object_count, float** rotation_out, float** translation_out, float** scale_out) {
  size_t animation_frames_count = animation->frame_counts[animation_index];
  size_t animation_max_keyframe = animation->max_keyframe + 1;
  float* rotation = malloc(animation_max_keyframe * 16 * object_count); // N quaternions, each quaternion is 4 floats, times object_count
  float* translation = malloc(animation_max_keyframe * 12 * object_count); // N translation vectors of 3 floats each, times object_count
  float* scale = malloc(animation_max_keyframe * 12 * object_count); // N scale vectors of 3 floats each, times object_count
  uint32_t object_start_rot = 0;
  uint32_t object_start_dest_rot = 0;
  uint32_t object_start_trans = 0;
  uint32_t object_start_dest_trans = 0;
  uint32_t object_start_scale = 0;
  uint32_t object_start_dest_scale = 0;
  for (int object = 0; object < object_count; object++) {
    uint32_t offset = animation->transform_offsets[object];
    iso_seek_to_sector(animation->iso, animation->file_sector);
    iso_seek_forward(animation->iso, offset);
    matrix_t m;

    // Read one matrix and one translation vector for each keyframe across all
    // animations in the file. This actually reads more than it needs to because
    // typically not all objects will have the same number of keyframes as
    // animation.max_keyframe. So if we are near the end of the rom this code
    // could error from reading further than it needs to.
    for (int frame = 0; frame < animation_max_keyframe; frame++) {
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
      fmatrix_t rotate_matrix;
      fmatrix_t scale_matrix;
      decompose(fm, &scale_matrix, &rotate_matrix);
      fprintf(stderr, "Scale by: [%.02f %.02f %.02f]\n",
        scale_matrix.x[0],
        scale_matrix.x[4],
        scale_matrix.x[8]);
      quaternion_t q = matrix_to_quaternion(rotate_matrix);
      normalize_quaternion_inplace(&q);
      fprintf(stderr, "object %d/%d, keyframe %d/%ld\n",
        object + 1, object_count,
        frame + 1, animation_max_keyframe);
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
      scale[object_start_scale + frame * 3 + 0] = scale_matrix.x[0];
      scale[object_start_scale + frame * 3 + 1] = scale_matrix.x[4];
      scale[object_start_scale + frame * 3 + 2] = scale_matrix.x[8];
    }
    object_start_rot += animation_max_keyframe * 4;
    object_start_trans += animation_max_keyframe * 3;
    object_start_scale += animation_max_keyframe * 3;
  }
  float* rotation_final = malloc(animation_frames_count * 16 * object_count);
  // TODO: 12, not 16?
  float* translation_final = malloc(animation_frames_count * 16 * object_count);
  // TODO: 12, not 16?
  float* scale_final = malloc(animation_frames_count * 16 * object_count);
  for (int object = 0; object < object_count; object++) {
    object_start_rot = animation_max_keyframe * 4 * object;
    object_start_dest_rot = animation_frames_count * 4 * object;
    object_start_trans = animation_max_keyframe * 3 * object;
    object_start_dest_trans = animation_frames_count * 3 * object;
    object_start_scale = animation_max_keyframe * 3 * object;
    object_start_dest_scale = animation_frames_count * 3 * object;
    for (int frame = 0; frame < animation_frames_count; frame++) {
      uint8_t from = animation->frame_tables[animation_index][frame * object_count + object];
      memcpy(
        &rotation_final[object_start_dest_rot + frame * 4],
        &rotation[object_start_rot + from * 4],
        4 * sizeof(float));
      memcpy(
        &translation_final[object_start_dest_trans + frame * 3],
        &translation[object_start_trans + from * 3],
        3 * sizeof(float));
      memcpy(
        &scale_final[object_start_dest_scale + frame * 3],
        &scale[object_start_scale + from * 3],
        3 * sizeof(float));
    }
  }
  free(rotation);
  free(translation);
  free(scale);
  *rotation_out = rotation_final;
  *translation_out = translation_final;
  *scale_out = scale_final;
}

// We transform a vertex by looking up its object's transform matrix and
// translation on the given frame number, and then potentially recursing with the parent object
vertex_t transform_vertex(vertex_t v, model_t* model, animation_t* animation, uint32_t object, uint32_t frame) {
  uint32_t offset = animation->transform_offsets[object];
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

void read_blink(iso_t* iso, blink_t* blink) {
  size_t items_read;
  items_read = iso_fread(
    iso,
    &blink->start_x,
    sizeof(uint8_t),
    1);
  if (items_read != 1) {
    die("read_blink: iso_fread error");
  }
  items_read = iso_fread(
    iso,
    &blink->start_y,
    sizeof(uint8_t),
    1);
  if (items_read != 1) {
    die("read_blink: iso_fread error");
  }
  items_read = iso_fread(
    iso,
    &blink->extent_x,
    sizeof(uint8_t),
    1);
  if (items_read != 1) {
    die("read_blink: iso_fread error");
  }
  items_read = iso_fread(
    iso,
    &blink->extent_y,
    sizeof(uint8_t),
    1);
  if (items_read != 1) {
    die("read_blink: iso_fread error");
  }
  items_read = iso_fread(
    iso,
    &blink->eye_0_x,
    sizeof(uint8_t),
    1);
  if (items_read != 1) {
    die("read_blink: iso_fread error");
  }
  items_read = iso_fread(
    iso,
    &blink->eye_0_y,
    sizeof(uint8_t),
    1);
  if (items_read != 1) {
    die("read_blink: iso_fread error");
  }
  items_read = iso_fread(
    iso,
    &blink->eye_1_x,
    sizeof(uint8_t),
    1);
  if (items_read != 1) {
    die("read_blink: iso_fread error");
  }
  items_read = iso_fread(
    iso,
    &blink->eye_1_y,
    sizeof(uint8_t),
    1);
  if (items_read != 1) {
    die("read_blink: iso_fread error");
  }
  items_read = iso_fread(
    iso,
    &blink->eye_2_x,
    sizeof(uint8_t),
    1);
  if (items_read != 1) {
    die("read_blink: iso_fread error");
  }
  items_read = iso_fread(
    iso,
    &blink->eye_2_y,
    sizeof(uint8_t),
    1);
  if (items_read != 1) {
    die("read_blink: iso_fread error");
  }
}

model_t load_model(iso_t* iso, uint32_t sector) {
  model_t new_model;
  new_model.iso = iso;
  new_model.file_sector = sector;
  new_model.blink_count = 0;
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

  for (int i = 0; 1; i++) {
    read_blink(iso, &new_model.blink[i]);
    fprintf(stderr, "start_x: %d\n", new_model.blink[i].start_x);
    if (new_model.blink[i].start_x == 0xfe ||
        new_model.blink[i].start_x == 0xff) {
      break;
    }
    new_model.blink_count++;
  }

  return new_model;
}

void make_epic_gltf_file(char* working_dir, float** vertices, size_t* vertex_count, uint32_t** tri_indices, size_t* triangle_count, float** texcoords, size_t* texcoord_count, animation_t* animations, size_t animation_file_count, char* animation_labels, int32_t* node_tree, size_t object_count, size_t png_alloc, blink_t* blinks, size_t blink_count) {
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

  char* vertex_encoded = octet_stream_encode(all_vertices, 4 * 3 * total_vertices);
  fprintf(stderr, "vertex encoded buffer size: %d\n", 4 * 3 * total_vertices);
  free(all_vertices);
  char* index_encoded = octet_stream_encode(all_triangles, 4 * 3 * total_triangles);
  fprintf(stderr, "index encoded buffer size: %d\n", 4 * 3 * total_triangles);
  free(all_triangles);

  char* texcoord_encoded = octet_stream_encode(all_texcoords, 4 * 2 * total_texcoords);
  fprintf(stderr, "texcoord encoded buffer size: %d\n", 4 * 2 * total_texcoords);
  free(all_texcoords);

  char* png_encoded = octet_stream_encode(png_buffer, png_alloc);
  fprintf(stderr, "texture png encoded buffer size: %d\n", png_alloc);

  size_t total_animation_count = 0;
  for (int i = 0; i < animation_file_count; i++) {
    total_animation_count += animations[i].animation_count;
  }

  cgltf_buffer buffers[4 * total_animation_count + 4];
  buffers[0] = (cgltf_buffer) {
    .name = "vertex_buffer",
    .size = 4 * 3 * total_vertices,
    .uri = vertex_encoded
  };
  buffers[1] = (cgltf_buffer) {
    .name = "vertex_index_buffer",
    .size = 4 * 3 * total_triangles,
    // 3 indices of 32-bit size, little-endian, "0 1 2"
    .uri = index_encoded
  };

  size_t animation_counter = 0;
  // Create a buffer for each animation
  for (size_t animation_file = 0; animation_file < animation_file_count; animation_file++) {
    animation_t* animation = &animations[animation_file];
    for (size_t anim = 0; anim < animation->animation_count; anim++) {
      size_t frame_count = animation->frame_counts[anim];
      float animation_input[frame_count];
      for (int i = 0; i < frame_count; i++) {
        animation_input[i] = (float) (i * 0.0333333); // 30 FPS
      }
      char* animation_input_encoded = octet_stream_encode(animation_input, frame_count * sizeof(float));
      fprintf(stderr, "animation input %d encoded buffer size: %d\n", animation_counter, frame_count * sizeof(float));
      buffers[animation_counter * 4 + 2] = (cgltf_buffer)
        {
          .name = "animation_input",
          .size = frame_count * sizeof(float),
          .uri = animation_input_encoded,
        };

      float* rotation_anim;
      float* translation_anim;
      float* scale_anim;
      serialize_animation(animation, anim, object_count, &rotation_anim, &translation_anim, &scale_anim);
      char* rotation_encoded = octet_stream_encode(rotation_anim,
        object_count * frame_count * 4 * sizeof(float));
      fprintf(stderr, "rotation encoded buffer size: %d\n", object_count * frame_count * 4 * sizeof(float));
      free(rotation_anim);
      char* translation_encoded = octet_stream_encode(translation_anim,
        object_count * frame_count * 3 * sizeof(float));
      fprintf(stderr, "translation encoded buffer size: %d\n", object_count * frame_count * 4 * sizeof(float));
      free(translation_anim);
      char* scale_encoded = octet_stream_encode(scale_anim,
        object_count * frame_count * 3 * sizeof(float));
      fprintf(stderr, "scale encoded buffer size: %d\n", object_count * frame_count * 4 * sizeof(float));
      free(scale_anim);

      buffers[animation_counter * 4 + 3] = (cgltf_buffer)
        {
          .name = "animation_rotation_output",
          .size = object_count * frame_count * 4 * sizeof(float),
          .uri = rotation_encoded,
        };
      buffers[animation_counter * 4 + 4] = (cgltf_buffer)
        {
          .name = "animation_translation_output",
          .size = object_count * frame_count * 3 * sizeof(float),
          .uri = translation_encoded
        };
      buffers[animation_counter * 4 + 5] = (cgltf_buffer)
        {
          .name = "animation_scale_output",
          .size = object_count * frame_count * 3 * sizeof(float),
          .uri = scale_encoded
        };
      animation_counter++;
    }
  }
  buffers[total_animation_count * 4 + 2] = (cgltf_buffer)
    {
      .name = "texcoord",
      .size = 4 * 2 * total_texcoords,
      .uri = texcoord_encoded
    };
  buffers[total_animation_count * 4 + 3] = (cgltf_buffer)
    {
      .name = "texture_buffer",
      .size = png_alloc,
      .uri = png_encoded
    };
  cgltf_buffer_view buffer_views[4 * total_animation_count + 4];
  buffer_views[0] = (cgltf_buffer_view)
    {
      .name = "vertex_buffer_view",
      .buffer = &buffers[0],
      .offset = 0,
      .size = 4 * 3 * total_vertices,
      .stride = 12,
      .type = cgltf_buffer_view_type_vertices
    };
  buffer_views[1] = (cgltf_buffer_view)
    {
      .name = "vertex_index_buffer_view",
      .buffer = &buffers[1],
      .offset = 0,
      .size = 4 * 3 * total_triangles,
      //.stride = 0,
      .type = cgltf_buffer_view_type_indices
    };

  animation_counter = 0;
  // Create a buffer for each animation
  for (size_t animation_file = 0; animation_file < animation_file_count; animation_file++) {
    animation_t* animation = &animations[animation_file];
    for (int anim = 0; anim < animation->animation_count; anim++) {
      size_t frame_count = animation->frame_counts[anim];
      buffer_views[animation_counter * 4 + 2] = (cgltf_buffer_view)
        {
          .name = "animation_input",
          .buffer = &buffers[animation_counter * 4 + 2],
          .offset = 0,
          .size = frame_count * sizeof(float)
        };
      buffer_views[animation_counter * 4 + 3] = (cgltf_buffer_view)
        {
          .name = "animation_rotation_output_view",
          .buffer = &buffers[animation_counter * 4 + 3],
          .offset = 0,
          .size = object_count * frame_count * 4 * sizeof(float),
        };
      buffer_views[animation_counter * 4 + 4] = (cgltf_buffer_view)
        {
          .name = "animation_translation_output_view",
          .buffer = &buffers[animation_counter * 4 + 4],
          .offset = 0,
          .size = object_count * frame_count * 3 * sizeof(float),
        };
      buffer_views[animation_counter * 4 + 5] = (cgltf_buffer_view)
        {
          .name = "animation_scale_output_view",
          .buffer = &buffers[animation_counter * 4 + 5],
          .offset = 0,
          .size = object_count * frame_count * 3 * sizeof(float),
        };
      animation_counter++;
    }
  }
  buffer_views[total_animation_count * 4 + 2] = (cgltf_buffer_view)
    {
      .name = "texcoord_view",
      .buffer = &buffers[total_animation_count * 4 + 2],
      .offset = 0,
      .size = 4 * 2 * total_texcoords,
      .stride = 8
    };
  buffer_views[total_animation_count * 4 + 3] = (cgltf_buffer_view)
    {
      .name = "texture_view",
      .buffer = &buffers[total_animation_count * 4 + 3],
      .offset = 0,
      .size = png_alloc
    };
  cgltf_accessor accessors[4 * object_count * total_animation_count + 3 * object_count];
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
    accessors[i + object_count] = (cgltf_accessor) {
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

  animation_counter = 0;
  // Create a buffer for each animation
  for (size_t animation_file = 0; animation_file < animation_file_count; animation_file++) {
    animation_t* animation = &animations[animation_file];
    for (int anim = 0; anim < animation->animation_count; anim++) {
      size_t frame_count = animation->frame_counts[anim];
      for (int i = 0; i < object_count; i++) {
        accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i] = (cgltf_accessor) {
          .name = "animation_input",
          .component_type = cgltf_component_type_r_32f,
          .normalized = 0,
          .type = cgltf_type_scalar,
          .offset = 0,
          .count = frame_count,
          .stride = 4,
          .buffer_view = &buffer_views[animation_counter * 4 + 2],
          .has_min = 1,
          .has_max = 1
        };
        accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i].min[0] = 0;
        accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i].max[0] =
          (frame_count - 1) * 0.0333333;

        accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i + 1] = (cgltf_accessor) {
          .name = "animation_rotation_output",
          .component_type = cgltf_component_type_r_32f,
          .normalized = 0,
          .type = cgltf_type_vec4,
          .offset = frame_count * 16 * i,
          .count = frame_count,
          .stride = 16,
          .buffer_view = &buffer_views[animation_counter * 4 + 3]
        };

        accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i + 2] = (cgltf_accessor) {
          .name = "animation_translation_output",
          .component_type = cgltf_component_type_r_32f,
          .normalized = 0,
          .type = cgltf_type_vec3,
          .offset = frame_count * 12 * i,
          .count = frame_count,
          .stride = 12,
          .buffer_view = &buffer_views[animation_counter * 4 + 4]
        };

        accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i + 3] = (cgltf_accessor) {
          .name = "animation_scale_output",
          .component_type = cgltf_component_type_r_32f,
          .normalized = 0,
          .type = cgltf_type_vec3,
          .offset = frame_count * 12 * i,
          .count = frame_count,
          .stride = 12,
          .buffer_view = &buffer_views[animation_counter * 4 + 5]
        };
      }
      animation_counter++;
    }
  }

  size_t texcoord_offset = 0;
  for (int i = 0; i < object_count; i++) {
    accessors[2 * object_count + total_animation_count * object_count * 4 + i] = (cgltf_accessor) {
      .name = "texcoord",
      .component_type = cgltf_component_type_r_32f,
      .normalized = 0,
      .type = cgltf_type_vec2,
      .offset = texcoord_offset,
      .count = texcoord_count[i],
      .stride = 8,
      .buffer_view = &buffer_views[2 + 4 * total_animation_count]
    };
    texcoord_offset += 4 * 2 * texcoord_count[i];
  }

  cgltf_image images[1];
  images[0] = (cgltf_image) {
    .name = "texture_image",
    .buffer_view = &buffer_views[3 + 4 * total_animation_count],
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
    .double_sided = 0,
    .alpha_mode = cgltf_alpha_mode_mask,
    .alpha_cutoff = 0.1
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
      .data = &accessors[2 * object_count + 4 * total_animation_count * object_count + i]
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

  cgltf_animation_sampler samplers[total_animation_count * object_count * 3];

  animation_counter = 0;
  for (size_t animation_file = 0; animation_file < animation_file_count; animation_file++) {
    animation_t* animation = &animations[animation_file];
    for (int anim = 0; anim < animation->animation_count; anim++) {
      for (int i = 0; i < object_count; i++) {
        samplers[animation_counter * object_count * 3 + i * 3] = (cgltf_animation_sampler) {
          .input = &accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i],
          .output = &accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i + 1],
          .interpolation = cgltf_interpolation_type_step
        };

        samplers[animation_counter * object_count * 3 + i * 3 + 1] = (cgltf_animation_sampler) {
          .input = &accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i],
          .output = &accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i + 2],
          .interpolation = cgltf_interpolation_type_step
        };

        samplers[animation_counter * object_count * 3 + i * 3 + 2] = (cgltf_animation_sampler) {
          .input = &accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i],
          .output = &accessors[2 * object_count + animation_counter * object_count * 4 + 4 * i + 3],
          .interpolation = cgltf_interpolation_type_step
        };
      }
      animation_counter++;
    }
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
      free(children);
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

  animation_counter = 0;
  cgltf_animation_channel channels[total_animation_count * object_count * 3];
  for (int anim = 0; anim < total_animation_count; anim++) {
    for (int i = 0; i < object_count; i++) {
      channels[animation_counter * object_count * 3 + 3 * i + 0] = (cgltf_animation_channel) {
        .sampler = &samplers[animation_counter * object_count * 3 + 3 * i + 0],
        .target_node = &nodes[i],
        .target_path = cgltf_animation_path_type_rotation
      };
      channels[animation_counter * object_count * 3 + 3 * i + 1] = (cgltf_animation_channel) {
        .sampler = &samplers[animation_counter * object_count * 3 + 3 * i + 1],
        .target_node = &nodes[i],
        .target_path = cgltf_animation_path_type_translation
      };
      channels[animation_counter * object_count * 3 + 3 * i + 2] = (cgltf_animation_channel) {
        .sampler = &samplers[animation_counter * object_count * 3 + 3 * i + 2],
        .target_node = &nodes[i],
        .target_path = cgltf_animation_path_type_scale
      };
    }
    animation_counter++;
  }

  animation_counter = 0;
  char* animation_names[total_animation_count];
  for (int anim = 0; anim < animation_file_count; anim++) {
    for (int i = 0; i < animations[anim].animation_count; i++) {
      animation_names[animation_counter] = malloc(256);
      int wrote = snprintf(
          animation_names[animation_counter],
          256,
          "%c:%c",
          animation_labels[anim],
          animations[anim].animation_labels[i]
      );
      if (wrote <= 0 || wrote >= 256) {
        die("snprintf error");
      }
      animation_counter++;
    }
  }
  cgltf_animation gltf_animations[total_animation_count];
  for (int i = 0; i < total_animation_count; i++) {
    gltf_animations[i] = (cgltf_animation) {
      .name = animation_names[i],
      .samplers = &samplers[3 * object_count * i],
      .samplers_count = 3 * object_count,
      .channels = &channels[3 * object_count * i],
      .channels_count = 3 * object_count
    };
  }

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

  data.animations = gltf_animations;
  data.animations_count = total_animation_count;

  data.accessors = accessors;
  data.accessors_count = 4 * object_count * total_animation_count + 3 * object_count;

  data.buffer_views = buffer_views;
  data.buffer_views_count = 4 * total_animation_count + 4;

  data.buffers = buffers;
  data.buffers_count = 4 * total_animation_count + 4;

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

  char extras_buf[256*256];
  int total_wrote = 0;
  int wrote = sprintf(
    extras_buf + total_wrote,
    "{ \"blink\": ["
  );
  total_wrote += wrote;
  for (int i = 0; i < blink_count; i++) {
    wrote = sprintf(
      extras_buf + total_wrote,
      "[%d, %d, %d, %d, %d, %d, %d, %d, %d, %d]",
      blinks[i].start_x,
      blinks[i].start_y,
      blinks[i].extent_x,
      blinks[i].extent_y,
      blinks[i].eye_0_x,
      blinks[i].eye_0_y,
      blinks[i].eye_1_x,
      blinks[i].eye_1_y,
      blinks[i].eye_2_x,
      blinks[i].eye_2_y
    );
    total_wrote += wrote;
    if (i + 1 < blink_count) {
      wrote = sprintf(
        extras_buf + total_wrote,
        ", "
      );
      total_wrote += wrote;
    }
  }
  wrote = sprintf(
    extras_buf + total_wrote,
    "] }"
  );
  total_wrote += wrote;

  printf("extras buf: %s\n", extras_buf);
  printf("extras bytes: %d\n", total_wrote);

  data.file_data = extras_buf;
  data.extras = (cgltf_extras) {
    .start_offset = 0,
    .end_offset = total_wrote,
  };

  size_t out_filename_len = strlen(working_dir) + strlen("/out.gltf") + 1;
  char out_filename[out_filename_len];
  memset(out_filename, 0, out_filename_len);
  strcat(out_filename, working_dir);
  strcat(out_filename, "/out.gltf");
  cgltf_options options = {0};
  cgltf_result result = cgltf_write_file(&options, out_filename, &data);
  if (result != cgltf_result_success) {
    fprintf(stderr, "Bad cgltf result: %d\n", result);
  }

  free(vertex_encoded);
  free(index_encoded);
  free(texcoord_encoded);
  free(png_encoded);

  for (int i = 0; i < object_count; i++) {
    free(nodes[i].children);
  }
}

void rip_model(iso_t* iso, char* name, size_t model_sector, size_t* animation_sectors, char* animation_labels, size_t animation_file_count) {
  struct stat st = {0};
  if (stat(name, &st) == -1) {
    mkdir(name, 0700);
  } else {
    return; // We already ripped this
  }
  model_t new_model;
  new_model = load_model(iso, model_sector);
  fprintf(stderr, "Blinks %d\n", new_model.blink_count);

  animation_t animation[animation_file_count];
  for (int i = 0; i < animation_file_count; i++) {
    animation[i] = load_animation(iso, animation_sectors[i], new_model.object_count);
  }

  for (int animation_file = 0; animation_file < animation_file_count; animation_file++) {
    fprintf(stderr, "Animation file %d\n", animation_file);
    for (int anim = 0; anim < animation[animation_file].animation_count; anim++) {
      fprintf(stderr, "Frame table:\n");
      for (int frame = 0; frame < animation[animation_file].frame_counts[anim]; frame++) {
        for (int object = 0; object < new_model.object_count; object++) {
          fprintf(stderr, "%02d ",
            animation[animation_file].frame_tables[anim][frame * new_model.object_count + object]);
        }
        fprintf(stderr, "\n");
      }
    }
  }

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
    fprintf(stderr, "loading verts (%d/%d)\n", j + 1, new_model.object_count);
    verts = load_vertices(&new_model, j, &num_read);
    fprintf(stderr, "loaded %d verts\n", num_read);
    uint32_t num_quads_read;
    uint32_t num_tris_read;
    polys_t polys;
    fprintf(stderr, "loading faces (%d/%d)\n", j + 1, new_model.object_count);
    polys = load_faces(&new_model, j, &num_quads_read, &num_tris_read);
    fprintf(stderr, "loaded %d faces\n", num_quads_read*2 + num_tris_read);
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
      uint8_t this_cmd = quads[i].cmd_upper == 0 ? 0 : 0x80;
      uint16_t pal_clut_packed =
        (this_clut << 8) |
        this_palette |
        (this_cmd << 8);
      int palette_is_new = 1;
      int pal;
      for (pal = 0; pal < exported_palettes_count; pal++) {
        if (exported_palettes[pal] == pal_clut_packed) {
          palette_is_new = 0;
          break;
        }
      }
      if (palette_is_new) {
        if (exported_palettes_count >= 32) {
          die("Too many palettes referenced in file!");
        }
        exported_palettes[exported_palettes_count++] = pal_clut_packed;
        fprintf(stderr,
          "Encountered new palette %04x (y=%02x, x=%02x, t=%02x)\n",
          pal_clut_packed,
          pal_clut_packed >> 6,
          pal_clut_packed & 0x3f,
          (pal_clut_packed >> 8) & 0x80);
      }

      int tex_page_x = pal % 8;
      int tex_page_y = pal / 8;
      float tex_offs_x = (float) tex_page_x / 8;
      float tex_offs_y = (float) tex_page_y / 4;

      // Slightly adjust the UV coordinates to make sampling of texels
      // more consistent
      float e = 0.0001;
      texcoords[12 * i +  0] = quads[i].tex_c_x / 1024.0 + tex_offs_x + e;
      texcoords[12 * i +  1] = quads[i].tex_c_y / 1024.0 + tex_offs_y + e;
      texcoords[12 * i +  2] = quads[i].tex_b_x / 1024.0 + tex_offs_x + e;
      texcoords[12 * i +  3] = quads[i].tex_b_y / 1024.0 + tex_offs_y + e;
      texcoords[12 * i +  4] = quads[i].tex_a_x / 1024.0 + tex_offs_x + e;
      texcoords[12 * i +  5] = quads[i].tex_a_y / 1024.0 + tex_offs_y + e;
      texcoords[12 * i +  6] = quads[i].tex_b_x / 1024.0 + tex_offs_x + e;
      texcoords[12 * i +  7] = quads[i].tex_b_y / 1024.0 + tex_offs_y + e;
      texcoords[12 * i +  8] = quads[i].tex_c_x / 1024.0 + tex_offs_x + e;
      texcoords[12 * i +  9] = quads[i].tex_c_y / 1024.0 + tex_offs_y + e;
      texcoords[12 * i + 10] = quads[i].tex_d_x / 1024.0 + tex_offs_x + e;
      texcoords[12 * i + 11] = quads[i].tex_d_y / 1024.0 + tex_offs_y + e;
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
      uint8_t this_cmd = tris[i].cmd_upper == 0 ? 0 : 0x80;
      uint16_t pal_clut_packed =
        (this_clut << 8) |
        this_palette |
        (this_cmd << 8);
      int palette_is_new = 1;
      int pal;
      for (pal = 0; pal < exported_palettes_count; pal++) {
        if (exported_palettes[pal] == pal_clut_packed) {
          palette_is_new = 0;
          break;
        }
      }
      if (palette_is_new) {
        if (exported_palettes_count >= 32) {
          die("Too many palettes referenced in file!");
        }
        exported_palettes[exported_palettes_count++] = pal_clut_packed;
        fprintf(stderr,
          "Encountered new palette %04x (y=%02x, x=%02x, t=%02x)\n",
          pal_clut_packed,
          pal_clut_packed >> 6,
          pal_clut_packed & 0x3f,
          (pal_clut_packed >> 8) & 0x80);
      }

      int tex_page_x = pal % 8;
      int tex_page_y = pal / 8;
      float tex_offs_x = (float) tex_page_x / 8;
      float tex_offs_y = (float) tex_page_y / 4;

      // Slightly adjust the UV coordinates to make sampling of texels
      // more consistent
      float e = 0.0001;
      texcoords[6 * i + 0 + (12 * num_quads_read)] = tris[i].tex_a_x / 1024.0 + tex_offs_x + e;
      texcoords[6 * i + 1 + (12 * num_quads_read)] = tris[i].tex_a_y / 1024.0 + tex_offs_y + e;
      texcoords[6 * i + 2 + (12 * num_quads_read)] = tris[i].tex_c_x / 1024.0 + tex_offs_x + e;
      texcoords[6 * i + 3 + (12 * num_quads_read)] = tris[i].tex_c_y / 1024.0 + tex_offs_y + e;
      texcoords[6 * i + 4 + (12 * num_quads_read)] = tris[i].tex_b_x / 1024.0 + tex_offs_x + e;
      texcoords[6 * i + 5 + (12 * num_quads_read)] = tris[i].tex_b_y / 1024.0 + tex_offs_y + e;
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
    free(polys.quads);
    free(polys.tris);
    flat_tri_table[j] = flat_tris;
    flat_tri_counts[j] = 2 * num_quads_read + num_tris_read;
    texcoord_table[j] = texcoords;
    texcoord_counts[j] = num_quads_read * 6 + num_tris_read * 3;
    texcoords_seen += num_quads_read * 4 + num_tris_read * 3;
    verts_seen += num_read;
    free(verts);
  }
  fprintf(stderr, "loading texture\n");
  paletted_texture_t tex = load_texture(&new_model);
  fprintf(stderr, "loaded texture\n");
  for (int pal = 0; pal < exported_palettes_count; pal++) {
    uint16_t clut = exported_palettes[pal];
    fprintf(stderr, "Loading the texture with %02x,%02x\n", clut & 0x3f, clut >> 6);
    size_t offset_x = 128 * (pal % 8);
    size_t offset_y = 256 * (pal / 8);
    blit_to_png_write_buffer(&tex, clut & 0x3f, clut >> 6, clut & 0x8000, offset_x, offset_y);
  }
  free(tex.texture);
  png_alloc_size_t png_alloc = save_png_write_buffer();
  make_epic_gltf_file(
    name,
    flat_vert_table,
    flat_vert_counts,
    flat_tri_table,
    flat_tri_counts,
    texcoord_table,
    texcoord_counts,
    animation,
    animation_file_count,
    animation_labels,
    new_model.node_tree,
    new_model.object_count,
    png_alloc,
    new_model.blink,
    new_model.blink_count
  );
  free(new_model.skeleton);
  free(new_model.node_tree);
  free(new_model.vertex_offsets);
  free(new_model.normal_offsets);
  free(new_model.face_offsets);

  for (int i = 0; i < new_model.object_count; i++) {
    free(flat_vert_table[i]);
    free(flat_tri_table[i]);
    free(texcoord_table[i]);
  }

  for (int i = 0; i < animation_file_count; i++) {
    free_animation(&animation[i]);
  }
}

int main(int argc, char** argv) {
  if (argc < 2) {
    die("Usage: ./rip_model.c ROM MODEL_TABLE");
  }
  FILE* fp;
  fp = fopen(argv[1], "r");
  if (!fp) {
    die("Failed to open file");
  }

  iso_t iso;
  iso_open(&iso, fp);

  FILE* model_table_fp;
  model_table_fp = fopen(argv[2], "r");
  if (!model_table_fp) {
    die("Failed to open model table file");
  }

  char* line = NULL;
  size_t len = 0;
  size_t read = 0;
  while ((read = getline(&line, &len, model_table_fp)) != -1) {
    size_t model_sector = strtoul(line+8, NULL, 16);

    char* pch;
    size_t anim_sectors[16];
    char anim_labels[16];
    size_t anim_sector_count = 0;
    pch = strtok(line+14, " :");
    while (pch != NULL) {
      fprintf(stderr, "pch: %s\n", pch);
      anim_sectors[anim_sector_count] = strtoul(pch, NULL, 16);
      fprintf(stderr, "anim_sector: %05lx\n", anim_sectors[anim_sector_count]);
      pch = strtok(NULL, " :");
      fprintf(stderr, "pch: %s\n", pch);
      anim_labels[anim_sector_count] = *pch;
      fprintf(stderr, "anim_label: %c\n", anim_labels[anim_sector_count]);
      pch = strtok(NULL, " :");
      fprintf(stderr, "pch: %s\n", pch);
      anim_sector_count++;
    }

    char name[8];
    memcpy(name, line, 7);
    name[7] = 0;
    fprintf(stderr, "File name %s sector numbers: %05lx;", name, model_sector);
    for (int i = 0; i < anim_sector_count; i++) {
      fprintf(stderr, " %05lx", anim_sectors[i]);
    }
    fprintf(stderr, "\n");
    rip_model(&iso, name, model_sector, anim_sectors, anim_labels, anim_sector_count);
    free(line);
    line = NULL;
  }
  free(line);
}
