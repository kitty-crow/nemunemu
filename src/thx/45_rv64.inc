static uint64_t rv64_sext(uint64_t value, unsigned bits) {
  if (bits >= 64u) return value;
  const uint64_t mask = (UINT64_C(1) << bits) - 1u;
  const uint64_t sign = UINT64_C(1) << (bits - 1u);
  value &= mask;
  return (value ^ sign) - sign;
}

static uint64_t rv64_word(uint32_t value) {
  return (uint64_t)(int64_t)(int32_t)value;
}

static uint64_t rv64_imm_i(uint32_t instruction) {
  return rv64_sext(instruction >> 20, 12u);
}

static uint64_t rv64_imm_s(uint32_t instruction) {
  uint64_t value = ((uint64_t)(instruction >> 25) << 5) |
                   ((instruction >> 7) & 0x1fu);
  return rv64_sext(value, 12u);
}

static uint64_t rv64_imm_b(uint32_t instruction) {
  uint64_t value = ((uint64_t)(instruction >> 31) << 12) |
                   ((uint64_t)(instruction >> 7 & 1u) << 11) |
                   ((uint64_t)(instruction >> 25 & 0x3fu) << 5) |
                   ((instruction >> 8) & 0x0fu) << 1;
  return rv64_sext(value, 13u);
}

static uint64_t rv64_imm_j(uint32_t instruction) {
  uint64_t value = ((uint64_t)(instruction >> 31) << 20) |
                   ((uint64_t)(instruction >> 12 & 0xffu) << 12) |
                   ((uint64_t)(instruction >> 20 & 1u) << 11) |
                   ((uint64_t)(instruction >> 21 & 0x3ffu) << 1);
  return rv64_sext(value, 21u);
}

static void rv64_set(uint64_t registers[32], unsigned index, uint64_t value) {
  if (index != 0u) registers[index] = value;
}

static int64_t rv64_host_result(int64_t result) {
  return result < 0 ? -(int64_t)errno : result;
}

