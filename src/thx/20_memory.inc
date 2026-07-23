static int memory_init(NemuMemory *memory, uint64_t size) {
  memset(memory, 0, sizeof(*memory));
  memory->size = size;
  uint64_t pages = (size + NEMU_PAGE_SIZE - 1u) >> NEMU_PAGE_SHIFT;
  if (pages > SIZE_MAX / sizeof(*memory->pages)) return -1;
  memory->page_count = (size_t)pages;
  memory->pages = calloc(memory->page_count, sizeof(*memory->pages));
  return memory->pages ? 0 : -1;
}

static void memory_free(NemuMemory *memory) {
  if (!memory) return;
  for (size_t i = 0; i < memory->page_count; ++i) free(memory->pages[i]);
  free(memory->pages);
  memset(memory, 0, sizeof(*memory));
}

static uint8_t *memory_page(NemuVm *vm, uint64_t addr, bool create) {
  size_t index = (size_t)(addr >> NEMU_PAGE_SHIFT);
  if (index >= vm->memory.page_count) vm_fault(vm, NEMU_DATA, "memory address outside image");
  if (!vm->memory.pages[index] && create) {
    vm->memory.pages[index] = calloc(1, NEMU_PAGE_SIZE);
    if (!vm->memory.pages[index]) vm_fault(vm, NEMU_OSERR, "out of host memory");
  }
  return vm->memory.pages[index];
}

static bool section_contains(const NemuSection *s, uint64_t addr, size_t size) {
  uint64_t end;
  return !add_overflow_u64(addr, size, &end) &&
         addr >= s->addr && end <= s->addr + s->size;
}

static const NemuSection *mapped_section(const NemuVm *vm, uint64_t addr, size_t size) {
  for (size_t i = 0; i < vm->image->section_count; ++i) {
    if (section_contains(&vm->image->sections[i], addr, size)) {
      return &vm->image->sections[i];
    }
  }
  return NULL;
}

static void vm_check_memory(NemuVm *vm, uint64_t addr, size_t size, bool write) {
  uint64_t end;
  if (add_overflow_u64(addr, size, &end) || end > vm->memory.size) {
    vm_fault(vm, NEMU_DATA, "memory access at 0x%" PRIx64, addr);
  }
  if (size == 0) return;
  const NemuSection *s = mapped_section(vm, addr, size);
  bool heap = addr >= vm->floor && end <= vm->brk;
  bool stack = addr >= vm->stack_at && end <= vm->memory.size;
  if (!s && !heap && !stack) {
    vm_fault(vm, NEMU_DATA, "unmapped memory access at 0x%" PRIx64, addr);
  }
  if (write && s && !strchr(s->flags, 'w')) {
    vm_fault(vm, NEMU_DATA, "write to read-only section %s", s->name);
  }
}

static void vm_check_exec(NemuVm *vm, uint64_t addr, size_t size) {
  const NemuSection *s = mapped_section(vm, addr, size);
  if (!s || !strchr(s->flags, 'x')) {
    vm_fault(vm, NEMU_DATA, "execute at 0x%" PRIx64, addr);
  }
}

static void memory_write_raw(NemuVm *vm, uint64_t addr, const uint8_t *data, size_t size) {
  while (size) {
    size_t off = (size_t)(addr & (NEMU_PAGE_SIZE - 1u));
    size_t take = NEMU_PAGE_SIZE - off;
    if (take > size) take = size;
    uint8_t *page = memory_page(vm, addr, true);
    memcpy(page + off, data, take);
    data += take;
    addr += take;
    size -= take;
  }
}

static void memory_read_raw(NemuVm *vm, uint64_t addr, uint8_t *data, size_t size) {
  while (size) {
    size_t off = (size_t)(addr & (NEMU_PAGE_SIZE - 1u));
    size_t take = NEMU_PAGE_SIZE - off;
    if (take > size) take = size;
    uint8_t *page = memory_page(vm, addr, false);
    if (page) memcpy(data, page + off, take);
    else memset(data, 0, take);
    data += take;
    addr += take;
    size -= take;
  }
}

static void vm_write(NemuVm *vm, uint64_t addr, const void *data, size_t size) {
  vm_check_memory(vm, addr, size, true);
  memory_write_raw(vm, addr, data, size);
}

static void vm_read(NemuVm *vm, uint64_t addr, void *data, size_t size) {
  vm_check_memory(vm, addr, size, false);
  memory_read_raw(vm, addr, data, size);
}

static uint8_t vm_u8(NemuVm *vm, uint64_t addr) {
  uint8_t b; vm_read(vm, addr, &b, 1); return b;
}

static uint16_t vm_u16(NemuVm *vm, uint64_t addr) {
  uint8_t b[2]; vm_read(vm, addr, b, 2);
  return (uint16_t)b[0] | (uint16_t)((uint16_t)b[1] << 8);
}

static uint32_t vm_u32(NemuVm *vm, uint64_t addr) {
  uint8_t b[4]; vm_read(vm, addr, b, 4); return rd32(b);
}

static uint64_t vm_u64(NemuVm *vm, uint64_t addr) {
  uint8_t b[8]; vm_read(vm, addr, b, 8); return rd64(b);
}

static void vm_set8(NemuVm *vm, uint64_t addr, uint8_t value) {
  vm_write(vm, addr, &value, 1);
}

static void vm_set16(NemuVm *vm, uint64_t addr, uint16_t value) {
  uint8_t b[2]; wr16(b, value); vm_write(vm, addr, b, 2);
}

