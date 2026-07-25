static int run_selected64(NemuVm *vm, const nemu_run_options *options) {
  if (strcmp(vm->image->isa, "rv64gc") == 0) return run_rv64(vm, options);
  return run_thistle64(vm, options);
}

int nemu_check_image(const char *path, char *message, size_t message_size) {
  return nemu_check_image_canonical(path, message, message_size);
}

int nemu_run(const nemu_run_options *options) {
  if (!options || !options->image_path) return NEMU_USAGE;
  char entry_host[NEMU_MAX_PATH];
  char root_host[NEMU_MAX_PATH];
  struct stat st;
  if (stat(options->image_path, &st) < 0) {
    fprintf(stderr, "nemunemu: %s: %s\n", options->image_path, strerror(errno));
    return NEMU_NOINPUT;
  }

  const char *entry = options->entrypoint;
  const char *root = options->root_path;
  if (S_ISDIR(st.st_mode)) {
    if (!realpath(options->image_path, root_host)) {
      fprintf(stderr, "nemunemu: %s: %s\n", options->image_path, strerror(errno));
      return NEMU_NOINPUT;
    }
    root = root_host;
    if (!entry) entry = "/sbin/init.thx";
    NemuVm path_vm;
    memset(&path_vm, 0, sizeof(path_vm));
    (void)snprintf(path_vm.root, sizeof(path_vm.root), "%s", root);
    (void)snprintf(path_vm.cwd, sizeof(path_vm.cwd), "/");
    if (guest_path(&path_vm, entry, entry_host, sizeof(entry_host)) < 0) {
      fprintf(stderr, "nemunemu: %s: %s\n", entry, strerror(errno));
      return NEMU_NOINPUT;
    }
  } else if (S_ISREG(st.st_mode)) {
    if (snprintf(entry_host, sizeof(entry_host), "%s", options->image_path) >=
        (int)sizeof(entry_host)) {
      fprintf(stderr, "nemunemu: image path is too long\n");
      return NEMU_NOINPUT;
    }
    if (!root) root = "/";
    if (!entry) entry = options->image_path;
  } else {
    fprintf(stderr, "nemunemu: image is neither a THX executable nor a system-image directory\n");
    return NEMU_DATA;
  }

  NemuImage image;
  char message[512];
  int status = load_image(entry_host, &image, message, sizeof(message));
  if (status != NEMU_OK) {
    fprintf(stderr, "nemunemu: %s\n", message);
    return status;
  }

  NemuVm vm;
  nemu_run_options resolved = *options;
  resolved.root_path = root;
  if (resolved.argc == 0) {
    resolved.argc = 1;
    resolved.argv = (char **)&entry;
  }
  status = vm_prepare(&vm, &image, &resolved);
  if (status != NEMU_OK) {
#if defined(__wasm__)
    static const char message[] =
      "nemunemu: could not initialise guest runtime\n";
    (void)write(STDERR_FILENO, message, sizeof(message) - 1u);
#else
    fprintf(stderr, "nemunemu: could not initialise guest runtime\n");
#endif
    free_image(&image);
    return status;
  }

#if defined(__wasm__)
  status = image.bits == 32 ? run32(&vm, &resolved) : run_selected64(&vm, &resolved);
#else
  vm.trap_ready = 1;
  if (setjmp(vm.trap) != 0) {
    fprintf(stderr, "nemunemu: %s\n", vm.fault);
    status = vm.fault_status ? vm.fault_status : NEMU_SOFTWARE;
  } else {
    status = image.bits == 32 ? run32(&vm, &resolved) : run_selected64(&vm, &resolved);
  }
  vm.trap_ready = 0;
#endif
  memory_free(&vm.memory);
  free_image(&image);

  if (getpid() == 1) {
    int child_status;
    while (waitpid(-1, &child_status, WNOHANG) > 0) {}
  }
  return status;
}
