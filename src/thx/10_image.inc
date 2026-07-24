static void free_image(NemuImage *image) {
  if (!image) return;
  for (size_t i = 0; i < image->section_count; ++i) {
    free(image->sections[i].name);
    free(image->sections[i].flags);
  }
  free(image->sections);
  free(image->isa);
  free(image->file);
  memset(image, 0, sizeof(*image));
}

static bool parse_section(Json *j, NemuSection *section) {
  if (!json_take(j, '{')) return false;
  bool have_name = false, have_flags = false, have_align = false;
  bool have_size = false, have_addr = false, have_at = false, have_len = false;
  for (;;) {
    if (json_take(j, '}')) break;
    char *key = json_string(j);
    if (!key || !json_take(j, ':')) { free(key); return false; }
    bool ok = true;
    if (strcmp(key, "name") == 0) {
      section->name = json_string(j); have_name = section->name != NULL; ok = have_name;
    } else if (strcmp(key, "flg") == 0) {
      section->flags = json_string(j); have_flags = section->flags != NULL; ok = have_flags;
    } else if (strcmp(key, "align") == 0) {
      ok = json_u64(j, &section->align); have_align = ok;
    } else if (strcmp(key, "size") == 0) {
      ok = json_u64(j, &section->size); have_size = ok;
    } else if (strcmp(key, "addr") == 0) {
      ok = json_u64(j, &section->addr); have_addr = ok;
    } else if (strcmp(key, "at") == 0) {
      ok = json_u64(j, &section->payload_at); have_at = ok;
    } else if (strcmp(key, "len") == 0) {
      ok = json_u64(j, &section->payload_len); have_len = ok;
    } else {
      ok = json_skip_value(j);
    }
    free(key);
    if (!ok) return false;
    if (json_take(j, '}')) break;
    if (!json_take(j, ',')) return false;
  }
  return have_name && have_flags && have_align && have_size &&
         have_addr && have_at && have_len;
}

static bool parse_sections(Json *j, NemuImage *image) {
  if (!json_take(j, '[')) return false;
  if (json_take(j, ']')) return true;
  for (;;) {
    if (image->section_count >= NEMU_MAX_SECTIONS) return false;
    NemuSection *next = realloc(
      image->sections, (image->section_count + 1u) * sizeof(*image->sections));
    if (!next) return false;
    image->sections = next;
    NemuSection *section = &image->sections[image->section_count];
    memset(section, 0, sizeof(*section));
    if (!parse_section(j, section)) return false;
    ++image->section_count;
    if (json_take(j, ']')) return true;
    if (!json_take(j, ',')) return false;
  }
}

static int parse_metadata(
  NemuImage *image, const char *text, size_t size, char *message, size_t message_size
) {
  Json j = {text, size, 0, 0};
  bool have_machine = false, have_ver = false, have_sections = false;
  bool have_entry = false, have_mem = false;
  char *machine = NULL;
  uint64_t version = 0;
  if (!json_take(&j, '{')) goto invalid;
  for (;;) {
    if (json_take(&j, '}')) break;
    char *key = json_string(&j);
    if (!key || !json_take(&j, ':')) { free(key); goto invalid; }
    bool ok = true;
    if (strcmp(key, "machine") == 0) {
      machine = json_string(&j); have_machine = machine != NULL; ok = have_machine;
    } else if (strcmp(key, "ver") == 0) {
      ok = json_u64(&j, &version); have_ver = ok;
    } else if (strcmp(key, "sec") == 0) {
      ok = parse_sections(&j, image); have_sections = ok;
    } else if (strcmp(key, "entry") == 0) {
      ok = json_u64(&j, &image->entry); have_entry = ok;
    } else if (strcmp(key, "mem") == 0) {
      ok = json_u64(&j, &image->mem_size); have_mem = ok;
    } else if (strcmp(key, "isa") == 0) {
      image->isa = json_string(&j); ok = image->isa != NULL;
    } else {
      ok = json_skip_value(&j);
    }
    free(key);
    if (!ok) goto invalid;
    if (json_take(&j, '}')) break;
    if (!json_take(&j, ',')) goto invalid;
  }
  json_ws(&j);
  if (j.pos != j.size || !have_machine || !have_ver || !have_sections ||
      !have_entry || !have_mem) goto invalid;
  if ((image->bits == 32 && (strcmp(machine, "thistle32") != 0 || version != 1)) ||
      (image->bits == 64 && (strcmp(machine, "thistle64") != 0 || version != 2))) {
    set_message(message, message_size, "Thistle magic, machine and version disagree");
    free(machine);
    return NEMU_DATA;
  }
  if (!image->isa) {
    image->isa = strdup("thistle");
    if (!image->isa) { free(machine); return NEMU_OSERR; }
  }
  bool supported_isa = strcmp(image->isa, "thistle") == 0 ||
    (image->bits == 64 && strcmp(image->isa, "rv64gc") == 0);
  if (!supported_isa) {
    set_message(message, message_size,
      "NEMUNEMU does not execute the %s instruction profile", image->isa);
    free(machine);
    return NEMU_UNAVAILABLE;
  }
  free(machine);
  return NEMU_OK;

invalid:
  free(machine);
  set_message(message, message_size, "invalid Thistle executable metadata");
  return NEMU_DATA;
}

