      }
      size_t base = 7u;
      char **exec_argv = calloc(base + count + 1u, sizeof(*exec_argv));
      if (!exec_argv) {
        for (size_t i = 0; i < count; ++i) free(guest_argv[i]);
        free(guest_argv);
        free(guest_program);
        errno = ENOMEM;
        return -thistle_errno(errno);
      }
      exec_argv[0] = vm->self;
      exec_argv[1] = "--image";
      exec_argv[2] = vm->root;
      exec_argv[3] = "--entrypoint";
      exec_argv[4] = guest_program;
      exec_argv[5] = "--";
      for (size_t i = 0; i < count; ++i) exec_argv[6u + i] = guest_argv[i];
      exec_argv[6u + count] = NULL;
      pid_t pid = -1;
      int spawn_error = posix_spawn(&pid, vm->self, NULL, NULL, exec_argv, environ);
      int saved = spawn_error;
      free(exec_argv);
      for (size_t i = 0; i < count; ++i) free(guest_argv[i]);
      free(guest_argv);
      free(guest_program);
      if (spawn_error != 0) {
        errno = saved;
        return -thistle_errno(errno);
      }
      return pid;
    }

    case SC_WAIT: {
      int status = 0;
      pid_t pid;
      do { pid = waitpid((pid_t)(int64_t)a0, &status, 0); }
      while (pid < 0 && errno == EINTR);
      if (pid < 0) return -thistle_errno(errno);
      if (WIFEXITED(status)) return WEXITSTATUS(status);
      if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
      return 0;
    }

    case SC_KILL:
      return kill((pid_t)(int64_t)a0, (int)(int64_t)a1) < 0 ?
        -thistle_errno(errno) : 0;

    case SC_SLEEP: {
      struct timespec req = {
        .tv_sec = (time_t)(a0 / 1000u),
        .tv_nsec = (long)((a0 % 1000u) * 1000000u)
      };
      while (nanosleep(&req, &req) < 0) {
        if (errno != EINTR) return -thistle_errno(errno);
      }
      return 0;
    }

    case SC_GETUID: return getuid();
    case SC_GETGID: return getgid();

    case SC_DUP: {
      int fd = (int)a0;
      int wanted = (int)(int64_t)a1;
      int out = wanted < 0 ? dup(fd) : dup2(fd, wanted);
      return out < 0 ? -thistle_errno(errno) : out;
    }

    case SC_READLINK: {
      char *guest = vm_string(vm, a0);
      rc = guest_path(vm, guest, path_a, sizeof(path_a));
      free(guest);
      if (rc < 0) return -thistle_errno(errno);
      if (a2 > NEMU_MAX_IO) { errno = EFBIG; return -thistle_errno(errno); }
      char *buffer = malloc((size_t)a2 ? (size_t)a2 : 1u);
      if (!buffer) { errno = ENOMEM; return -thistle_errno(errno); }
      ssize_t n = readlink(path_a, buffer, (size_t)a2);
      if (n >= 0) vm_write(vm, a1, buffer, (size_t)n);
      free(buffer);
