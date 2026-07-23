      return n < 0 ? -thistle_errno(errno) : n;
    }

    case SC_TRUNCATE: {
      char *guest = vm_string(vm, a0);
      rc = guest_path(vm, guest, path_a, sizeof(path_a));
      free(guest);
      if (rc < 0) return -thistle_errno(errno);
      return truncate(path_a, (off_t)a1) < 0 ? -thistle_errno(errno) : 0;
    }

    default:
      errno = ENOSYS;
      return -thistle_errno(errno);
  }
}

static uint64_t stack_put(NemuVm *vm, uint64_t *sp, const char *text) {
  size_t n = strlen(text) + 1u;
  if (*sp < n || *sp - n < vm->stack_at) vm_fault(vm, NEMU_DATA, "argument block exceeds stack");
  *sp -= n;
  vm_write(vm, *sp, text, n);
  return *sp;
}

static size_t env_count(void) {
  size_t n = 0;
  while (environ && environ[n]) ++n;
  return n;
}

static int vm_prepare(NemuVm *vm, const NemuImage *image, const nemu_run_options *options) {
  memset(vm, 0, sizeof(*vm));
  vm->image = image;
  vm->fuel = 100000000ull;
  const char *fuel = getenv("NEMUNEMU_FUEL");
  if (fuel && *fuel) {
    char *end = NULL;
    unsigned long long n = strtoull(fuel, &end, 10);
    if (end && *end == '\0') vm->fuel = n;
  }
  if (memory_init(&vm->memory, image->mem_size) < 0) return NEMU_OSERR;
  uint64_t highest = 0;
  for (size_t i = 0; i < image->section_count; ++i) {
    const NemuSection *s = &image->sections[i];
    uint64_t end = s->addr + s->size;
    if (end > highest) highest = end;
    if (s->payload_len) {
      memory_write_raw(vm, s->addr,
        image->file + image->payload_base + (size_t)s->payload_at,
        (size_t)s->payload_len);
    }
  }
  vm->floor = (highest + 15u) & ~UINT64_C(15);
  vm->brk = vm->floor;
  uint64_t stack_size;
  if (image->bits == 64) {
    stack_size = image->mem_size / 8u;
    if (stack_size < 1024u * 1024u) stack_size = 1024u * 1024u;
    if (stack_size > 16u * 1024u * 1024u) stack_size = 16u * 1024u * 1024u;
  } else {
    stack_size = image->mem_size / 4u;
    if (stack_size < 64u * 1024u) stack_size = 64u * 1024u;
    if (stack_size > 4u * 1024u * 1024u) stack_size = 4u * 1024u * 1024u;
  }
  vm->stack_at = image->mem_size - stack_size;
  if (vm->stack_at <= vm->brk + (image->bits == 64 ? 1024u * 1024u : 65536u)) {
    memory_free(&vm->memory);
    return NEMU_DATA;
  }
  if (!realpath(options->root_path ? options->root_path : "/", vm->root)) {
    memory_free(&vm->memory);
    return NEMU_NOINPUT;
  }
  (void)snprintf(vm->cwd, sizeof(vm->cwd), "/");
#if defined(__linux__)
  ssize_t self_n = readlink("/proc/self/exe", vm->self, sizeof(vm->self) - 1u);
  if (self_n > 0) vm->self[self_n] = '\0';
  else
#endif
  {
    const char *fallback = getpid() == 1 ? "/sbin/nemunemu" : "nemunemu";
    (void)snprintf(vm->self, sizeof(vm->self), "%s", fallback);
  }
  return NEMU_OK;
}

