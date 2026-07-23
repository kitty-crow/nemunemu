# Architecture

NEMUNEMU is an ordinary Linux userspace compatibility layer. NERU builds
one Linux image ahead of time, installs NEMUNEMU in it and boots that same
image from both terminal and browser hosts.

```text
mikuOS userland tree
        |
        |  NERU ahead-of-time image build
        v
Linux initramfs containing the unchanged userland contract
        |
        v
NEMUNEMU compatibility layer
        |                         |
        | THX1 / THX2             | #!thistle:<command>
        v                         v
Thistle interpreter        Linux compatibility applet
        \_________________________/
                    |
                    v
             NERU Linux kernel
```

NEMUNEMU owns:

- THX validation and Thistle32/Thistle64 execution;
- translation of the Thistle syscall ABI to Linux;
- resolution of packaged mikuOS command contracts;
- entering the packaged userland root and starting its shell contract.

NERU owns:

- selecting and building Linux-WASM;
- staging the mikuOS userland without modifying its source tree;
- preparing Linux-executable wrappers in the packaged copy;
- initramfs construction and CLI/browser kernel integration.

The compatibility layer does not create a second mikuOS implementation.
The normal Thistle and Teto roots and the NERU image all originate from the
same mikuOS userland tree.

`uname` inside NERU reports `Linux` and the actual built Linux release.
