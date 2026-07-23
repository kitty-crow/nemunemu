      if (a0 < vm->floor || a0 + (bits == 64 ? 1024u * 1024u : 65536u) >= vm->stack_at) {
        errno = ENOMEM; return -thistle_errno(errno);
      }
      vm->brk = a0;
      return (int64_t)a0;

    case SC_CLOCK: {
      struct timespec ts;
      if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -thistle_errno(errno);
      return (int64_t)ms_time(&ts);
    }

    case SC_YIELD:
      return sched_yield() < 0 ? -thistle_errno(errno) : 0;

    case SC_STAT: {
      char *guest = vm_string(vm, a0);
      rc = guest_path(vm, guest, path_a, sizeof(path_a));
      free(guest);
      if (rc < 0) return -thistle_errno(errno);
      struct stat st;
      if (lstat(path_a, &st) < 0) return -thistle_errno(errno);
      int kind = S_ISREG(st.st_mode) ? 1 : S_ISDIR(st.st_mode) ? 2 :
                 S_ISLNK(st.st_mode) ? 3 : 4;
      if (bits == 32) {
        uint8_t b[56] = {0};
        wr32(b + 0, (uint32_t)st.st_ino);
        wr32(b + 4, (uint32_t)st.st_mode);
        wr32(b + 8, (uint32_t)st.st_uid);
        wr32(b + 12, (uint32_t)st.st_gid);
        wr32(b + 16, (uint32_t)st.st_nlink);
        wr32(b + 20, (uint32_t)st.st_size);
        wr32(b + 24, (uint32_t)kind);
        wr64(b + 32, ms_time(&st.st_atim));
        wr64(b + 40, ms_time(&st.st_mtim));
        wr64(b + 48, ms_time(&st.st_ctim));
        vm_write(vm, a1, b, sizeof(b));
      } else {
        uint8_t b[64] = {0};
        wr64(b + 0, (uint64_t)st.st_ino);
        wr32(b + 8, (uint32_t)st.st_mode);
        wr32(b + 12, (uint32_t)kind);
        wr32(b + 16, (uint32_t)st.st_uid);
        wr32(b + 20, (uint32_t)st.st_gid);
        wr64(b + 24, (uint64_t)st.st_nlink);
        wr64(b + 32, (uint64_t)st.st_size);
        wr64(b + 40, ms_time(&st.st_atim));
        wr64(b + 48, ms_time(&st.st_mtim));
        wr64(b + 56, ms_time(&st.st_ctim));
        vm_write(vm, a1, b, sizeof(b));
      }
      return 0;
    }

    case SC_GETCWD: {
      size_t n = strlen(vm->cwd) + 1u;
      if (n > a1) { errno = ERANGE; return -thistle_errno(errno); }
      vm_write(vm, a0, vm->cwd, n);
      return (int64_t)n;
    }

    case SC_CHDIR: {
      char *guest = vm_string(vm, a0);
      if (guest_path(vm, guest, path_a, sizeof(path_a)) < 0) {
        free(guest); return -thistle_errno(errno);
      }
      struct stat st;
      if (stat(path_a, &st) < 0 || !S_ISDIR(st.st_mode)) {
        if (errno == 0) errno = ENOTDIR;
        free(guest); return -thistle_errno(errno);
      }
      char host[NEMU_MAX_PATH];
      if (!realpath(path_a, host)) { free(guest); return -thistle_errno(errno); }
