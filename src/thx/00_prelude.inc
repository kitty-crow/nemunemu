
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "nemunemu/thx.h"
#include "nemunemu/abi.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <sched.h>
#include <spawn.h>
#if !defined(__wasm__)
#include <setjmp.h>
#endif
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

enum {
  NEMU_OK = 0,
  NEMU_USAGE = 64,
  NEMU_DATA = 65,
  NEMU_NOINPUT = 66,
  NEMU_UNAVAILABLE = 69,
  NEMU_SOFTWARE = 70,
  NEMU_OSERR = 71
};

#define NEMU_PAGE_SHIFT 16u
#define NEMU_PAGE_SIZE (1u << NEMU_PAGE_SHIFT)

/*
 * Three sparse radix levels cover the 24-bit page index of a 1-TiB
 * Thistle64 address space using 256 entries per level.
 */
#define NEMU_PAGE_RADIX_BITS 8u
#define NEMU_PAGE_RADIX_ENTRIES (1u << NEMU_PAGE_RADIX_BITS)
#define NEMU_PAGE_RADIX_MASK (NEMU_PAGE_RADIX_ENTRIES - 1u)
#define NEMU_MAX_SECTIONS 4096u
#define NEMU_MAX_JSON_DEPTH 64u
#define NEMU_MAX_PATH 4096u
#define NEMU_MAX_ARGS 4096u
#define NEMU_MAX_IO (64u * 1024u * 1024u)
#define NEMU_MAX_LOGICAL_MEMORY (1ull << 40)

typedef struct {
  char *name;
  char *flags;
  uint64_t align;
  uint64_t size;
  uint64_t addr;
  uint64_t payload_at;
  uint64_t payload_len;
} NemuSection;

typedef struct {
  int bits;
  uint64_t entry;
  uint64_t mem_size;
  char *isa;
  NemuSection *sections;
  size_t section_count;
  uint8_t *file;
  size_t file_size;
  size_t payload_base;
} NemuImage;

typedef struct {
  uint64_t size;
  size_t page_count;
  uint8_t ****roots;
} NemuMemory;

typedef struct NemuVm {
  const NemuImage *image;
  NemuMemory memory;
  uint64_t floor;
  uint64_t brk;
  uint64_t stack_at;
  uint64_t fuel;
  int done;
  int exit_code;
  char root[NEMU_MAX_PATH];
  char cwd[NEMU_MAX_PATH];
  char self[NEMU_MAX_PATH];
#if !defined(__wasm__)
  jmp_buf trap;
  int trap_ready;
#endif
  int fault_status;
  char fault[512];
} NemuVm;

typedef struct {
  const char *text;
  size_t size;
  size_t pos;
  unsigned depth;
} Json;

static void set_message(char *message, size_t message_size, const char *format, ...) {
  if (!message || message_size == 0) return;
  va_list args;
  va_start(args, format);
  (void)vsnprintf(message, message_size, format, args);
  va_end(args);
}

_Noreturn static void vm_fault(NemuVm *vm, int status, const char *format, ...) {
  vm->fault_status = status;
  va_list args;
  va_start(args, format);
  (void)vsnprintf(vm->fault, sizeof(vm->fault), format, args);
  va_end(args);
#if defined(__wasm__)
  static const char prefix[] = "nemunemu: ";
  (void)write(STDERR_FILENO, prefix, sizeof(prefix) - 1u);
  (void)write(STDERR_FILENO, vm->fault, strlen(vm->fault));
  (void)write(STDERR_FILENO, "\n", 1u);
  _Exit(status ? status : NEMU_SOFTWARE);
#else
  if (vm->trap_ready) longjmp(vm->trap, 1);
  abort();
#endif
}

static uint32_t fnv1a(const uint8_t *data, size_t size) {
  uint32_t hash = UINT32_C(0x811c9dc5);
  for (size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= UINT32_C(0x01000193);
  }
  return hash;
}

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p) {
  uint64_t out = 0;
  for (unsigned i = 0; i < 8; ++i) out |= (uint64_t)p[i] << (i * 8u);
  return out;
}

static void wr16(uint8_t *p, uint16_t n) {
  p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
}

static void wr32(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (i * 8u));
}

static void wr64(uint8_t *p, uint64_t n) {
  for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(n >> (i * 8u));
}

static bool add_overflow_u64(uint64_t a, uint64_t b, uint64_t *out) {
  *out = a + b;
  return *out < a;
}