static int64_t rv64_linux_syscall(NemuVm *vm, uint64_t registers[32]) {
  const uint64_t number = registers[17];
  const uint64_t a0 = registers[10];
  const uint64_t a1 = registers[11];
  const uint64_t a2 = registers[12];
  const uint64_t a3 = registers[13];

  switch (number) {
    case 56: { /* openat */
      if ((int64_t)a0 != -100 && (char)vm_u8(vm, a1) != '/') return -ENOSYS;
      char *guest = vm_string(vm, a1);
      char path[NEMU_MAX_PATH];
      int translated = guest_path(vm, guest, path, sizeof(path));
      free(guest);
      if (translated < 0) return -errno;
      int flags = host_open_flags(a2);
      if (flags < 0) return -errno;
      int fd = open(path, flags, (mode_t)a3);
      return rv64_host_result(fd);
    }

    case 57: /* close */
      return rv64_host_result(close((int)a0));

    case 62: { /* lseek */
      off_t result = lseek((int)a0, (off_t)a1, (int)a2);
      return result < 0 ? -(int64_t)errno : (int64_t)result;
    }

    case 63: { /* read */
      if (a2 > NEMU_MAX_IO || a2 > SIZE_MAX) return -EINVAL;
      size_t size = (size_t)a2;
      vm_check_memory(vm, a1, size, true);
      uint8_t *buffer = malloc(size ? size : 1u);
      if (!buffer) return -ENOMEM;
      ssize_t result = read((int)a0, buffer, size);
      if (result > 0) memory_write_raw(vm, a1, buffer, (size_t)result);
      int64_t out = result < 0 ? -(int64_t)errno : (int64_t)result;
      free(buffer);
      return out;
    }

    case 64: { /* write */
      if (a2 > NEMU_MAX_IO || a2 > SIZE_MAX) return -EINVAL;
      size_t size = (size_t)a2;
      uint8_t *buffer = malloc(size ? size : 1u);
      if (!buffer) return -ENOMEM;
      vm_read(vm, a1, buffer, size);
      ssize_t result = write((int)a0, buffer, size);
      int64_t out = result < 0 ? -(int64_t)errno : (int64_t)result;
      free(buffer);
      return out;
    }

    case 93:  /* exit */
    case 94:  /* exit_group */
      vm->exit_code = (int)(a0 & 0xffu);
      vm->done = 1;
      return 0;

    case 113: { /* clock_gettime */
      struct timespec now;
      if (clock_gettime((clockid_t)a0, &now) < 0) return -errno;
      vm_set64(vm, a1, (uint64_t)now.tv_sec);
      vm_set64(vm, a1 + 8u, (uint64_t)now.tv_nsec);
      return 0;
    }

    case 160: { /* uname */
      struct utsname identity;
      if (uname(&identity) < 0) return -errno;
      char fields[6][65];
      memset(fields, 0, sizeof(fields));
      (void)snprintf(fields[0], sizeof(fields[0]), "%s", identity.sysname);
      (void)snprintf(fields[1], sizeof(fields[1]), "%s", identity.nodename);
      (void)snprintf(fields[2], sizeof(fields[2]), "%s", identity.release);
      (void)snprintf(fields[3], sizeof(fields[3]), "%s", identity.version);
      (void)snprintf(fields[4], sizeof(fields[4]), "%s", identity.machine);
#if defined(_GNU_SOURCE)
      (void)snprintf(fields[5], sizeof(fields[5]), "%s", identity.domainname);
#endif
      vm_write(vm, a0, fields, sizeof(fields));
      return 0;
    }

    case 172: return getpid();
    case 173: return getppid();
    case 174: return getuid();
    case 175: return geteuid();
    case 176: return getgid();
    case 177: return getegid();

    case 214: /* brk */
      if (a0 == 0u) return (int64_t)vm->brk;
      if (a0 >= vm->floor && a0 < vm->stack_at) vm->brk = a0;
      return (int64_t)vm->brk;

    case 278: { /* getrandom */
      if (a1 > NEMU_MAX_IO || a1 > SIZE_MAX) return -EINVAL;
      size_t size = (size_t)a1;
      uint8_t *buffer = malloc(size ? size : 1u);
      if (!buffer) return -ENOMEM;
      int result = random_bytes(buffer, size);
      if (result == 0) vm_write(vm, a0, buffer, size);
      int64_t out = result < 0 ? -(int64_t)errno : (int64_t)size;
      free(buffer);
      return out;
    }

    default:
      return -ENOSYS;
  }
}

static uint64_t rv64_stack(NemuVm *vm, const nemu_run_options *options, uint64_t registers[32]) {
  if (options->argc < 0 || (unsigned)options->argc > NEMU_MAX_ARGS) {
    vm_fault(vm, NEMU_USAGE, "too many RV64 arguments");
  }
  size_t environment_count = env_count();
  if (environment_count > NEMU_MAX_ARGS) vm_fault(vm, NEMU_USAGE, "too many environment entries");

  uint64_t *arguments = calloc(options->argc ? (size_t)options->argc : 1u, sizeof(*arguments));
  uint64_t *environment = calloc(environment_count ? environment_count : 1u, sizeof(*environment));
  if (!arguments || !environment) vm_fault(vm, NEMU_OSERR, "out of host memory");

  uint64_t sp = vm->memory.size - 16u;
  for (size_t index = 0; index < environment_count; ++index) {
    environment[index] = stack_put(vm, &sp, environ[index]);
  }
  for (int index = 0; index < options->argc; ++index) {
    arguments[index] = stack_put(vm, &sp, options->argv[index]);
  }
  sp &= ~UINT64_C(15);

  size_t words = 1u + (size_t)options->argc + 1u + environment_count + 1u + 2u;
  if ((words & 1u) != 0u) {
    sp -= 8u;
    vm_set64(vm, sp, 0u);
  }

  sp -= 16u; /* AT_NULL, 0 */
  vm_set64(vm, sp, 0u);
  vm_set64(vm, sp + 8u, 0u);

  sp -= 8u;
  vm_set64(vm, sp, 0u);
  for (size_t index = environment_count; index > 0u; --index) {
    sp -= 8u;
    vm_set64(vm, sp, environment[index - 1u]);
  }

  sp -= 8u;
  vm_set64(vm, sp, 0u);
  for (size_t index = (size_t)options->argc; index > 0u; --index) {
    sp -= 8u;
    vm_set64(vm, sp, arguments[index - 1u]);
  }
  uint64_t argv = sp;

  sp -= 8u;
  vm_set64(vm, sp, (uint64_t)options->argc);
  if ((sp & 15u) != 0u) vm_fault(vm, NEMU_DATA, "RV64 stack is not aligned");

  free(arguments);
  free(environment);
  registers[2] = sp;
  registers[10] = (uint64_t)options->argc;
  registers[11] = argv;
  return sp;
}

