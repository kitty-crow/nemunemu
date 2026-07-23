
#include "nemunemu/cli.h"

#include <assert.h>

int main(void) {
  char executable[] = "nemunemu";
  char probe[] = "--probe";
  char version[] = "--version";
  char *probe_argv[] = {executable, probe};
  char *version_argv[] = {executable, version};
  assert(nemunemu_cli_main(2, probe_argv) == 0);
  assert(nemunemu_cli_main(2, version_argv) == 0);
  return 0;
}
