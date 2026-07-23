# Development

NEMUNEMU is C17 so the same execution core can be compiled for ordinary
Linux and the Linux-WASM musl userspace.

Do not duplicate Thistle opcode or syscall values manually. Run:

```bash
python3 scripts/generate-abi.py
```

after changing the pinned Thistle revision, then commit the regenerated
header. CI runs the same command with `--check`.

Native changes must pass:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Changes touching parsing, memory, or execution must also pass ASan and
UBSan. Kernel and host-runtime code belongs in NERU, not this repository.
