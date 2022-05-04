#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct model_s {
  uint32_t texture_sheet_offset;
  uint16_t object_count;
  uint32_t* vertex_offsets;
  uint32_t* normal_offsets;
  uint32_t* face_offsets;
} model_t;

void die(char* message) {
  fprintf(stderr, "Fatal: %s\n", message);
  exit(1);
}

model_t load_model(FILE* fp, uint32_t file_offset) {
  model_t new_model;
  fseek(fp, file_offset, SEEK_SET); 
  size_t items_read;
  items_read = fread(&new_model.texture_sheet_offset, sizeof(uint32_t), 1, fp);
  if (items_read != 1) {
    die("fread failure, an error occured or EOF");
  }
  return new_model;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    die("Expected a filename");
  }
  FILE* fp;
  fp = fopen(argv[1], "r");
  model_t new_model;
  new_model = load_model(fp, 0x172e4398);

  printf("Loaded model\n");
  printf("Model texture_sheet_offset: %x", new_model.texture_sheet_offset);
}
