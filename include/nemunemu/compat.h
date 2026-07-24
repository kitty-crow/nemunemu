#ifndef NEMUNEMU_COMPAT_H
#define NEMUNEMU_COMPAT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int nemu_compat_read_marker(const char *script, char *out, size_t out_size);
int nemu_compat_read_thx(const char *script, char *out, size_t out_size);
bool nemu_compat_marker_supported(const char *name);
int nemu_compat_init(const char *root);
int nemu_compat_shell(const char *root);
int nemu_compat_marker(const char *script, int argc, char **argv);
int nemu_compat_thx_wrapper(const char *script, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
