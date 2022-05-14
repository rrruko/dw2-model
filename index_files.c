#include <string.h>
#include "iso_reader.h"

int match(unsigned char* searched, size_t searched_buffer_size, size_t searched_offset, unsigned char* goal, size_t bytes, size_t offset) {
  /*
  if (searched_offset + offset + bytes >= searched_buffer_size) {
    fprintf(stderr, "Called match with insufficiently large buffer\n");
    exit(1);
  }
  */
  for (int i = 0; i < bytes; i++) {
    size_t searched_ix = (searched_offset + offset + i) % searched_buffer_size;
    if (searched[searched_ix] != goal[i]) {
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
  uint8_t ring_buf[1024];
  size_t page_number = 0;

  uint8_t search_for[4] = ".BIN";

  // Earliest sector containing the model files
  iso_seek_to_sector(&iso, 0x254ac);
  page_number = (0x254ac * 0x800) / 1024;

  int eof = 0;
  items_read = iso_fread(&iso, ring_buf, 512, 1);
  if (items_read < 0) {
    fprintf(stderr, "eof\n");
    eof = 1;
  }
  while (!eof) {
    for (int i = 0; i < 1024; i++) {
      if (i == 0) {
        items_read = iso_fread(&iso, ring_buf + 512, 512, 1);
        if (items_read < 0) {
          fprintf(stderr, "eof\n");
          eof = 1;
        }
      }
      if (i == 512) {
        items_read = iso_fread(&iso, ring_buf, 512, 1);
        if (items_read < 0) {
          fprintf(stderr, "eof\n");
          eof = 1;
        }
      }
      if (eof) break;
      if (match(ring_buf, 1024, i, search_for, 4, 35)) {
        size_t bytes_read = 1024 * page_number + i;
        size_t location = bytes_read + 0x130*(bytes_read/0x800) + 24;
        fprintf(stderr, "Found match at 0x%lx\n", location);
        uint32_t offset = 0;
        offset |= ring_buf[i % 1024] << 24;
        offset |= ring_buf[(i+1) % 1024] << 16;
        offset |= ring_buf[(i+2) % 1024] << 8;
        offset |= ring_buf[(i+3) % 1024];
        char name[13];
        for (int j = 0; j < 13; j++) {
          name[j] = ring_buf[(i+j+27)%1024];
        }
        name[12] = 0;
        fprintf(stdout, "* %x %s\n",
          offset,
          name);

        for (int repeat = 0; repeat < 8; repeat++) {
          for (int ch = 0; ch < 8; ch++) {
            uint16_t word = 0;
            word |= ring_buf[(i + 2 * ch + 16 * repeat) % 1024];
            word |= ring_buf[(i + 2 * ch + 16 * repeat + 1) % 1024] << 8;
            fprintf(stderr, "%02x%02x ", word & 0xff, word >> 8);
          }
          fprintf(stderr, "   ");
          for (int ch = 0; ch < 8; ch++) {
            uint8_t lo = ring_buf[(i + 2 * ch + 16 * repeat) % 1024];
            uint8_t hi = ring_buf[(i + 2 * ch + 16 * repeat + 1) % 1024];
            fprintf(stderr, "%c%c",
              show[lo],
              show[hi]);
          }
          fprintf(stderr, "\n");
        }
      }
    }
    page_number++;
  }
}