static uint64_t rv64_div_signed(uint64_t left, uint64_t right) {
  int64_t a = (int64_t)left;
  int64_t b = (int64_t)right;
  if (b == 0) return UINT64_MAX;
  if (a == INT64_MIN && b == -1) return (uint64_t)INT64_MIN;
  return (uint64_t)(a / b);
}

static uint64_t rv64_rem_signed(uint64_t left, uint64_t right) {
  int64_t a = (int64_t)left;
  int64_t b = (int64_t)right;
  if (b == 0) return left;
  if (a == INT64_MIN && b == -1) return 0u;
  return (uint64_t)(a % b);
}

static uint64_t rv64_div_word(uint64_t left, uint64_t right, bool is_unsigned) {
  if (is_unsigned) {
    uint32_t a = (uint32_t)left;
    uint32_t b = (uint32_t)right;
    return rv64_word(b == 0u ? UINT32_MAX : a / b);
  }
  int32_t a = (int32_t)left;
  int32_t b = (int32_t)right;
  if (b == 0) return rv64_word(UINT32_MAX);
  if (a == INT32_MIN && b == -1) return rv64_word((uint32_t)INT32_MIN);
  return rv64_word((uint32_t)(a / b));
}

static uint64_t rv64_rem_word(uint64_t left, uint64_t right, bool is_unsigned) {
  if (is_unsigned) {
    uint32_t a = (uint32_t)left;
    uint32_t b = (uint32_t)right;
    return rv64_word(b == 0u ? a : a % b);
  }
  int32_t a = (int32_t)left;
  int32_t b = (int32_t)right;
  if (b == 0) return rv64_word((uint32_t)a);
  if (a == INT32_MIN && b == -1) return 0u;
  return rv64_word((uint32_t)(a % b));
}

