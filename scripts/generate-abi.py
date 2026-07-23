#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "include" / "nemunemu" / "abi.h"
THISTLE = ROOT / "upstream" / "thistle"

def enum_names(path: Path, name: str) -> list[str]:
    text = path.read_text(encoding="utf-8")
    match = re.search(rf"export\s+enum\s+{re.escape(name)}\s*\{{(.*?)\}}", text, re.S)
    if not match:
        raise SystemExit(f"cannot find enum {name} in {path}")
    body = re.sub(r"//.*", "", match.group(1))
    names: list[str] = []
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        item = item.split("=", 1)[0].strip()
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", item):
            raise SystemExit(f"unsupported enum item {item!r} in {path}")
        names.append(item)
    return names

def ident(prefix: str, name: str) -> str:
    return prefix + re.sub(r"[^A-Za-z0-9]", "_", name).upper()

def wrapped(items: list[str], indent: str = "  ", width: int = 88) -> str:
    lines: list[str] = []
    current = indent
    for index, item in enumerate(items):
        token = item + ("," if index + 1 < len(items) else "")
        if len(current) + len(token) + 1 > width and current != indent:
            lines.append(current.rstrip())
            current = indent
        current += token + " "
    lines.append(current.rstrip())
    return "\n".join(lines)

def render_enum(tag: str, names: list[str], prefix: str) -> str:
    values = [f"{ident(prefix, name)} = {index}" for index, name in enumerate(names)]
    return f"enum {tag} {{\n{wrapped(values)}\n}};\n"

def render() -> str:
    sc = enum_names(THISTLE / "src" / "vm" / "vm.ts", "Sc")
    op32 = enum_names(THISTLE / "src" / "asm" / "isa.ts", "Op")
    op64 = enum_names(THISTLE / "src" / "asm" / "isa64.ts", "Op64")
    return (
        "#ifndef NEMUNEMU_ABI_H\n#define NEMUNEMU_ABI_H\n\n"
        "/* Generated from the pinned canonical Thistle sources. */\n\n"
        + render_enum("nemu_syscall", sc, "SC_") + "\n"
        + render_enum("nemu_op32", op32, "OP_") + "\n"
        + render_enum("nemu_op64", op64, "OP64_") + "\n"
        "#endif\n"
    )

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = render()
    if args.check:
        actual = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if actual != expected:
            raise SystemExit("generated ABI header is stale; run scripts/generate-abi.py")
        return 0
    OUT.write_text(expected, encoding="utf-8")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
