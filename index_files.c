#include <string.h>
#include "iso_reader.h"

int match(unsigned char* searched, unsigned char* goal, size_t bytes) {
  for (int i = 0; i < bytes; i++) {
    if (searched[i] != goal[i]) {
      return 0;
    }
  }
  return 1;
}

char show[256] = {
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  ' ', '!', '"', '#', '$', '%', '&', '\'', 
  '(', ')', '*', '+', ',', '-', '.', '/', 
  '0', '1', '2', '3', '4', '5', '6', '7', 
  '8', '9', ':', ';', '<', '=', '>', '?', 
  '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 
  'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 
  'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 
  'X', 'Y', 'Z', '[', '\\', ']', '^', '_',
  '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 
  'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 
  'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 
  'x', 'y', 'z', '{', '|', '}', '~', '.',
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
  '.', '.', '.', '.', '.', '.', '.', '.', 
};

void main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: ./index_files INFILE");
    exit(1);
  }
  FILE* fp;
  fp = fopen(argv[1], "r");
  if (!fp) {
    fprintf(stderr, "Failed to open file");
    exit(1);
  }

  iso_t iso;
  iso_open(&iso, fp);
  iso_seek_to_sector(&iso, 0);

  int items_read;
  uint8_t buf[1024];
  size_t page_number = 0;

  int eof = 0;
  while (!eof) {
    items_read = iso_fread(&iso, buf, 1024, 1);
    if (items_read < 0) {
      fprintf(stderr, "eof\n");
      eof = 1;
    } else {
      for (int i = 0; i < 1020; i++) {
        if (match(&buf[i], ".BIN", 4)) {
          size_t bytes_read = 1024 * page_number + i;
          size_t location = bytes_read + 0x130*(bytes_read/0x800) + 24;
          fprintf(stderr, "Found match at 0x%lx\n", location);
          for (int repeat = 0; repeat < 8; repeat++) {
            for (int ch = 0; ch < 8; ch++) {
              uint16_t word;
              memcpy(&word, &buf[i + 2 * ch + 16 * repeat], 2);
              fprintf(stderr, "%02x%02x ", word & 0xff, word >> 8);

            }
            fprintf(stderr, "   ");
            for (int ch = 0; ch < 8; ch++) {
              fprintf(stderr, "%c%c",
                show[buf[i + 2 * ch + 16 * repeat]],
                show[buf[i + 2 * ch + 16 * repeat + 1]]);
            }
            fprintf(stderr, "\n");
          }
        }
      }
    }
    page_number++;
  }
}
