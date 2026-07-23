    "\"flg\":\"rx\",\"align\":8,\"size\":16,\"addr\":4096,\"at\":0,\"len\":16}],"
    "\"sym\":[],\"rel\":[],\"dbg\":[],\"ident\":[],\"entry\":4096,"
    "\"mem\":1048576,\"isa\":\"thistle\"}");
  assert(hn > 0);
  size_t size = 16u + (size_t)hn + sizeof(text);
  uint8_t *out = calloc(1, size);
  assert(out);
  memcpy(out, "THX1", 4);
  wr32(out + 4, (uint32_t)hn);
  wr32(out + 8, sizeof(text));
  memcpy(out + 16, meta, (size_t)hn);
  memcpy(out + 16 + (size_t)hn, text, sizeof(text));
  wr32(out + 12, hash(out + 16, size - 16));
  write_all(path, out, size);
  free(out);
}

static void make_thx64(const char *path, int code, int write_message) {
  static const char message[] = "nemunemu guest write\n";
  uint8_t text[128] = {0};
  size_t at = 0;
#define INS(OP,D,A,B,IMM) do { \
    text[at] = (OP); text[at+1] = (D); text[at+2] = (A); text[at+3] = (B); \
    wr64(text + at + 8, (uint64_t)(IMM)); at += 16; \
  } while (0)
  if (write_message) {
    INS(OP64_LI, 0, 0, 0, 1);
    INS(OP64_LI, 1, 0, 0, 0x20000);
    INS(OP64_LI, 2, 0, 0, sizeof(message) - 1);
    INS(OP64_SYS, 0, 0, 0, SC_WRITE);
  }
  INS(OP64_LI, 0, 0, 0, code);
  INS(OP64_HALT, 0, 0, 0, 0);
#undef INS

  char sections[768];
  if (write_message) {
    snprintf(sections, sizeof(sections),
      "{\"name\":\".text\",\"flg\":\"rx\",\"align\":16,\"size\":%zu,"
      "\"addr\":65536,\"at\":0,\"len\":%zu},"
      "{\"name\":\".rodata\",\"flg\":\"r\",\"align\":16,\"size\":%zu,"
      "\"addr\":131072,\"at\":%zu,\"len\":%zu}",
