        break;
      }
      case OP_TEST: z = (x & y) == 0; n = (x & y) < 0; c = false; o = false; break;
      case OP_JMP: pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_JE: if (z) pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_JNE: if (!z) pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_JL: if (n != o) pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_JLE: if (z || n != o) pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_JG: if (!z && n == o) pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_JGE: if (n == o) pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_JB: if (!c) pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_JBE: if (!c || z) pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_JA: if (c && !z) pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_JAE: if (c) pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_CALL:
        r[14] = (int32_t)((uint32_t)r[14] - 4u);
        vm_set32(vm, (uint32_t)r[14], pc);
        pc = (uint32_t)(pc + (uint32_t)imm); break;
      case OP_CALLR:
        r[14] = (int32_t)((uint32_t)r[14] - 4u);
        vm_set32(vm, (uint32_t)r[14], pc);
        pc = (uint32_t)r[d]; break;
      case OP_RET: {
        uint32_t ret = vm_u32(vm, (uint32_t)r[14]);
        r[14] = (int32_t)((uint32_t)r[14] + 4u);
        if (!ret) { vm->exit_code = r[0] & 0xff; vm->done = 1; }
        else pc = ret;
        break;
      }
      case OP_PUSH:
        r[14] = (int32_t)((uint32_t)r[14] - 4u);
        vm_set32(vm, (uint32_t)r[14], (uint32_t)r[d]); break;
      case OP_POP:
        r[d] = (int32_t)vm_u32(vm, (uint32_t)r[14]);
        r[14] = (int32_t)((uint32_t)r[14] + 4u); break;
      case OP_ENTER:
        r[14] = (int32_t)((uint32_t)r[14] - 4u);
        vm_set32(vm, (uint32_t)r[14], (uint32_t)r[13]);
        r[13] = r[14];
        r[14] = (int32_t)((uint32_t)r[14] - (uint32_t)imm);
        vm_check_memory(vm, (uint32_t)r[14], (uint32_t)imm, true);
        break;
      case OP_LEAVE:
        r[14] = r[13];
