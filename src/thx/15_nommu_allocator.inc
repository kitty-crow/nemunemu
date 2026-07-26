#if defined(NEMU_NOMMU)

#include <stddef.h>

#ifndef NEMU_NOMMU_ARENA_BYTES
#define NEMU_NOMMU_ARENA_BYTES (64u * 1024u * 1024u)
#endif

typedef union {
  max_align_t alignment;
  unsigned char bytes[NEMU_NOMMU_ARENA_BYTES];
} NemuNommuArena;

typedef struct {
  size_t size;
} NemuNommuAllocation;

static NemuNommuArena nemu_nommu_arena;
static size_t nemu_nommu_arena_used;

static size_t nemu_nommu_align(size_t value) {
  const size_t alignment = _Alignof(max_align_t);
  const size_t remainder = value % alignment;
  if (remainder == 0) return value;
  if (value > SIZE_MAX - (alignment - remainder)) return SIZE_MAX;
  return value + alignment - remainder;
}

static void *nemu_nommu_malloc(size_t size) {
  if (size == 0) size = 1;

  const size_t header_size = nemu_nommu_align(sizeof(NemuNommuAllocation));
  const size_t payload_size = nemu_nommu_align(size);

  if (header_size == SIZE_MAX || payload_size == SIZE_MAX) return NULL;
  if (header_size > SIZE_MAX - payload_size) return NULL;

  const size_t total = header_size + payload_size;
  if (nemu_nommu_arena_used > NEMU_NOMMU_ARENA_BYTES - total) return NULL;

  unsigned char *base = nemu_nommu_arena.bytes + nemu_nommu_arena_used;
  NemuNommuAllocation *allocation = (NemuNommuAllocation *)base;
  allocation->size = size;

  void *result = base + header_size;
  nemu_nommu_arena_used += total;
  return result;
}

static void *nemu_nommu_calloc(size_t count, size_t size) {
  if (count != 0 && size > SIZE_MAX / count) return NULL;

  const size_t bytes = count * size;
  void *result = nemu_nommu_malloc(bytes);
  if (result) memset(result, 0, bytes);
  return result;
}

static void *nemu_nommu_realloc(void *pointer, size_t size) {
  if (!pointer) return nemu_nommu_malloc(size);
  if (size == 0) return NULL;

  const size_t header_size = nemu_nommu_align(sizeof(NemuNommuAllocation));
  unsigned char *payload = (unsigned char *)pointer;
  NemuNommuAllocation *allocation =
    (NemuNommuAllocation *)(payload - header_size);

  void *replacement = nemu_nommu_malloc(size);
  if (!replacement) return NULL;

  const size_t copy = allocation->size < size ? allocation->size : size;
  memcpy(replacement, pointer, copy);
  return replacement;
}

static void nemu_nommu_free(void *pointer) {
  (void)pointer;
}

#define malloc(size) nemu_nommu_malloc(size)
#define calloc(count, size) nemu_nommu_calloc((count), (size))
#define realloc(pointer, size) nemu_nommu_realloc((pointer), (size))
#define free(pointer) nemu_nommu_free(pointer)

#endif