static void vm_set32(NemuVm *vm, uint64_t addr, uint32_t value) {
  uint8_t b[4]; wr32(b, value); vm_write(vm, addr, b, 4);
}

static void vm_set64(NemuVm *vm, uint64_t addr, uint64_t value) {
  uint8_t b[8]; wr64(b, value); vm_write(vm, addr, b, 8);
}

static char *vm_string(NemuVm *vm, uint64_t addr) {
  size_t cap = 64, len = 0;
  char *out = malloc(cap);
  if (!out) vm_fault(vm, NEMU_OSERR, "out of host memory");
  for (;;) {
    if (len >= 65536u) {
      free(out);
      vm_fault(vm, NEMU_DATA, "guest string is too long");
    }
    uint8_t c = vm_u8(vm, addr + len);
    if (!c) break;
    if (len + 2 > cap) {
      cap *= 2;
      char *next = realloc(out, cap);
      if (!next) { free(out); vm_fault(vm, NEMU_OSERR, "out of host memory"); }
      out = next;
    }
    out[len++] = (char)c;
  }
  out[len] = '\0';
  return out;
}

static int guest_path(NemuVm *vm, const char *path, char *out, size_t out_size) {
  char guest[NEMU_MAX_PATH];
  if (!path || !*path) { errno = ENOENT; return -1; }
  if (path[0] == '/') {
    if (snprintf(guest, sizeof(guest), "%s", path) >= (int)sizeof(guest)) {
      errno = ENAMETOOLONG; return -1;
    }
  } else {
    const char *slash = strcmp(vm->cwd, "/") == 0 ? "" : "/";
    if (snprintf(guest, sizeof(guest), "%s%s%s", vm->cwd, slash, path) >=
        (int)sizeof(guest)) {
      errno = ENAMETOOLONG; return -1;
    }
  }

  char normal[NEMU_MAX_PATH];
  size_t used = 0;
  normal[used++] = '/';
  normal[used] = '\0';
  const char *p = guest;
  while (*p) {
    while (*p == '/') ++p;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != '/') ++p;
    size_t n = (size_t)(p - start);
    if (n == 1 && start[0] == '.') continue;
    if (n == 2 && start[0] == '.' && start[1] == '.') {
      if (used > 1) {
        --used;
        while (used > 1 && normal[used - 1] != '/') --used;
        normal[used] = '\0';
      }
      continue;
    }
    if (used > 1) normal[used++] = '/';
    if (used + n >= sizeof(normal)) { errno = ENAMETOOLONG; return -1; }
    memcpy(normal + used, start, n);
    used += n;
    normal[used] = '\0';
  }

  if (strcmp(vm->root, "/") == 0) {
    if (snprintf(out, out_size, "%s", normal) >= (int)out_size) {
      errno = ENAMETOOLONG; return -1;
    }
  } else {
    if (snprintf(out, out_size, "%s%s", vm->root, normal) >= (int)out_size) {
      errno = ENAMETOOLONG; return -1;
    }
  }
  return 0;
}

static int thistle_errno(int e) {
  switch (e) {
    case EACCES: return 2;
    case EAGAIN: return 6;
    case EBADF: return 8;
    case EBUSY: return 10;
    case ECHILD: return 12;
    case EEXIST: return 20;
    case EFAULT: return 21;
    case EFBIG: return 22;
    case EINTR: return 27;
    case EINVAL: return 28;
    case EIO: return 29;
    case EISDIR: return 31;
    case ELOOP: return 32;
    case EMFILE: return 33;
    case ENAMETOOLONG: return 37;
    case ENFILE: return 41;
    case ENOENT: return 44;
    case ENOEXEC: return 45;
#ifdef ENETUNREACH
    case ENETUNREACH: return 46;
#endif
    case ENOMEM: return 48;
    case ENOSPC: return 51;
    case ENOSYS: return 52;
    case ENOTDIR: return 54;
    case ENOTEMPTY: return 55;
#ifdef ENOTSUP
    case ENOTSUP: return 58;
#endif
    case EPERM: return 63;
    case EPIPE: return 64;
#ifdef EPROTO
    case EPROTO: return 65;
#endif
    case ERANGE: return 68;
    case EROFS: return 69;
    case ESRCH: return 71;
    case ETIMEDOUT: return 73;
    default: return 29;
  }
}

static int host_open_flags(uint64_t flags) {
  int out = 0;
  switch (flags & 3u) {
    case 0: out |= O_RDONLY; break;
    case 1: out |= O_WRONLY; break;
    case 2: out |= O_RDWR; break;
    default: errno = EINVAL; return -1;
  }
  if (flags & 0x40u) out |= O_CREAT;
  if (flags & 0x80u) out |= O_EXCL;
  if (flags & 0x200u) out |= O_TRUNC;
  if (flags & 0x400u) out |= O_APPEND;
  return out;
}

static uint64_t ms_time(const struct timespec *ts) {
  return (uint64_t)ts->tv_sec * 1000u + (uint64_t)ts->tv_nsec / 1000000u;
}

static int random_bytes(uint8_t *buffer, size_t size) {
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) return -1;
  size_t done = 0;
  while (done < size) {
    ssize_t n = read(fd, buffer + done, size - done);
    if (n < 0) {
      if (errno == EINTR) continue;
      close(fd);
      return -1;
    }
    if (n == 0) { errno = EIO; close(fd); return -1; }
    done += (size_t)n;
  }
  close(fd);
  return 0;
}

