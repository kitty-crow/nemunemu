static int run32(NemuVm *vm, const nemu_run_options *options) {
  int32_t r[16] = {0};
  bool z = false, n = false, c = false, o = false;
  uint32_t pc = (uint32_t)vm->image->entry;
  uint64_t sp64 = vm->memory.size - 16u;
  size_t ec = env_count();
  uint32_t *env_ptrs = calloc(ec ? ec : 1u, sizeof(*env_ptrs));
  uint32_t *arg_ptrs = calloc(options->argc ? (size_t)options->argc : 1u, sizeof(*arg_ptrs));
  if (!env_ptrs || !arg_ptrs) vm_fault(vm, NEMU_OSERR, "out of host memory");

  for (size_t i = 0; i < ec; ++i) env_ptrs[i] = (uint32_t)stack_put(vm, &sp64, environ[i]);
  for (int i = 0; i < options->argc; ++i) arg_ptrs[i] = (uint32_t)stack_put(vm, &sp64, options->argv[i]);
  sp64 &= ~UINT64_C(3);
  sp64 -= (ec + 1u) * 4u;
  uint32_t env_vec = (uint32_t)sp64;
  for (size_t i = 0; i < ec; ++i) vm_set32(vm, env_vec + i * 4u, env_ptrs[i]);
  vm_set32(vm, env_vec + ec * 4u, 0);
  sp64 -= ((size_t)options->argc + 1u) * 4u;
  uint32_t arg_vec = (uint32_t)sp64;
  for (int i = 0; i < options->argc; ++i) vm_set32(vm, arg_vec + (size_t)i * 4u, arg_ptrs[i]);
  vm_set32(vm, arg_vec + (size_t)options->argc * 4u, 0);
  sp64 -= 4u;
  vm_set32(vm, sp64, 0);
  free(env_ptrs); free(arg_ptrs);
  r[0] = options->argc; r[1] = (int32_t)arg_vec;
  r[2] = (int32_t)ec; r[3] = (int32_t)env_vec;
  r[13] = 0; r[14] = (int32_t)sp64; r[15] = 0;

  while (!vm->done) {
    if (vm->fuel && --vm->fuel == 0) vm_fault(vm, NEMU_DATA, "Thistle32 instruction limit reached");
    vm_check_exec(vm, pc, 8);
    uint8_t ins[8]; memory_read_raw(vm, pc, ins, sizeof(ins));
    uint8_t op = ins[0], d = ins[1], a = ins[2], b = ins[3];
    int32_t imm = (int32_t)rd32(ins + 4);
    if (d > 15 || a > 15 || b > 15 || op > OP_SETAE) {
      vm_fault(vm, NEMU_DATA, "bad Thistle32 instruction at 0x%08" PRIx32, pc);
    }
    pc += 8u;
    int32_t x = r[a], y = r[b];
    uint32_t ux = (uint32_t)x, uy = (uint32_t)y;
    switch (op) {
      case OP_NOP: break;
      case OP_HALT: vm->exit_code = r[0] & 0xff; vm->done = 1; break;
      case OP_MOV: r[d] = x; break;
