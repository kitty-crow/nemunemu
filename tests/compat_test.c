#define _POSIX_C_SOURCE 200809L

#include "nemunemu/compat.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_text(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  assert(file != NULL);
  assert(fputs(text, file) >= 0);
  assert(fclose(file) == 0);
}

int main(void) {
  char marker[] = "/tmp/nemunemu-marker-XXXXXX";
  int marker_fd = mkstemp(marker);
  assert(marker_fd >= 0);
  close(marker_fd);
  write_text(marker, "#!/sbin/nemunemu --marker\n#!thistle:ls\n");

  char value[128];
  assert(nemu_compat_read_marker(marker, value, sizeof(value)) == 0);
  assert(strcmp(value, "ls") == 0);
  unlink(marker);

  char thx[] = "/tmp/nemunemu-thx-XXXXXX";
  int thx_fd = mkstemp(thx);
  assert(thx_fd >= 0);
  close(thx_fd);
  write_text(
    thx,
    "#!/sbin/nemunemu --thx-wrapper\n"
    "#!nemunemu-thx:/usr/libexec/nemunemu/thx/bin/demo\n"
  );
  assert(nemu_compat_read_thx(thx, value, sizeof(value)) == 0);
  assert(strcmp(value, "/usr/libexec/nemunemu/thx/bin/demo") == 0);
  unlink(thx);
  return 0;
}
