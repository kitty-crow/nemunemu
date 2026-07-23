
#define _POSIX_C_SOURCE 200809L

#include "nemunemu/thx.h"

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { OP_HALT = 1, OP_LI = 3, OP64_HALT = 1, OP64_LI = 3, OP64_SYS = 61 };
enum { SC_WRITE = 2 };

static uint32_t hash(const uint8_t *data, size_t size) {
  uint32_t n = 0x811c9dc5u;
  for (size_t i = 0; i < size; ++i) {
    n ^= data[i];
    n *= 0x01000193u;
  }
  return n;
}

static void wr32(uint8_t *p, uint32_t n) {
  p[0] = (uint8_t)n;
  p[1] = (uint8_t)(n >> 8);
  p[2] = (uint8_t)(n >> 16);
  p[3] = (uint8_t)(n >> 24);
}

static void wr64(uint8_t *p, uint64_t n) {
  for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(n >> (i * 8u));
}

static char *temp_path(const char *tag) {
  char *path = malloc(128);
  assert(path);
  snprintf(path, 128, "/tmp/nemunemu-%s-XXXXXX", tag);
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);
  return path;
}

static void write_all(const char *path, const uint8_t *data, size_t size) {
  FILE *file = fopen(path, "wb");
  assert(file);
  assert(fwrite(data, 1, size, file) == size);
  assert(fclose(file) == 0);
}

static void make_thx32(const char *path, int code) {
  uint8_t text[16] = {0};
  text[0] = OP_LI;
  wr32(text + 4, (uint32_t)code);
  text[8] = OP_HALT;
  char meta[512];
  int hn = snprintf(meta, sizeof(meta),
    "{\"machine\":\"thistle32\",\"ver\":1,\"sec\":[{\"name\":\".text\","
