      size_t root_len = strcmp(vm->root, "/") == 0 ? 0u : strlen(vm->root);
      const char *relative = host + root_len;
      if (!*relative) relative = "/";
      if (snprintf(vm->cwd, sizeof(vm->cwd), "%s", relative) >= (int)sizeof(vm->cwd)) {
        free(guest); errno = ENAMETOOLONG; return -thistle_errno(errno);
      }
      free(guest);
      return 0;
    }

    case SC_RMDIR: {
      char *guest = vm_string(vm, a0);
      rc = guest_path(vm, guest, path_a, sizeof(path_a));
      free(guest);
      if (rc < 0) return -thistle_errno(errno);
      return rmdir(path_a) < 0 ? -thistle_errno(errno) : 0;
    }

    case SC_RENAME:
    case SC_LINK:
    case SC_SYMLINK: {
      char *ga = vm_string(vm, a0);
      char *gb = vm_string(vm, a1);
      rc = guest_path(vm, ga, path_a, sizeof(path_a));
      if (rc == 0) rc = guest_path(vm, gb, path_b, sizeof(path_b));
      if (rc < 0) { free(ga); free(gb); return -thistle_errno(errno); }
      if (number == SC_RENAME) rc = rename(path_a, path_b);
      else if (number == SC_LINK) rc = link(path_a, path_b);
      else rc = symlink(ga, path_b);
      free(ga); free(gb);
      return rc < 0 ? -thistle_errno(errno) : 0;
    }

    case SC_CHMOD: {
      char *guest = vm_string(vm, a0);
      rc = guest_path(vm, guest, path_a, sizeof(path_a));
      free(guest);
      if (rc < 0) return -thistle_errno(errno);
      return chmod(path_a, (mode_t)a1) < 0 ? -thistle_errno(errno) : 0;
    }

    case SC_RANDOM: {
      if (a1 > NEMU_MAX_IO) { errno = EFBIG; return -thistle_errno(errno); }
      uint8_t *buffer = malloc((size_t)a1 ? (size_t)a1 : 1u);
      if (!buffer) { errno = ENOMEM; return -thistle_errno(errno); }
      rc = random_bytes(buffer, (size_t)a1);
      if (rc == 0) vm_write(vm, a0, buffer, (size_t)a1);
      free(buffer);
      return rc < 0 ? -thistle_errno(errno) : (int64_t)a1;
    }

    case SC_SPAWN: {
      char *guest_program = vm_string(vm, a0);
      if (guest_path(vm, guest_program, path_a, sizeof(path_a)) < 0) {
        free(guest_program); return -thistle_errno(errno);
      }
      if (a2 > NEMU_MAX_ARGS) {
        free(guest_program); errno = EFBIG; return -thistle_errno(errno);
      }
      size_t count = (size_t)a2;
      char **guest_argv = calloc(count + 1u, sizeof(*guest_argv));
      if (!guest_argv) {
        free(guest_program); errno = ENOMEM; return -thistle_errno(errno);
      }
      for (size_t i = 0; i < count; ++i) {
        uint64_t ptr = bits == 32 ? vm_u32(vm, a1 + i * 4u) : vm_u64(vm, a1 + i * 8u);
        guest_argv[i] = vm_string(vm, ptr);
