#!/usr/bin/env python3
"""Print the system-call table of kernel/syscall.c as Markdown (the list in
docs/SYSCALLS.md is generated with this; rerun it after adding a call):

    scripts/syscall-table.py > /tmp/t.md
"""
import os
import re

NOTES = {
    "clone3": "returns ENOSYS; musl falls back to clone",
    "rseq": "returns ENOSYS (restartable sequences not supported)",
    "msync": "no-op: no file-backed shared mappings are written back",
    "munlock": "no-op (memory is never swapped)",
    "munlockall": "no-op (memory is never swapped)",
    "fadvise64": "accepted and ignored",
    "flock": "validates the descriptor; locks are advisory no-ops",
    "fallocate": "mode 0 only",
    "capget": "root has every capability, others none",
    "capset": "accepted for root",
    "syslog": "reads the kernel log (dmesg)",
    "adjtimex": "offset, frequency and status (NTP clock discipline)",
    "reboot": "RESTART, POWER_OFF, HALT, CAD_ON/OFF",
}


def main():
    src = open(os.path.join(os.path.dirname(__file__), "..", "kernel", "syscall.c")).read()
    linux = re.search(r"#define SYSCALLS\(X\)(.*?)\n\n", src, re.S).group(1)
    olux = re.search(r"#define OLUX_SYSCALLS\(X\)(.*?)\n", src, re.S).group(1)
    print("| Nr | Name | Notes |")
    print("|---:|------|-------|")
    for n, name in re.findall(r"X\((\d+), (\w+)\)", linux):
        print("| %s | `%s` | %s |" % (n, name, NOTES.get(name, "")))
    for n, name in re.findall(r"X\((\d+), (\w+)\)", olux):
        print("| %d | `%s` | OluxOS extension |" % (1000 + int(n), name))


if __name__ == "__main__":
    main()
