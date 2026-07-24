  char *rv64 = temp_path("rv64");
  make_thx32(thx32, 42);
  make_thx64(thx64, 43, 0);
  make_thx64(writer, 7, 1);
  make_rv64(rv64, 9);

  char message[256];
  assert(nemu_check_image(thx32, message, sizeof(message)) == 0);
  assert(strstr(message, "THX32") != NULL);
  assert(nemu_check_image(thx64, message, sizeof(message)) == 0);
  assert(strstr(message, "THX64") != NULL);
  assert(nemu_check_image(rv64, message, sizeof(message)) == 0);
  assert(strstr(message, "rv64gc") != NULL);

  assert(run_file(thx32) == 42);
  assert(run_file(thx64) == 43);

  char root_template[] = "/tmp/nemunemu-root-XXXXXX";
  char *root = mkdtemp(root_template);
  assert(root);
  char sbin[256], init_path[256];
  snprintf(sbin, sizeof(sbin), "%s/sbin", root);
  snprintf(init_path, sizeof(init_path), "%s/sbin/init.thx", root);
  assert(mkdir(sbin, 0700) == 0);
  make_thx64(init_path, 23, 0);
  char *root_argv[] = {"/sbin/init.thx", NULL};
  nemu_run_options root_options = {
    .image_path = root,
    .entrypoint = NULL,
    .root_path = NULL,
    .argc = 1,
    .argv = root_argv
  };
  assert(nemu_run(&root_options) == 23);

  char output[] = "/tmp/nemunemu-output-XXXXXX";
  int fd = mkstemp(output);
  assert(fd >= 0);
  int saved = dup(STDOUT_FILENO);
  assert(saved >= 0);
  assert(dup2(fd, STDOUT_FILENO) >= 0);
  assert(run_file(writer) == 7);
  fflush(stdout);
  assert(dup2(saved, STDOUT_FILENO) >= 0);
  close(saved);
  lseek(fd, 0, SEEK_SET);
  char actual[64] = {0};
  ssize_t n = read(fd, actual, sizeof(actual) - 1u);
  assert(n > 0);
  assert(strcmp(actual, "nemunemu guest write\n") == 0);
  close(fd);

  char rv_output[] = "/tmp/nemunemu-rv64-output-XXXXXX";
  int rv_fd = mkstemp(rv_output);
  assert(rv_fd >= 0);
  saved = dup(STDOUT_FILENO);
  assert(saved >= 0);
  assert(dup2(rv_fd, STDOUT_FILENO) >= 0);
  assert(run_file(rv64) == 9);
  fflush(stdout);
  assert(dup2(saved, STDOUT_FILENO) >= 0);
  close(saved);
  lseek(rv_fd, 0, SEEK_SET);
  memset(actual, 0, sizeof(actual));
  n = read(rv_fd, actual, sizeof(actual) - 1u);
  assert(n > 0);
  assert(strcmp(actual, "nemunemu rv64 write\n") == 0);
  close(rv_fd);

  unlink(init_path); rmdir(sbin); rmdir(root);
  unlink(thx32); unlink(thx64); unlink(writer); unlink(rv64); unlink(output); unlink(rv_output);
  free(thx32); free(thx64); free(writer); free(rv64);
  return 0;
}
