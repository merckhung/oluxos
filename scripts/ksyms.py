#!/usr/bin/env python3
"""Generate the kernel symbol table used for symbolised backtraces.

Reads `nm -n` output on stdin and writes an assembly file defining
ksyms_table (sorted {addr, name offset} pairs), ksyms_count and
ksyms_names. `--empty` emits an empty table for the first link pass.
"""
import sys


def emit(out, syms):
    names = bytearray()
    lines = ['  .section .ksyms, "a"', "  .balign 8", "  .globl ksyms_table, ksyms_count, ksyms_names"]
    lines.append("ksyms_count:")
    lines.append(f"  .quad {len(syms)}")
    lines.append("ksyms_table:")
    for addr, name in syms:
        lines.append(f"  .quad {addr:#x}")
        lines.append(f"  .long {len(names)}, 0")
        names += name.encode() + b"\0"
    lines.append("ksyms_names:")
    for i in range(0, len(names), 16):
        chunk = names[i : i + 16]
        lines.append("  .byte " + ",".join(str(b) for b in chunk))
    lines.append("  .byte 0")
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    args = sys.argv[1:]
    if args[0] == "--empty":
        emit(args[1], [])
        return
    syms = []
    for line in sys.stdin:
        parts = line.split()
        if len(parts) != 3:
            continue
        addr, kind, name = parts
        if kind not in "tTwW" or name.startswith("$") or name.startswith(".L"):
            continue
        syms.append((int(addr, 16), name))
    syms.sort()
    emit(args[0], syms)


if __name__ == "__main__":
    main()
