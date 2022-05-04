#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct iso_s {
  FILE* fp;
  size_t offset;
  size_t current_sector;
} iso_t;

int iso_seek_to_sector(iso_t* iso, long int sector_number);
void iso_open(iso_t* iso, FILE* fp);
int iso_seek_forward(iso_t* iso, long int offset);
int iso_fread(iso_t* iso, void* buf, size_t member_size, size_t items);
void exercise_iso_seek(iso_t* iso);
