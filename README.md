# NEMUNEMU

NEMUNEMU: **NEMUnemu is Not an EMUlator**.

NEMUNEMU is the Linux compatibility layer used by NERU. It lets the same
mikuOS/Thistle userland run after NERU has built it into a Linux image.

It provides two execution paths inside that single packaged userland:

- canonical THX1 and THX2 programmes are interpreted directly;
- existing `#!thistle:<command>` rescue contracts are translated to their
  Linux compatibility applets.

NERU owns ahead-of-time image construction, Linux-WASM, the kernel,
initramfs assembly and host integration. NEMUNEMU does not build images and
does not contain mikuOS-specific source trees.

## Native build and test

```bash
git submodule update --init --recursive
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Linux-WASM build

After NERU has built its pinned Linux-WASM toolchain:

```bash
LINUX_WASM_ROOT=/path/to/neru/vendor/linux-wasm \
LW_WORKSPACE=/path/to/linux-wasm/workspace \
./scripts/build-linux-wasm.sh
```

The output is a Linux-WASM userspace executable that NERU installs both as
Linux PID 1 and inside the packaged mikuOS root.

## Runtime modes

```text
nemunemu --shell ROOT
nemunemu --marker WRAPPER [ARG ...]
nemunemu --thx-wrapper WRAPPER [ARG ...]
nemunemu --image PROGRAMME.thx [-- ARG ...]
```

Inside a NERU boot, `uname` is the real Linux `uname`; NEMUNEMU never
replaces the kernel identity.
