static int run64(NemuVm *vm, const nemu_run_options *options) {
  uint64_t r[32] = {0};
  double f[16] = {0};
  bool z = false, n = false, c = false, o = false;
  uint64_t pc = vm->image->entry;
  uint64_t sp = vm->memory.size - 16u;
  size_t ec = env_count();
  uint64_t *env_ptrs = calloc(ec ? ec : 1u, sizeof(*env_ptrs));
  uint64_t *arg_ptrs = calloc(options->argc ? (size_t)options->argc : 1u, sizeof(*arg_ptrs));
  if (!env_ptrs || !arg_ptrs) vm_fault(vm, NEMU_OSERR, "out of host memory");

  for (size_t i = 0; i < ec; ++i) env_ptrs[i] = stack_put(vm, &sp, environ[i]);
  for (int i = 0; i < options->argc; ++i) arg_ptrs[i] = stack_put(vm, &sp, options->argv[i]);
  sp &= ~UINT64_C(7);
  sp -= (ec + 1u) * 8u;
  uint64_t env_vec = sp;
  for (size_t i = 0; i < ec; ++i) vm_set64(vm, env_vec + i * 8u, env_ptrs[i]);
  vm_set64(vm, env_vec + ec * 8u, 0);
  sp -= ((size_t)options->argc + 1u) * 8u;
  uint64_t arg_vec = sp;
  for (int i = 0; i < options->argc; ++i) vm_set64(vm, arg_vec + (size_t)i * 8u, arg_ptrs[i]);
  vm_set64(vm, arg_vec + (size_t)options->argc * 8u, 0);
  sp = (sp - 8u) & ~UINT64_C(15);
  vm_set64(vm, sp, 0);
  free(env_ptrs); free(arg_ptrs);
  r[0] = (uint64_t)options->argc; r[1] = arg_vec;
  r[2] = ec; r[3] = env_vec; r[29] = 0; r[30] = sp; r[31] = 0;

  while (!vm->done) {
    if (vm->fuel && --vm->fuel == 0) vm_fault(vm, NEMU_DATA, "Thistle64 instruction limit reached");
    vm_check_exec(vm, pc, 16);
    uint8_t ins[16]; memory_read_raw(vm, pc, ins, sizeof(ins));
    uint8_t op = ins[0], d = ins[1], a = ins[2], b = ins[3];
    int64_t imm = (int64_t)rd64(ins + 8);
    double fimm;
    uint64_t fbits = rd64(ins + 8);
    memcpy(&fimm, &fbits, sizeof(fimm));
    if (d > 31 || a > 31 || b > 31 || op > OP64_FST64) {
      vm_fault(vm, NEMU_DATA, "bad Thistle64 instruction at 0x%016" PRIx64, pc);
    }
    pc += 16u;
    uint64_t x = r[a], y = r[b];
    switch (op) {
      case OP64_NOP: break;
      case OP64_HALT: vm->exit_code = (int)(r[0] & 0xffu); vm->done = 1; break;
      case OP64_MOV: r[d] = x; break;
      case OP64_LI: r[d] = (uint64_t)imm; break;
      case OP64_ADD: r[d] = x + y; break;
      case OP64_ADDI: r[d] = x + (uint64_t)imm; break;
      case OP64_SUB: r[d] = x - y; break;
      case OP64_SUBI: r[d] = x - (uint64_t)imm; break;
      case OP64_MUL: r[d] = x * y; break;
      case OP64_MULI: r[d] = x * (uint64_t)imm; break;
      case OP64_DIV:
        if (y == 0) vm_fault(vm, NEMU_DATA, "integer division by zero");
        r[d] = ((int64_t)x == INT64_MIN && (int64_t)y == -1) ?
          UINT64_C(0x8000000000000000) : (uint64_t)((int64_t)x / (int64_t)y); break;
      case OP64_DIVU:
        if (y == 0) vm_fault(vm, NEMU_DATA, "integer division by zero");
        r[d] = x / y; break;
      case OP64_MOD:
        if (y == 0) vm_fault(vm, NEMU_DATA, "integer division by zero");
        r[d] = ((int64_t)x == INT64_MIN && (int64_t)y == -1) ?
          0 : (uint64_t)((int64_t)x % (int64_t)y); break;
      case OP64_MODU:
        if (y == 0) vm_fault(vm, NEMU_DATA, "integer division by zero");
        r[d] = x % y; break;
      case OP64_AND: r[d] = x & y; break;
      case OP64_ANDI: r[d] = x & (uint64_t)imm; break;
      case OP64_OR: r[d] = x | y; break;
      case OP64_ORI: r[d] = x | (uint64_t)imm; break;
      case OP64_XOR: r[d] = x ^ y; break;
      case OP64_XORI: r[d] = x ^ (uint64_t)imm; break;
      case OP64_NOT: r[d] = ~x; break;
      case OP64_NEG: r[d] = 0u - x; break;
      case OP64_SHL: r[d] = x << (y & 63u); break;
      case OP64_SHLI: r[d] = x << ((uint64_t)imm & 63u); break;
      case OP64_SHR: r[d] = x >> (y & 63u); break;
      case OP64_SHRI: r[d] = x >> ((uint64_t)imm & 63u); break;
      case OP64_SAR: r[d] = (uint64_t)((int64_t)x >> (y & 63u)); break;
      case OP64_SARI: r[d] = (uint64_t)((int64_t)x >> ((uint64_t)imm & 63u)); break;
      case OP64_CMP:
      case OP64_CMPI: {
        uint64_t rhs = op == OP64_CMP ? y : (uint64_t)imm;
        uint64_t ur = x - rhs;
        int64_t sr = (int64_t)ur;
        z = sr == 0; n = sr < 0; c = x >= rhs;
        o = (((x ^ rhs) & (x ^ ur) & (UINT64_C(1) << 63)) != 0);
        break;
      }
      case OP64_TEST: z = (x & y) == 0; n = (int64_t)(x & y) < 0; c = false; o = false; break;
      case OP64_JMP: pc += (uint64_t)imm; break;
      case OP64_JMPR: pc = r[d]; break;
      case OP64_JE: if (z) pc += (uint64_t)imm; break;
      case OP64_JNE: if (!z) pc += (uint64_t)imm; break;
      case OP64_JL: if (n != o) pc += (uint64_t)imm; break;
      case OP64_JLE: if (z || n != o) pc += (uint64_t)imm; break;
      case OP64_JG: if (!z && n == o) pc += (uint64_t)imm; break;
      case OP64_JGE: if (n == o) pc += (uint64_t)imm; break;
      case OP64_JB: if (!c) pc += (uint64_t)imm; break;
      case OP64_JBE: if (!c || z) pc += (uint64_t)imm; break;
      case OP64_JA: if (c && !z) pc += (uint64_t)imm; break;
      case OP64_JAE: if (c) pc += (uint64_t)imm; break;
      case OP64_CALL:
        r[30] -= 8u; vm_set64(vm, r[30], pc); pc += (uint64_t)imm; break;
      case OP64_CALLR:
        r[30] -= 8u; vm_set64(vm, r[30], pc); pc = r[d]; break;
      case OP64_RET: {
        uint64_t ret = vm_u64(vm, r[30]); r[30] += 8u;
        if (!ret) { vm->exit_code = (int)(r[0] & 0xffu); vm->done = 1; }
        else pc = ret;
        break;
      }
      case OP64_PUSH: r[30] -= 8u; vm_set64(vm, r[30], r[d]); break;
      case OP64_POP: r[d] = vm_u64(vm, r[30]); r[30] += 8u; break;
      case OP64_ENTER:
        r[30] -= 8u; vm_set64(vm, r[30], r[29]); r[29] = r[30];
        r[30] -= (uint64_t)imm; vm_check_memory(vm, r[30], (size_t)imm, true); break;
      case OP64_LEAVE:
        r[30] = r[29]; r[29] = vm_u64(vm, r[30]); r[30] += 8u; break;
      case OP64_LD8U: r[d] = vm_u8(vm, x + (uint64_t)imm); break;
      case OP64_LD8S: r[d] = (uint64_t)(int64_t)(int8_t)vm_u8(vm, x + (uint64_t)imm); break;
      case OP64_LD16U: r[d] = vm_u16(vm, x + (uint64_t)imm); break;
      case OP64_LD16S: r[d] = (uint64_t)(int64_t)(int16_t)vm_u16(vm, x + (uint64_t)imm); break;
      case OP64_LD32U: r[d] = vm_u32(vm, x + (uint64_t)imm); break;
      case OP64_LD32S: r[d] = (uint64_t)(int64_t)(int32_t)vm_u32(vm, x + (uint64_t)imm); break;
      case OP64_LD64: r[d] = vm_u64(vm, x + (uint64_t)imm); break;
      case OP64_ST8: vm_set8(vm, x + (uint64_t)imm, (uint8_t)r[d]); break;
      case OP64_ST16: vm_set16(vm, x + (uint64_t)imm, (uint16_t)r[d]); break;
      case OP64_ST32: vm_set32(vm, x + (uint64_t)imm, (uint32_t)r[d]); break;
      case OP64_ST64: vm_set64(vm, x + (uint64_t)imm, r[d]); break;
      case OP64_SYS: {
        uint64_t aa[6] = {r[0], r[1], r[2], r[3], r[4], r[5]};
        r[0] = (uint64_t)syscall_common(vm, 64, (uint64_t)imm, aa);
        break;
      }
      case OP64_XCHG: { uint64_t q = r[d]; r[d] = x; r[a] = q; break; }
      case OP64_SEX8: r[d] = (uint64_t)(int64_t)(int8_t)x; break;
      case OP64_SEX16: r[d] = (uint64_t)(int64_t)(int16_t)x; break;
      case OP64_SEX32: r[d] = (uint64_t)(int64_t)(int32_t)x; break;
      case OP64_SETE: r[d] = z; break;
      case OP64_SETNE: r[d] = !z; break;
      case OP64_SETL: r[d] = n != o; break;
      case OP64_SETLE: r[d] = z || n != o; break;
      case OP64_SETG: r[d] = !z && n == o; break;
      case OP64_SETGE: r[d] = n == o; break;
      case OP64_SETB: r[d] = !c; break;
      case OP64_SETBE: r[d] = !c || z; break;
      case OP64_SETA: r[d] = c && !z; break;
      case OP64_SETAE: r[d] = c; break;
      case OP64_CLZ: r[d] = x ? (uint64_t)__builtin_clzll(x) : 64u; break;
      case OP64_CTZ: r[d] = x ? (uint64_t)__builtin_ctzll(x) : 64u; break;
      case OP64_POPCNT: r[d] = (uint64_t)__builtin_popcountll(x); break;
      case OP64_FMOV:
        if (d > 15 || a > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        f[d] = f[a]; break;
      case OP64_FLI:
        if (d > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        f[d] = fimm; break;
      case OP64_FADD: case OP64_FSUB: case OP64_FMUL: case OP64_FDIV:
        if (d > 15 || a > 15 || b > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        f[d] = op == OP64_FADD ? f[a] + f[b] :
               op == OP64_FSUB ? f[a] - f[b] :
               op == OP64_FMUL ? f[a] * f[b] : f[a] / f[b];
        break;
      case OP64_FNEG: case OP64_FABS: case OP64_FSQRT:
        if (d > 15 || a > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        f[d] = op == OP64_FNEG ? -f[a] : op == OP64_FABS ? fabs(f[a]) : sqrt(f[a]);
        break;
      case OP64_FCMP:
        if (a > 15 || b > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        o = isnan(f[a]) || isnan(f[b]); z = !o && f[a] == f[b];
        n = !o && f[a] < f[b]; c = !o && f[a] >= f[b]; break;
      case OP64_FSETE: case OP64_FSETNE: case OP64_FSETL:
      case OP64_FSETLE: case OP64_FSETG: case OP64_FSETGE: {
        if (a > 15 || b > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        bool result = op == OP64_FSETE ? f[a] == f[b] :
          op == OP64_FSETNE ? f[a] != f[b] :
          op == OP64_FSETL ? (!isnan(f[a]) && !isnan(f[b]) && f[a] < f[b]) :
          op == OP64_FSETLE ? (!isnan(f[a]) && !isnan(f[b]) && f[a] <= f[b]) :
          op == OP64_FSETG ? (!isnan(f[a]) && !isnan(f[b]) && f[a] > f[b]) :
          (!isnan(f[a]) && !isnan(f[b]) && f[a] >= f[b]);
        r[d] = result; break;
      }
      case OP64_ITOF:
        if (d > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        f[d] = (double)(int64_t)x; break;
      case OP64_UTOF:
        if (d > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        f[d] = (double)x; break;
      case OP64_FTOI:
        if (a > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        r[d] = ftoi64(f[a], false); break;
      case OP64_FTOU:
        if (a > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        r[d] = ftoi64(f[a], true); break;
      case OP64_FLD32: {
        if (d > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        uint32_t bits = vm_u32(vm, x + (uint64_t)imm); float q; memcpy(&q, &bits, 4); f[d] = q; break;
      }
      case OP64_FLD64: {
        if (d > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        uint64_t bits = vm_u64(vm, x + (uint64_t)imm); memcpy(&f[d], &bits, 8); break;
      }
      case OP64_FST32: {
        if (d > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        float q = (float)f[d]; uint32_t bits; memcpy(&bits, &q, 4); vm_set32(vm, x + (uint64_t)imm, bits); break;
      }
      case OP64_FST64: {
        if (d > 15) vm_fault(vm, NEMU_DATA, "bad floating register");
        uint64_t bits; memcpy(&bits, &f[d], 8); vm_set64(vm, x + (uint64_t)imm, bits); break;
      }
      default: vm_fault(vm, NEMU_DATA, "unsupported Thistle64 opcode %u", op);
    }
  }
  return vm->exit_code;
}

int nemu_check_image(const char *path, char *message, size_t message_size) {
  NemuImage image;
  int status = load_image(path, &image, message, message_size);
  if (status == NEMU_OK) {
    set_message(message, message_size, "THX%d %s, entry 0x%" PRIx64,
      image.bits, image.isa, image.entry);
    free_image(&image);
  }
  return status;
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
    if (!realpath(options->image_path, entry_host)) {
      fprintf(stderr, "nemunemu: %s: %s\n", options->image_path, strerror(errno));
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
    fprintf(stderr, "nemunemu: could not initialise guest runtime\n");
    free_image(&image);
    return status;
  }

#if defined(__wasm__)
  status = image.bits == 32 ? run32(&vm, &resolved) : run64(&vm, &resolved);
#else
  vm.trap_ready = 1;
  if (setjmp(vm.trap) != 0) {
    fprintf(stderr, "nemunemu: %s\n", vm.fault);
    status = vm.fault_status ? vm.fault_status : NEMU_SOFTWARE;
  } else {
    status = image.bits == 32 ? run32(&vm, &resolved) : run64(&vm, &resolved);
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
