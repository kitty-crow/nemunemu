static int64_t syscall_common(
  NemuVm *vm, int bits, uint64_t number, const uint64_t args[6]
) {
  uint64_t a0 = args[0], a1 = args[1], a2 = args[2];
  char path_a[NEMU_MAX_PATH], path_b[NEMU_MAX_PATH];
  int rc;

  switch (number) {
    case SC_EXIT:
      vm->exit_code = (int)(a0 & 0xffu);
      vm->done = 1;
      return 0;

    case SC_READ: {
      if (a2 > NEMU_MAX_IO) { errno = EFBIG; return -thistle_errno(errno); }
      uint8_t *buffer = malloc((size_t)a2 ? (size_t)a2 : 1u);
      if (!buffer) { errno = ENOMEM; return -thistle_errno(errno); }
      ssize_t n;
      do { n = read((int)a0, buffer, (size_t)a2); } while (n < 0 && errno == EINTR);
      if (n >= 0) vm_write(vm, a1, buffer, (size_t)n);
      free(buffer);
      return n < 0 ? -thistle_errno(errno) : n;
    }

    case SC_WRITE: {
      if (a2 > NEMU_MAX_IO) { errno = EFBIG; return -thistle_errno(errno); }
      uint8_t *buffer = malloc((size_t)a2 ? (size_t)a2 : 1u);
      if (!buffer) { errno = ENOMEM; return -thistle_errno(errno); }
      vm_read(vm, a1, buffer, (size_t)a2);
      ssize_t n;
      do { n = write((int)a0, buffer, (size_t)a2); } while (n < 0 && errno == EINTR);
      free(buffer);
      return n < 0 ? -thistle_errno(errno) : n;
    }

    case SC_OPEN: {
      char *guest = vm_string(vm, a0);
      rc = guest_path(vm, guest, path_a, sizeof(path_a));
      free(guest);
      if (rc < 0) return -thistle_errno(errno);
      int flags = host_open_flags(a1);
      if (flags < 0) return -thistle_errno(errno);
      int fd = open(path_a, flags, (mode_t)a2);
      return fd < 0 ? -thistle_errno(errno) : fd;
    }

    case SC_CLOSE:
      return close((int)a0) < 0 ? -thistle_errno(errno) : 0;

    case SC_SEEK: {
      off_t at = lseek((int)a0, (off_t)(int64_t)a1, (int)a2);
      return at == (off_t)-1 ? -thistle_errno(errno) : (int64_t)at;
    }

    case SC_UNLINK: {
      char *guest = vm_string(vm, a0);
      rc = guest_path(vm, guest, path_a, sizeof(path_a));
      free(guest);
      if (rc < 0) return -thistle_errno(errno);
      return unlink(path_a) < 0 ? -thistle_errno(errno) : 0;
    }

    case SC_MKDIR: {
      char *guest = vm_string(vm, a0);
      rc = guest_path(vm, guest, path_a, sizeof(path_a));
      free(guest);
      if (rc < 0) return -thistle_errno(errno);
      return mkdir(path_a, (mode_t)a1) < 0 ? -thistle_errno(errno) : 0;
    }

    case SC_GETPID: return getpid();
    case SC_GETPPID: return getppid();

    case SC_BRK:
      if (a0 == 0) return (int64_t)vm->brk;
