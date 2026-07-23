#ifndef NEMUNEMU_THX_H
#define NEMUNEMU_THX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nemu_run_options {
  const char *image_path;
  const char *entrypoint;
  const char *root_path;
  int argc;
  char **argv;
} nemu_run_options;

int nemu_run(const nemu_run_options *options);
int nemu_check_image(const char *path, char *message, size_t message_size);

#ifdef __cplusplus
}
#endif
#endif
