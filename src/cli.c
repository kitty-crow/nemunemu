#define _POSIX_C_SOURCE 200809L

#include "nemunemu/cli.h"
#include "nemunemu/compat.h"
#include "nemunemu/thx.h"

#include <stdio.h>
#include <string.h>

static const char *value(int argc, char **argv, int *index, const char *name) {
  size_t n = strlen(name);
  if (strncmp(argv[*index], name, n) == 0 && argv[*index][n] == '=') {
    return argv[*index] + n + 1;
  }
  if (strcmp(argv[*index], name) == 0) {
    if (*index + 1 >= argc) return NULL;
    return argv[++*index];
  }
  return NULL;
}

static void usage(FILE *out) {
  fputs(
    "Usage: nemunemu --image PATH [--entrypoint PATH] [--root PATH] [--] [ARG ...]\n"
    "       nemunemu --shell ROOT\n"
    "       nemunemu --marker SCRIPT [ARG ...]\n"
    "       nemunemu --thx-wrapper SCRIPT [ARG ...]\n"
    "       nemunemu --check PATH\n"
    "       nemunemu --probe | --version\n",
    out
  );
}

int nemunemu_cli_main(int argc, char **argv) {
  /*
   * Linux invokes init with argv[0] only. NEMUNEMU must remain PID 1 so it can
   * supervise and reap the interactive shell. Replacing PID 1 with BusyBox hush
   * gives the shell Linux init's special signal semantics and can leave it
   * blocked forever while waiting for an otherwise completed command.
   */
  if (argc == 1) {
    return nemu_compat_init("/");
  }

  if (argc >= 3 && strcmp(argv[1], "--shell") == 0) {
    return nemu_compat_shell(argv[2]);
  }
  if (argc >= 3 && strcmp(argv[1], "--marker") == 0) {
    return nemu_compat_marker(argv[2], argc - 3, argv + 3);
  }
  if (argc >= 3 && strcmp(argv[1], "--thx-wrapper") == 0) {
    return nemu_compat_thx_wrapper(argv[2], argc - 3, argv + 3);
  }

  const char *image = NULL;
  const char *entrypoint = NULL;
  const char *root = NULL;
  const char *check = NULL;
  int guest_at = argc;

  if (argc == 2 && strcmp(argv[1], "--version") == 0) {
    puts("NEMUNEMU 0.1.0");
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--probe") == 0) {
    puts("nemunemu: ready (mikuOS compatibility, THX32, THX64, Linux ABI)");
    return 0;
  }

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--") == 0) {
      guest_at = i + 1;
      break;
    }
    if (strcmp(argv[i], "--help") == 0) {
      usage(stdout);
      return 0;
    }
    const char *v;
    if ((v = value(argc, argv, &i, "--image")) != NULL) image = v;
    else if ((v = value(argc, argv, &i, "--entrypoint")) != NULL) entrypoint = v;
    else if ((v = value(argc, argv, &i, "--root")) != NULL) root = v;
    else if ((v = value(argc, argv, &i, "--check")) != NULL) check = v;
    else {
      fprintf(stderr, "nemunemu: unknown argument: %s\n", argv[i]);
      usage(stderr);
      return 64;
    }
  }

  if (check) {
    char message[512];
    int status = nemu_check_image(check, message, sizeof(message));
    FILE *out = status == 0 ? stdout : stderr;
    fprintf(out, "nemunemu: %s\n", message);
    return status;
  }
  if (!image) {
    usage(stderr);
    return 64;
  }

  char *default_argv[2] = {(char *)(entrypoint ? entrypoint : image), NULL};
  nemu_run_options options = {
    .image_path = image,
    .entrypoint = entrypoint,
    .root_path = root,
    .argc = guest_at < argc ? argc - guest_at : 1,
    .argv = guest_at < argc ? argv + guest_at : default_argv
  };
  return nemu_run(&options);
}
