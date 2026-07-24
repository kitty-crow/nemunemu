#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "nemunemu/compat.h"
#include "nemunemu/thx.h"

#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define NEMU_BUSYBOX "/usr/libexec/nemunemu/busybox"
#define NEMU_MARKER "#!thistle:"
#define NEMU_SHELL_APPLET "sh"
#define NEMU_HOSTNAME "mikuos"

static int read_contract_line(const char *path, const char *prefix, char *out, size_t out_size) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "nemunemu: %s: %s\n", path, strerror(errno));
    return 126;
  }

  char line[512];
  bool found = false;
  for (unsigned number = 0; number < 3 && fgets(line, sizeof(line), file); ++number) {
    size_t length = strlen(line);
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
      line[--length] = '\0';
    }
    if (strncmp(line, prefix, strlen(prefix)) == 0) {
      const char *value = line + strlen(prefix);
      if (!*value || strlen(value) + 1 > out_size) {
        fclose(file);
        fprintf(stderr, "nemunemu: malformed compatibility wrapper: %s\n", path);
        return 126;
      }
      memcpy(out, value, strlen(value) + 1);
      found = true;
      break;
    }
  }
  fclose(file);
  if (!found) {
    fprintf(stderr, "nemunemu: compatibility contract is missing from %s\n", path);
    return 126;
  }
  return 0;
}

int nemu_compat_read_marker(const char *script, char *out, size_t out_size) {
  return read_contract_line(script, NEMU_MARKER, out, out_size);
}

int nemu_compat_read_thx(const char *script, char *out, size_t out_size) {
  return read_contract_line(script, "#!nemunemu-thx:", out, out_size);
}

