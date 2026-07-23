      case OP_LI: r[d] = imm; break;
      case OP_ADD: r[d] = (int32_t)(ux + uy); break;
      case OP_ADDI: r[d] = (int32_t)(ux + (uint32_t)imm); break;
      case OP_SUB: r[d] = (int32_t)(ux - uy); break;
      case OP_SUBI: r[d] = (int32_t)(ux - (uint32_t)imm); break;
      case OP_MUL: r[d] = (int32_t)((uint32_t)((uint64_t)ux * uy)); break;
      case OP_MULI: r[d] = (int32_t)((uint32_t)((uint64_t)ux * (uint32_t)imm)); break;
      case OP_DIV:
        if (y == 0) vm_fault(vm, NEMU_DATA, "integer division by zero");
        r[d] = (x == INT32_MIN && y == -1) ? INT32_MIN : x / y; break;
      case OP_DIVU:
        if (uy == 0) vm_fault(vm, NEMU_DATA, "integer division by zero");
        r[d] = (int32_t)(ux / uy); break;
      case OP_MOD:
        if (y == 0) vm_fault(vm, NEMU_DATA, "integer division by zero");
        r[d] = (x == INT32_MIN && y == -1) ? 0 : x % y; break;
      case OP_MODU:
        if (uy == 0) vm_fault(vm, NEMU_DATA, "integer division by zero");
        r[d] = (int32_t)(ux % uy); break;
      case OP_AND: r[d] = x & y; break;
      case OP_ANDI: r[d] = x & imm; break;
      case OP_OR: r[d] = x | y; break;
      case OP_ORI: r[d] = x | imm; break;
      case OP_XOR: r[d] = x ^ y; break;
      case OP_XORI: r[d] = x ^ imm; break;
      case OP_NOT: r[d] = ~x; break;
      case OP_NEG: r[d] = (int32_t)(0u - ux); break;
      case OP_SHL: r[d] = (int32_t)(ux << (uy & 31u)); break;
      case OP_SHLI: r[d] = (int32_t)(ux << ((uint32_t)imm & 31u)); break;
      case OP_SHR: r[d] = (int32_t)(ux >> (uy & 31u)); break;
      case OP_SHRI: r[d] = (int32_t)(ux >> ((uint32_t)imm & 31u)); break;
      case OP_SAR: r[d] = x >> (uy & 31u); break;
      case OP_SARI: r[d] = x >> ((uint32_t)imm & 31u); break;
      case OP_CMP:
      case OP_CMPI: {
        int32_t rhs = op == OP_CMP ? y : imm;
        uint32_t ur = ux - (uint32_t)rhs;
        int32_t sr = (int32_t)ur;
        z = sr == 0; n = sr < 0; c = ux >= (uint32_t)rhs;
        o = (((ux ^ (uint32_t)rhs) & (ux ^ ur) & UINT32_C(0x80000000)) != 0);