static void json_ws(Json *j) {
  while (j->pos < j->size) {
    char c = j->text[j->pos];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
    ++j->pos;
  }
}

static bool json_take(Json *j, char expected) {
  json_ws(j);
  if (j->pos >= j->size || j->text[j->pos] != expected) return false;
  ++j->pos;
  return true;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static char *json_string(Json *j) {
  json_ws(j);
  if (j->pos >= j->size || j->text[j->pos++] != '"') return NULL;
  size_t cap = 32, len = 0;
  char *out = malloc(cap);
  if (!out) return NULL;
  while (j->pos < j->size) {
    unsigned char c = (unsigned char)j->text[j->pos++];
    if (c == '"') {
      if (len + 1 > cap) {
        char *next = realloc(out, len + 1);
        if (!next) { free(out); return NULL; }
        out = next;
      }
      out[len] = '\0';
      return out;
    }
    if (c == '\\') {
      if (j->pos >= j->size) break;
      char e = j->text[j->pos++];
      switch (e) {
        case '"': c = '"'; break;
        case '\\': c = '\\'; break;
        case '/': c = '/'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case 'u': {
          if (j->pos + 4 > j->size) { free(out); return NULL; }
          int h0 = hex_digit(j->text[j->pos]);
          int h1 = hex_digit(j->text[j->pos + 1]);
          int h2 = hex_digit(j->text[j->pos + 2]);
          int h3 = hex_digit(j->text[j->pos + 3]);
          if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) { free(out); return NULL; }
          unsigned code = (unsigned)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
          j->pos += 4;
          if (code < 0x80) {
            c = (unsigned char)code;
          } else {
            /* Metadata keys and paths are UTF-8 already; escaped non-ASCII is rejected. */
            free(out);
            return NULL;
          }
          break;
        }
        default: free(out); return NULL;
      }
    } else if (c < 0x20) {
      free(out);
      return NULL;
    }
    if (len + 2 > cap) {
      cap *= 2;
      char *next = realloc(out, cap);
      if (!next) { free(out); return NULL; }
      out = next;
    }
    out[len++] = (char)c;
  }
  free(out);
  return NULL;
}

static bool json_u64(Json *j, uint64_t *value) {
  json_ws(j);
  if (j->pos >= j->size || j->text[j->pos] < '0' || j->text[j->pos] > '9') return false;
  uint64_t n = 0;
  while (j->pos < j->size) {
    unsigned d = (unsigned)(j->text[j->pos] - '0');
    if (d > 9) break;
    if (n > (UINT64_MAX - d) / 10u) return false;
    n = n * 10u + d;
    ++j->pos;
  }
  *value = n;
  return true;
}

static bool json_skip_value(Json *j);

static bool json_skip_array(Json *j) {
  if (++j->depth > NEMU_MAX_JSON_DEPTH || !json_take(j, '[')) return false;
  json_ws(j);
  if (json_take(j, ']')) { --j->depth; return true; }
  for (;;) {
    if (!json_skip_value(j)) return false;
    if (json_take(j, ']')) { --j->depth; return true; }
    if (!json_take(j, ',')) return false;
  }
}

static bool json_skip_object(Json *j) {
  if (++j->depth > NEMU_MAX_JSON_DEPTH || !json_take(j, '{')) return false;
  json_ws(j);
  if (json_take(j, '}')) { --j->depth; return true; }
  for (;;) {
    char *key = json_string(j);
    if (!key) return false;
    free(key);
    if (!json_take(j, ':') || !json_skip_value(j)) return false;
    if (json_take(j, '}')) { --j->depth; return true; }
    if (!json_take(j, ',')) return false;
  }
}

static bool json_skip_value(Json *j) {
  json_ws(j);
  if (j->pos >= j->size) return false;
  char c = j->text[j->pos];
  if (c == '"') {
    char *s = json_string(j);
    if (!s) return false;
    free(s);
    return true;
  }
  if (c == '{') return json_skip_object(j);
  if (c == '[') return json_skip_array(j);
  if (c >= '0' && c <= '9') {
    uint64_t ignored;
    return json_u64(j, &ignored);
  }
  static const char *const literals[] = {"true", "false", "null"};
  for (size_t i = 0; i < sizeof(literals) / sizeof(literals[0]); ++i) {
    size_t n = strlen(literals[i]);
    if (j->pos + n <= j->size && memcmp(j->text + j->pos, literals[i], n) == 0) {
      j->pos += n;
      return true;
    }
  }
  return false;
}

