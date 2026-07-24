      at, at, sizeof(message) - 1u, at, sizeof(message) - 1u);
  } else {
    snprintf(sections, sizeof(sections),
      "{\"name\":\".text\",\"flg\":\"rx\",\"align\":16,\"size\":%zu,"
      "\"addr\":65536,\"at\":0,\"len\":%zu}",
      at, at);
  }

  char meta[1280];
  int hn = snprintf(meta, sizeof(meta),
    "{\"machine\":\"thistle64\",\"ver\":2,\"sec\":[%s],"
    "\"sym\":[],\"rel\":[],\"dbg\":[],\"ident\":[],\"entry\":65536,"
    "\"mem\":67108864,\"isa\":\"thistle\"}",
    sections);
  assert(hn > 0);

  size_t payload = at + (write_message ? sizeof(message) - 1u : 0u);
  uint8_t *payload_data = calloc(1, payload);
  assert(payload_data);
  memcpy(payload_data, text, at);
  if (write_message) memcpy(payload_data + at, message, sizeof(message) - 1u);

  size_t size = 16u + (size_t)hn + payload;
  uint8_t *out = calloc(1, size);
  assert(out);
  memcpy(out, "THX2", 4);
  wr32(out + 4, (uint32_t)hn);
  wr32(out + 8, (uint32_t)payload);
  memcpy(out + 16, meta, (size_t)hn);
  memcpy(out + 16 + (size_t)hn, payload_data, payload);
  wr32(out + 12, hash(out + 16, size - 16));
  write_all(path, out, size);
  free(payload_data);
  free(out);
}

static uint32_t rv_i(unsigned immediate, unsigned rs1, unsigned funct3,
                     unsigned rd, unsigned opcode) {
  return ((immediate & 0xfffu) << 20) | ((rs1 & 31u) << 15) |
         ((funct3 & 7u) << 12) | ((rd & 31u) << 7) | (opcode & 0x7fu);
}

static uint32_t rv_u(unsigned immediate, unsigned rd, unsigned opcode) {
  return (immediate & 0xfffff000u) | ((rd & 31u) << 7) | (opcode & 0x7fu);
}

static void make_rv64(const char *path, int code) {
  static const char message[] = "nemunemu rv64 write\n";
  uint32_t instructions[] = {
    rv_i(1u, 0u, 0u, 10u, 0x13u),
    rv_u(0x20000u, 11u, 0x37u),
    rv_i((unsigned)(sizeof(message) - 1u), 0u, 0u, 12u, 0x13u),
    rv_i(64u, 0u, 0u, 17u, 0x13u),
    UINT32_C(0x00000073),
    rv_i((unsigned)code, 0u, 0u, 10u, 0x13u),
    rv_i(93u, 0u, 0u, 17u, 0x13u),
    UINT32_C(0x00000073),
  };

  uint8_t text[sizeof(instructions)];
  for (size_t index = 0; index < sizeof(instructions) / sizeof(instructions[0]); ++index) {
    wr32(text + index * 4u, instructions[index]);
  }

  char meta[1280];
  int hn = snprintf(meta, sizeof(meta),
    "{\"machine\":\"thistle64\",\"ver\":2,\"sec\":["
    "{\"name\":\".text\",\"flg\":\"rx\",\"align\":4096,\"size\":%zu,"
    "\"addr\":65536,\"at\":0,\"len\":%zu},"
    "{\"name\":\".rodata\",\"flg\":\"r\",\"align\":4096,\"size\":%zu,"
    "\"addr\":131072,\"at\":%zu,\"len\":%zu}],"
    "\"sym\":[],\"rel\":[],\"dbg\":[],\"ident\":[],\"entry\":65536,"
    "\"mem\":67108864,\"isa\":\"rv64gc\"}",
    sizeof(text), sizeof(text), sizeof(message) - 1u,
    sizeof(text), sizeof(message) - 1u);
  assert(hn > 0);

  size_t payload = sizeof(text) + sizeof(message) - 1u;
  uint8_t *payload_data = calloc(1, payload);
  assert(payload_data);
  memcpy(payload_data, text, sizeof(text));
  memcpy(payload_data + sizeof(text), message, sizeof(message) - 1u);

  size_t size = 16u + (size_t)hn + payload;
  uint8_t *out = calloc(1, size);
  assert(out);
  memcpy(out, "THX2", 4);
  wr32(out + 4, (uint32_t)hn);
  wr32(out + 8, (uint32_t)payload);
  memcpy(out + 16, meta, (size_t)hn);
  memcpy(out + 16 + (size_t)hn, payload_data, payload);
  wr32(out + 12, hash(out + 16, size - 16u));
  write_all(path, out, size);
  free(payload_data);
  free(out);
}

static int run_file(const char *path) {
  char *argv[] = {(char *)path, NULL};
  nemu_run_options options = {
    .image_path = path,
    .entrypoint = NULL,
    .root_path = "/",
    .argc = 1,
    .argv = argv
  };
  return nemu_run(&options);
}

int main(void) {
  char *thx32 = temp_path("32");
  char *thx64 = temp_path("64");
  char *writer = temp_path("write");
