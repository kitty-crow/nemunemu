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

  static const char *const supported[] = {
    "[", "base64", "basename", "cat", "chmod", "chown", "clear", "cp", "cut",
    "date", "df", "dirname", "dmesg", "echo", "env", "expr", "false", "file", "find",
    "free", "grep", "head", "help", "hostname", "id", "kill", "ln", "ls", "mkdir",
    "mount", "mv", "printenv", "printf", "ps", "pwd", "readlink", "rm", "rmdir",
    "sed", "seq", "sh", "sleep", "sort", "stat", "strings", "tail", "tee", "test",
    "thsh", "time", "touch", "tr", "true", "uname", "uniq", "uptime", "wc", "wget",
    "which", "whoami", "yes"
  };
  for (size_t index = 0; index < sizeof(supported) / sizeof(supported[0]); ++index) {
    assert(nemu_compat_marker_supported(supported[index]));
  }

  static const char *const unsupported[] = {
    "as", "dis", "elf2thx", "ld", "nm", "objdump", "size", "wasm"
  };
  for (size_t index = 0; index < sizeof(unsupported) / sizeof(unsupported[0]); ++index) {
    assert(!nemu_compat_marker_supported(unsupported[index]));
  }
  assert(!nemu_compat_marker_supported(NULL));
  assert(!nemu_compat_marker_supported(""));

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