static int run_rv64(NemuVm *vm, const nemu_run_options *options) {
  uint64_t registers[32] = {0};
  uint64_t pc = vm->image->entry;
  (void)rv64_stack(vm, options, registers);

  while (!vm->done) {
    if (vm->fuel && --vm->fuel == 0u) vm_fault(vm, NEMU_DATA, "RV64 instruction limit reached");
    vm_check_exec(vm, pc, 2u);
    uint16_t half = vm_u16(vm, pc);
    if ((half & 3u) != 3u) {
      vm_fault(vm, NEMU_UNAVAILABLE,
        "compressed RV64GC instruction is not implemented at 0x%" PRIx64, pc);
    }
    vm_check_exec(vm, pc, 4u);
    uint64_t at = pc;
    uint32_t instruction = vm_u32(vm, pc);
    pc += 4u;

    unsigned opcode = instruction & 0x7fu;
    unsigned rd = instruction >> 7 & 31u;
    unsigned funct3 = instruction >> 12 & 7u;
    unsigned rs1 = instruction >> 15 & 31u;
    unsigned rs2 = instruction >> 20 & 31u;
    unsigned funct7 = instruction >> 25;
    uint64_t left = registers[rs1];
    uint64_t right = registers[rs2];

    switch (opcode) {
      case 0x03: { /* loads */
        uint64_t address = left + rv64_imm_i(instruction);
        switch (funct3) {
          case 0: rv64_set(registers, rd, (uint64_t)(int64_t)(int8_t)vm_u8(vm, address)); break;
          case 1: rv64_set(registers, rd, (uint64_t)(int64_t)(int16_t)vm_u16(vm, address)); break;
          case 2: rv64_set(registers, rd, (uint64_t)(int64_t)(int32_t)vm_u32(vm, address)); break;
          case 3: rv64_set(registers, rd, vm_u64(vm, address)); break;
          case 4: rv64_set(registers, rd, vm_u8(vm, address)); break;
          case 5: rv64_set(registers, rd, vm_u16(vm, address)); break;
          case 6: rv64_set(registers, rd, vm_u32(vm, address)); break;
          default: vm_fault(vm, NEMU_UNAVAILABLE, "unsupported RV64 load at 0x%" PRIx64, at);
        }
        break;
      }

      case 0x0f: /* fence */
        break;

      case 0x13: { /* integer immediate */
        uint64_t immediate = rv64_imm_i(instruction);
        switch (funct3) {
          case 0: rv64_set(registers, rd, left + immediate); break;
          case 2: rv64_set(registers, rd, (int64_t)left < (int64_t)immediate); break;
          case 3: rv64_set(registers, rd, left < immediate); break;
          case 4: rv64_set(registers, rd, left ^ immediate); break;
          case 6: rv64_set(registers, rd, left | immediate); break;
          case 7: rv64_set(registers, rd, left & immediate); break;
          case 1:
            if ((instruction >> 26) != 0u) vm_fault(vm, NEMU_DATA, "invalid RV64 SLLI");
            rv64_set(registers, rd, left << (instruction >> 20 & 63u));
            break;
          case 5: {
            unsigned mode = instruction >> 26;
            unsigned shift = instruction >> 20 & 63u;
            if (mode == 0u) rv64_set(registers, rd, left >> shift);
            else if (mode == 0x10u) rv64_set(registers, rd, (uint64_t)((int64_t)left >> shift));
            else vm_fault(vm, NEMU_DATA, "invalid RV64 shift-immediate");
            break;
          }
          default: vm_fault(vm, NEMU_DATA, "invalid RV64 immediate instruction");
        }
        break;
      }

      case 0x17: /* auipc */
        rv64_set(registers, rd, at + rv64_sext(instruction & UINT32_C(0xfffff000), 32u));
        break;

      case 0x1b: { /* integer immediate word */
        unsigned shift = instruction >> 20 & 31u;
        if (funct3 == 0u) {
          rv64_set(registers, rd, rv64_word((uint32_t)(left + rv64_imm_i(instruction))));
        } else if (funct3 == 1u && funct7 == 0u) {
          rv64_set(registers, rd, rv64_word((uint32_t)left << shift));
        } else if (funct3 == 5u && funct7 == 0u) {
          rv64_set(registers, rd, rv64_word((uint32_t)left >> shift));
        } else if (funct3 == 5u && funct7 == 0x20u) {
          rv64_set(registers, rd, rv64_word((uint32_t)((int32_t)left >> shift)));
        } else {
          vm_fault(vm, NEMU_UNAVAILABLE, "unsupported RV64 word-immediate at 0x%" PRIx64, at);
        }
        break;
      }

      case 0x23: { /* stores */
        uint64_t address = left + rv64_imm_s(instruction);
        switch (funct3) {
          case 0: vm_set8(vm, address, (uint8_t)right); break;
          case 1: vm_set16(vm, address, (uint16_t)right); break;
          case 2: vm_set32(vm, address, (uint32_t)right); break;
          case 3: vm_set64(vm, address, right); break;
          default: vm_fault(vm, NEMU_UNAVAILABLE, "unsupported RV64 store at 0x%" PRIx64, at);
        }
        break;
      }

      case 0x33: { /* integer register */
        uint64_t result = 0u;
        if (funct7 == 0u) {
          switch (funct3) {
            case 0: result = left + right; break;
            case 1: result = left << (right & 63u); break;
            case 2: result = (int64_t)left < (int64_t)right; break;
            case 3: result = left < right; break;
            case 4: result = left ^ right; break;
            case 5: result = left >> (right & 63u); break;
            case 6: result = left | right; break;
            case 7: result = left & right; break;
            default: vm_fault(vm, NEMU_DATA, "invalid RV64 register instruction");
          }
        } else if (funct7 == 0x20u) {
          if (funct3 == 0u) result = left - right;
          else if (funct3 == 5u) result = (uint64_t)((int64_t)left >> (right & 63u));
          else vm_fault(vm, NEMU_DATA, "invalid RV64 subtract/shift instruction");
        } else if (funct7 == 1u) {
          switch (funct3) {
            case 0: result = left * right; break;
            case 4: result = rv64_div_signed(left, right); break;
            case 5: result = right == 0u ? UINT64_MAX : left / right; break;
            case 6: result = rv64_rem_signed(left, right); break;
            case 7: result = right == 0u ? left : left % right; break;
            default: vm_fault(vm, NEMU_UNAVAILABLE,
              "RV64 multiply-high instruction is not implemented at 0x%" PRIx64, at);
          }
        } else {
          vm_fault(vm, NEMU_UNAVAILABLE, "unsupported RV64 register extension at 0x%" PRIx64, at);
        }
        rv64_set(registers, rd, result);
        break;
      }

      case 0x37: /* lui */
        rv64_set(registers, rd, rv64_sext(instruction & UINT32_C(0xfffff000), 32u));
        break;

      case 0x3b: { /* integer register word */
        uint64_t result;
        unsigned shift = (unsigned)(right & 31u);
        if (funct7 == 0u && funct3 == 0u) result = rv64_word((uint32_t)left + (uint32_t)right);
        else if (funct7 == 0x20u && funct3 == 0u) result = rv64_word((uint32_t)left - (uint32_t)right);
        else if (funct7 == 0u && funct3 == 1u) result = rv64_word((uint32_t)left << shift);
        else if (funct7 == 0u && funct3 == 5u) result = rv64_word((uint32_t)left >> shift);
        else if (funct7 == 0x20u && funct3 == 5u) result = rv64_word((uint32_t)((int32_t)left >> shift));
        else if (funct7 == 1u && funct3 == 0u) result = rv64_word((uint32_t)left * (uint32_t)right);
        else if (funct7 == 1u && funct3 == 4u) result = rv64_div_word(left, right, false);
        else if (funct7 == 1u && funct3 == 5u) result = rv64_div_word(left, right, true);
        else if (funct7 == 1u && funct3 == 6u) result = rv64_rem_word(left, right, false);
        else if (funct7 == 1u && funct3 == 7u) result = rv64_rem_word(left, right, true);
        else vm_fault(vm, NEMU_UNAVAILABLE, "unsupported RV64 word operation at 0x%" PRIx64, at);
        rv64_set(registers, rd, result);
        break;
      }

      case 0x63: { /* branches */
        bool take;
        switch (funct3) {
          case 0: take = left == right; break;
          case 1: take = left != right; break;
          case 4: take = (int64_t)left < (int64_t)right; break;
          case 5: take = (int64_t)left >= (int64_t)right; break;
          case 6: take = left < right; break;
          case 7: take = left >= right; break;
          default: vm_fault(vm, NEMU_DATA, "invalid RV64 branch");
        }
        if (take) pc = at + rv64_imm_b(instruction);
        break;
      }

      case 0x67: /* jalr */
        if (funct3 != 0u) vm_fault(vm, NEMU_DATA, "invalid RV64 JALR");
        rv64_set(registers, rd, pc);
        pc = (left + rv64_imm_i(instruction)) & ~UINT64_C(1);
        break;

      case 0x6f: /* jal */
        rv64_set(registers, rd, pc);
        pc = at + rv64_imm_j(instruction);
        break;

      case 0x73: /* system */
        if (instruction == UINT32_C(0x00000073)) {
          registers[10] = (uint64_t)rv64_linux_syscall(vm, registers);
        } else if (instruction == UINT32_C(0x00100073)) {
          vm_fault(vm, NEMU_DATA, "RV64 breakpoint at 0x%" PRIx64, at);
        } else {
          vm_fault(vm, NEMU_UNAVAILABLE, "RV64 CSR instruction is not implemented at 0x%" PRIx64, at);
        }
        break;

      default:
        vm_fault(vm, NEMU_UNAVAILABLE,
          "unsupported RV64 opcode 0x%02x at 0x%" PRIx64, opcode, at);
    }

    registers[0] = 0u;
  }

  return vm->exit_code;
}
