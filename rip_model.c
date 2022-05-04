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
  uint32_t num_read;
  vertex_t* verts;
  verts = load_vertices(&new_model, 0, &num_read);
  for (int i = 0; i < num_read; i++) {
    printf("(%d, %d, %d)\n", verts[i].x, verts[i].y, verts[i].z);
  }
  verts = load_vertices(&new_model, 1, &num_read);
  for (int i = 0; i < num_read; i++) {
    printf("(%d, %d, %d)\n", verts[i].x, verts[i].y, verts[i].z);
  }

}
