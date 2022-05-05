#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct iso_s {
  FILE* fp;
  size_t offset;
  size_t current_sector;
} iso_t;

int iso_seek_to_sector(iso_t* iso, long int sector_number) {
  // The start of a sector looks like this:
  // 00ff ffff ffff ffff ffff ff00 MMMM MMMM
  // MMMM MMMM MMMM MMMM DDDD DDDD DDDD DDDD
  // Where M is metadata and D is data
  //
  // A sector is 0x800 bytes plus 0x130 bytes of metadata.
  // The remaining 0x118 bytes are at the bottom.
  long int new_offset = sector_number * 0x930 + 24;
  int result = fseek(iso->fp, new_offset, SEEK_SET);
  iso->offset = new_offset;
  iso->current_sector = sector_number;
  return result;
}

void iso_open(iso_t* iso, FILE* fp) {
  iso->fp = fp;
  iso->offset = 24;
  iso->current_sector = 0;
}

int iso_seek_forward(iso_t* iso, long int offset) {
  long int start_of_current_sector = 24 + 0x930 * iso->current_sector;
  long int sector_progress = iso->offset - start_of_current_sector;
  // If the new seek position would dip into the metadata, we need to add 0x130.
  // If it would dip into the metadata of the next sector, we need to add 0x260.
  long int diff_sectors = (sector_progress + offset) / 0x800;
  iso->current_sector += diff_sectors;
  iso->offset += offset + 0x130 * diff_sectors;
  int result = fseek(iso->fp, iso->offset, SEEK_SET);
  return result;
}

int iso_fread(iso_t* iso, void* buf, size_t member_size, size_t items) {
  size_t bytes_to_read = member_size * items;
  long int start_of_current_sector = 24 + 0x930 * iso->current_sector;
  long int sector_progress = iso->offset - start_of_current_sector;
  long int remaining_bytes_in_sector = 0x800 - sector_progress;
  int result;
  if (remaining_bytes_in_sector >= bytes_to_read) {
    // If there's enough bytes left in this sector, we can just read normally
    result = fread(buf, member_size, items, iso->fp);
    if (result != items) {
      return -1;
    }
    iso->offset += bytes_to_read;
  } else {
    char* temp = malloc(bytes_to_read);
    // Otherwise, we need to split the calls
    result = fread(temp, remaining_bytes_in_sector, 1, iso->fp);
    if (result != 1) {
      return -1;
    }
    temp += remaining_bytes_in_sector;
    fseek(iso->fp, 0x130, SEEK_CUR);
    long int leftover = bytes_to_read - remaining_bytes_in_sector;
    long int chunks = leftover / 0x800;
    long int margin = leftover % 0x800;
    for (int i = 0; i < chunks; i++) {
      result = fread(temp, 0x800, 1, iso->fp);
      if (result != 1) {
        return -1;
      }
      temp += 0x800;
      fseek(iso->fp, 0x130, SEEK_CUR);
    }
    result = fread(temp, margin, 1, iso->fp);
    temp += margin;
    if (result != 1) {
      return -1;
    }
    iso_seek_forward(iso, bytes_to_read);
    // void* memcpy( void* dest, const void* src, std::size_t count );
    memcpy(buf, temp - bytes_to_read, bytes_to_read);
  }
  return items;
}

void exercise_iso_seek(iso_t* iso) {
  printf("iso initial offset: 0x%lx\niso initial sector: %lu\n",
    iso->offset,
    iso->current_sector);
  iso_seek_to_sector(iso, 1);
  printf("iso offset: 0x%lx\niso sector: %lu\n",
    iso->offset,
    iso->current_sector);
  iso_seek_to_sector(iso, 2);
  printf("iso offset: 0x%lx\niso sector: %lu\n",
    iso->offset,
    iso->current_sector);
  iso_seek_to_sector(iso, 0);
  iso_seek_forward(iso, 0x7fe);
  printf("iso offset: 0x%lx\niso sector: %lu\n",
    iso->offset,
    iso->current_sector);
  iso_seek_forward(iso, 0x1);
  printf("iso offset: 0x%lx\niso sector: %lu\n",
    iso->offset,
    iso->current_sector);
  iso_seek_forward(iso, 0x1);
  printf("iso offset: 0x%lx\niso sector: %lu\n",
    iso->offset,
    iso->current_sector);
  iso_seek_forward(iso, 0x1);
  printf("iso offset: 0x%lx\niso sector: %lu\n",
    iso->offset,
    iso->current_sector);
  iso_seek_to_sector(iso, 0);
  iso_seek_forward(iso, 0x7ff);
  printf("iso offset: 0x%lx\niso sector: %lu\n",
    iso->offset,
    iso->current_sector);
  iso_seek_forward(iso, 0x1001);
  printf("iso offset: 0x%lx\niso sector: %lu\n",
    iso->offset,
    iso->current_sector);
}
