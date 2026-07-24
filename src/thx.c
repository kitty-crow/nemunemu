#include "thx/00_prelude.inc"
#include <sys/utsname.h>
#include "thx/10_image.inc"
#include "thx/20_memory.inc"
#include "thx/30_linux.inc"
#include "thx/40_vm32.inc"
#include "thx/45_rv64.inc"

#define run64 run_thistle64
#define nemu_check_image nemu_check_image_canonical
#define nemu_run nemu_run_canonical
#include "thx/50_vm64.inc"
#undef nemu_run
#undef nemu_check_image
#undef run64

#include "thx/55_dispatch.inc"