static bool safe_name(const char *name) {
  if (!name || !*name) return false;
  for (const unsigned char *cursor = (const unsigned char *)name; *cursor; ++cursor) {
    const unsigned char c = *cursor;
    if (!(c == '[' || c == ']' || c == '_' || c == '-' || c == '+' || c == '.' ||
          (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
      return false;
    }
  }
  return true;
}

static bool busybox_applet(const char *name) {
  static const char *const applets[] = {
    "[", "base64", "basename", "cat", "chmod", "chown", "clear", "cp", "cut",
    "date", "df", "dirname", "dmesg", "echo", "env", "expr", "false", "file", "find",
    "free", "grep", "head", "hostname", "id", "kill", "ln", "ls", "mkdir",
    "mount", "mv", "printenv", "printf", "ps", "pwd", "readlink", "rm", "rmdir",
    "sed", "seq", "sh", "sleep", "sort", "stat", "strings", "tail", "tee", "test", "time",
    "touch", "tr", "true", "uname", "uniq", "uptime", "wc", "wget", "which",
    "whoami", "yes"
  };
  if (!name) return false;
  for (size_t i = 0; i < sizeof(applets) / sizeof(applets[0]); ++i) {
    if (strcmp(name, applets[i]) == 0) return true;
  }
  return false;
}

bool nemu_compat_marker_supported(const char *name) {
  if (!name) return false;
  return strcmp(name, "thsh") == 0 || strcmp(name, "help") == 0 || busybox_applet(name);
}

static int exec_busybox(const char *applet, int argc, char **argv) {
  if (!safe_name(applet)) {
    fprintf(stderr, "nemunemu: invalid compatibility applet: %s\n", applet ? applet : "");
    return 126;
  }

  char **child = calloc((size_t)argc + 3u, sizeof(*child));
  if (!child) {
    fprintf(stderr, "nemunemu: out of memory\n");
    return 71;
  }
  child[0] = (char *)"busybox";
  child[1] = (char *)applet;
  for (int i = 0; i < argc; ++i) child[i + 2] = argv[i];
  child[argc + 2] = NULL;
  execv(NEMU_BUSYBOX, child);
  const int saved = errno;
  free(child);
  fprintf(stderr, "nemunemu: %s %s: %s\n", NEMU_BUSYBOX, applet, strerror(saved));
  return saved == ENOENT ? 127 : 126;
}

static void print_motd(void) {
  FILE *file = fopen("/etc/motd", "rb");
  if (!file) return;
  struct utsname identity;
  const bool have_uname = uname(&identity) == 0;
  char line[1024];
  while (fgets(line, sizeof(line), file)) {
    char *kernel = strstr(line, "Kernel source:");
    char *suffix = kernel ? strstr(kernel, " Run ") : NULL;
    if (have_uname && kernel && suffix) {
      fwrite(line, 1, (size_t)(kernel - line), stdout);
      fprintf(stdout, "Kernel source: %s %s.%s", identity.sysname, identity.release, suffix);
    } else {
      fputs(line, stdout);
    }
  }
  fclose(file);
}

static int print_help(void) {
  puts("mikuOS compatibility commands are provided by the packaged userland.");
  puts("NEMUNEMU executes THX programmes directly and maps rescue markers to Linux applets.");
  puts("Use 'ls /bin' to list installed commands.");
  return 0;
}

static int prepare_compat_root(const char *root) {
  if (!root || !*root) {
    fprintf(stderr, "nemunemu: compatibility root is required\n");
    return 64;
  }

  char resolved[PATH_MAX];
  if (!realpath(root, resolved)) {
    fprintf(stderr, "nemunemu: %s: %s\n", root, strerror(errno));
    return 66;
  }
  if (chroot(resolved) < 0 || chdir("/") < 0) {
    fprintf(stderr, "nemunemu: cannot enter %s: %s\n", resolved, strerror(errno));
    return 71;
  }

  if (sethostname(NEMU_HOSTNAME, strlen(NEMU_HOSTNAME)) < 0 && errno != EPERM) {
    fprintf(stderr, "nemunemu: cannot set hostname: %s\n", strerror(errno));
  }

  (void)setenv("PATH", "/bin:/usr/bin:/sbin:/usr/sbin", 1);
  (void)setenv("SHELL", "/bin/sh", 1);
  (void)setenv("MIKUOS_KERNEL_MODE", "neru", 1);
  (void)setenv("MIKUOS_KERNEL_SOURCE", "Linux", 1);
  (void)setenv("HOME", "/root", 1);
  (void)setenv("USER", "root", 1);
  (void)setenv("LOGNAME", "root", 1);
  (void)setenv("HOSTNAME", NEMU_HOSTNAME, 1);
  (void)setenv("PS1", "root@mikuos:\\w# ", 1);
  if (!getenv("TERM")) (void)setenv("TERM", "xterm-256color", 1);
  return 0;
}

static int shell_child(void *unused) {
  (void)unused;

  /* Give the interactive shell a normal process/session context beneath init. */
  if (setsid() >= 0) {
#ifdef TIOCSCTTY
    (void)ioctl(STDIN_FILENO, TIOCSCTTY, 0);
#endif
  }

  char *const shell_argv[] = {(char *)"busybox", (char *)NEMU_SHELL_APPLET, (char *)"-l", NULL};
  execv(NEMU_BUSYBOX, shell_argv);
  _exit(errno == ENOENT ? 127 : 126);
}

static pid_t spawn_shell(void) {
#ifdef __wasm__
  return clone(shell_child, NULL, CLONE_VM | CLONE_VFORK | SIGCHLD, NULL);
#else
  pid_t pid = fork();
  if (pid == 0) shell_child(NULL);
  return pid;
#endif
}

int nemu_compat_init(const char *root) {
  int status = prepare_compat_root(root);
  if (status != 0) return status;

  (void)signal(SIGCHLD, SIG_DFL);
  print_motd();
  fflush(stdout);

  for (;;) {
    pid_t shell = spawn_shell();
    if (shell < 0) {
      fprintf(stderr, "nemunemu: cannot spawn mikuOS shell: %s\n", strerror(errno));
      sleep(1);
      continue;
    }

    int shell_status = 0;
    pid_t waited;
    do {
      waited = waitpid(shell, &shell_status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0) {
      fprintf(stderr, "nemunemu: cannot reap mikuOS shell: %s\n", strerror(errno));
    } else if (WIFSIGNALED(shell_status)) {
      fprintf(stderr, "nemunemu: shell terminated by signal %d; restarting\n", WTERMSIG(shell_status));
    } else {
      fprintf(stderr, "nemunemu: shell exited with status %d; restarting\n", WEXITSTATUS(shell_status));
    }
    sleep(1);
  }
}

int nemu_compat_shell(const char *root) {
  int status = prepare_compat_root(root);
  if (status != 0) return status;

  print_motd();
  fflush(stdout);
  char *const shell_argv[] = {(char *)"busybox", (char *)NEMU_SHELL_APPLET, (char *)"-l", NULL};
  execv(NEMU_BUSYBOX, shell_argv);
  fprintf(stderr, "nemunemu: cannot start mikuOS shell: %s\n", strerror(errno));
  return errno == ENOENT ? 127 : 126;
}

int nemu_compat_marker(const char *script, int argc, char **argv) {
  char applet[128];
  int status = nemu_compat_read_marker(script, applet, sizeof(applet));
  if (status != 0) return status;

  if (strcmp(applet, "thsh") == 0) return exec_busybox(NEMU_SHELL_APPLET, argc, argv);
  if (strcmp(applet, "help") == 0) return print_help();
  if (busybox_applet(applet)) return exec_busybox(applet, argc, argv);

  fprintf(stderr,
    "%s: the mikuOS rescue implementation is not yet mapped by NEMUNEMU\n",
    applet);
  return 127;
}

int nemu_compat_thx_wrapper(const char *script, int argc, char **argv) {
  char image[PATH_MAX];
  int status = nemu_compat_read_thx(script, image, sizeof(image));
  if (status != 0) return status;

  char **guest = calloc((size_t)argc + 2u, sizeof(*guest));
  if (!guest) return 71;
  guest[0] = (char *)script;
  for (int i = 0; i < argc; ++i) guest[i + 1] = argv[i];
  guest[argc + 1] = NULL;
  nemu_run_options options = {
    .image_path = image,
    .entrypoint = image,
    .root_path = "/",
    .argc = argc + 1,
    .argv = guest,
  };
  status = nemu_run(&options);
  free(guest);
  return status;
}
