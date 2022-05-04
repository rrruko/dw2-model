#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "iso_reader.h"

typedef struct model_s {
  iso_t* iso;
  uint32_t file_sector;
  uint32_t texture_sheet_offset;
  uint32_t object_count;
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

void die(char* message) {
  fprintf(stderr, "Fatal: %s\n", message);
  exit(1);
}

vertex_t* load_vertices(model_t* model, uint32_t object, uint32_t* num_read) {
  uint32_t vertex_offset = model->vertex_offsets[object];
  printf("Vertex offset: %x\n", vertex_offset);
  iso_seek_to_sector(model->iso, model->file_sector);
  iso_seek_forward(model->iso, vertex_offset);
  uint32_t count;
  iso_fread(model->iso, &count, sizeof(uint32_t), 1);
  printf("Count: %x\nOffset: %lx\n", count, model->iso->offset);
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
  printf("Face offset: %x\n", face_offset);
  iso_seek_to_sector(model->iso, model->file_sector);
  iso_seek_forward(model->iso, face_offset);
  iso_seek_forward(model->iso, 4);
  uint32_t count;
  iso_fread(model->iso, &count, sizeof(uint32_t), 1);
  printf("Count: %x\nOffset: %lx\n", count, model->iso->offset);
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
  return new_model;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    die("Expected a filename");
  }
  FILE* fp;
  fp = fopen(argv[1], "r");
  if (!fp) {
    die("Failed to open file");
  }

  iso_t iso;
  iso_open(&iso, fp);

  model_t new_model;
  new_model = load_model(&iso, 0x285e8);

  printf("Loaded model\n");
  printf("Model texture_sheet_offset: %x\n", new_model.texture_sheet_offset);
  printf("Model object_count: %x\n", new_model.object_count);
  for (int i = 0; i < new_model.object_count; i++) {
    printf("offsets[%d]: (%x, %x, %x)\n",
      i,
      new_model.vertex_offsets[i],
      new_model.normal_offsets[i],
      new_model.face_offsets[i]);
  }

  for (int i = 0; i < new_model.object_count; i++) {
    printf("o %d\n", i);
    uint32_t num_read;
    vertex_t* verts;
    verts = load_vertices(&new_model, i, &num_read);
    for (int i = 0; i < num_read; i++) {
      printf("(%d, %d, %d)\n", verts[i].x, verts[i].y, verts[i].z);
    }
    uint32_t num_quads_read;
    uint32_t num_tris_read;
    polys_t polys;
    polys = load_faces(&new_model, i, &num_quads_read, &num_tris_read);
    for (int i = 0; i < num_quads_read; i++) {
      face_quad_t* quads = polys.quads;
      printf("(%d, %d, %d, %d)\n",
        quads[i].vertex_a,
        quads[i].vertex_b,
        quads[i].vertex_c,
        quads[i].vertex_d);
    }
    for (int i = 0; i < num_tris_read; i++) {
      face_tri_t* tris = polys.tris;
      printf("(%d, %d, %d)\n",
        tris[i].vertex_a,
        tris[i].vertex_b,
        tris[i].vertex_c);
    }
  }
}
