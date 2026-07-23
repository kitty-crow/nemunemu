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

