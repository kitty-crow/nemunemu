        r[13] = (int32_t)vm_u32(vm, (uint32_t)r[14]);
        r[14] = (int32_t)((uint32_t)r[14] + 4u); break;
      case OP_LD8U: r[d] = vm_u8(vm, ux + (uint32_t)imm); break;
      case OP_LD8S: r[d] = (int8_t)vm_u8(vm, ux + (uint32_t)imm); break;
      case OP_LD16U: r[d] = vm_u16(vm, ux + (uint32_t)imm); break;
      case OP_LD16S: r[d] = (int16_t)vm_u16(vm, ux + (uint32_t)imm); break;
      case OP_LD32: r[d] = (int32_t)vm_u32(vm, ux + (uint32_t)imm); break;
      case OP_ST8: vm_set8(vm, ux + (uint32_t)imm, (uint8_t)r[d]); break;
      case OP_ST16: vm_set16(vm, ux + (uint32_t)imm, (uint16_t)r[d]); break;
      case OP_ST32: vm_set32(vm, ux + (uint32_t)imm, (uint32_t)r[d]); break;
      case OP_SYS: {
        uint64_t aa[6] = {
          (uint32_t)r[0], (uint32_t)r[1], (uint32_t)r[2],
          (uint32_t)r[3], (uint32_t)r[4], (uint32_t)r[5]
        };
        r[0] = (int32_t)syscall_common(vm, 32, (uint32_t)imm, aa);
        break;
      }
      case OP_XCHG: { int32_t q = r[d]; r[d] = x; r[a] = q; break; }
      case OP_SEX8: r[d] = (int8_t)x; break;
      case OP_SEX16: r[d] = (int16_t)x; break;
      case OP_SETE: r[d] = z; break;
      case OP_SETNE: r[d] = !z; break;
      case OP_SETL: r[d] = n != o; break;
      case OP_SETLE: r[d] = z || n != o; break;
      case OP_SETG: r[d] = !z && n == o; break;
      case OP_SETGE: r[d] = n == o; break;
      case OP_SETB: r[d] = !c; break;
      case OP_SETBE: r[d] = !c || z; break;
      case OP_SETA: r[d] = c && !z; break;
      case OP_SETAE: r[d] = c; break;
      default: vm_fault(vm, NEMU_DATA, "unsupported Thistle32 opcode %u", op);
    }
  }
  return vm->exit_code;
}

static uint64_t ftoi64(double value, bool uns) {
  if (isnan(value)) return 0;
  if (uns) {
    if (value <= 0) return 0;
    if (value >= 18446744073709551615.0) return UINT64_MAX;
    return (uint64_t)value;
  }
  if (value <= -9223372036854775808.0) return UINT64_C(0x8000000000000000);
  if (value >= 9223372036854775807.0) return UINT64_C(0x7fffffffffffffff);
  return (uint64_t)(int64_t)value;
}