static int read_file(const char *path, uint8_t **data, size_t *size, char *message, size_t message_size) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    set_message(message, message_size, "%s: %s", path, strerror(errno));
    return NEMU_NOINPUT;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    set_message(message, message_size, "%s: seek failed", path);
    fclose(file);
    return NEMU_OSERR;
  }
  long length = ftell(file);
  if (length < 0 || (unsigned long)length > SIZE_MAX) {
    set_message(message, message_size, "%s: invalid length", path);
    fclose(file);
    return NEMU_DATA;
  }
  rewind(file);
  uint8_t *bytes = malloc((size_t)length ? (size_t)length : 1u);
  if (!bytes) {
    fclose(file);
    return NEMU_OSERR;
  }
  if ((size_t)length && fread(bytes, 1, (size_t)length, file) != (size_t)length) {
    set_message(message, message_size, "%s: short read", path);
    free(bytes);
    fclose(file);
    return NEMU_OSERR;
  }
  fclose(file);
  *data = bytes;
  *size = (size_t)length;
  return NEMU_OK;
}

static int load_image(const char *path, NemuImage *image, char *message, size_t message_size) {
  memset(image, 0, sizeof(*image));
  int status = read_file(path, &image->file, &image->file_size, message, message_size);
  if (status != NEMU_OK) return status;
  if (image->file_size < 16u) {
    set_message(message, message_size, "truncated Thistle executable");
    status = NEMU_DATA;
    goto fail;
  }
  if (memcmp(image->file, "THX1", 4) == 0) image->bits = 32;
  else if (memcmp(image->file, "THX2", 4) == 0) image->bits = 64;
  else {
    set_message(message, message_size, "unknown Thistle executable magic");
    status = NEMU_DATA;
    goto fail;
  }
  uint32_t header_size = rd32(image->file + 4);
  uint32_t payload_size = rd32(image->file + 8);
  uint64_t total = 16ull + header_size + payload_size;
  if (total != image->file_size) {
    set_message(message, message_size, "bad Thistle executable length");
    status = NEMU_DATA;
    goto fail;
  }
  if (rd32(image->file + 12) != fnv1a(image->file + 16, image->file_size - 16u)) {
    set_message(message, message_size, "Thistle executable checksum mismatch");
    status = NEMU_DATA;
    goto fail;
  }
  image->payload_base = 16u + header_size;
  status = parse_metadata(
    image, (const char *)image->file + 16u, header_size, message, message_size);
  if (status != NEMU_OK) goto fail;
  if (image->mem_size < 1024u * 1024u ||
      image->mem_size > NEMU_MAX_LOGICAL_MEMORY) {
    set_message(message, message_size, "Thistle memory size is outside NEMUNEMU limits");
    status = NEMU_DATA;
    goto fail;
  }
  uint64_t highest = 0;
  for (size_t i = 0; i < image->section_count; ++i) {
    NemuSection *s = &image->sections[i];
    if (s->align == 0 || (s->align & (s->align - 1u)) != 0 ||
        s->payload_len > s->size || s->addr % s->align != 0) {
      set_message(message, message_size, "invalid section mapping: %s", s->name);
      status = NEMU_DATA;
      goto fail;
    }
    uint64_t end = 0, payload_end = 0;
    if (add_overflow_u64(s->addr, s->size, &end) || end > image->mem_size ||
        add_overflow_u64(s->payload_at, s->payload_len, &payload_end) ||
        payload_end > payload_size) {
      set_message(message, message_size, "section exceeds image bounds: %s", s->name);
      status = NEMU_DATA;
      goto fail;
    }
    if ((image->bits == 32 && s->addr < 0x1000u) ||
        (image->bits == 64 && s->addr < 0x10000u)) {
      set_message(message, message_size, "section maps below ABI floor: %s", s->name);
      status = NEMU_DATA;
      goto fail;
    }
    for (size_t k = 0; k < i; ++k) {
      uint64_t other_end = image->sections[k].addr + image->sections[k].size;
      if (s->addr < other_end && end > image->sections[k].addr) {
        set_message(message, message_size, "overlapping sections: %s", s->name);
        status = NEMU_DATA;
        goto fail;
      }
    }
    if (end > highest) highest = end;
  }
  bool entry_ok = false;
  for (size_t i = 0; i < image->section_count; ++i) {
    const NemuSection *s = &image->sections[i];
    if (strchr(s->flags, 'x') &&
        image->entry >= s->addr && image->entry < s->addr + s->size) {
      entry_ok = true;
      break;
    }
  }
  if (!entry_ok) {
    set_message(message, message_size, "entry point is not executable");
    status = NEMU_DATA;
    goto fail;
  }
  return NEMU_OK;

fail:
  free_image(image);
  return status;
}
