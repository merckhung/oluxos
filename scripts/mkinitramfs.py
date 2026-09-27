#!/usr/bin/env python3
"""Build the OluxOS initramfs (newc cpio) deterministically.

Contents:
  * the rootfs/ skeleton (directories, config files, scripts)
  * BusyBox at /bin/busybox plus a symlink for every applet
  * OluxOS user programs (user/prog/<name>, installed to the path in
    user/prog/<name>/dest or /bin/<name> by default)
All entries are owned by root and carry a fixed timestamp.
"""
import argparse
import os
import stat
import sys

MTIME = int(os.environ.get("SOURCE_DATE_EPOCH", "1700000000"))


class Cpio:
    def __init__(self):
        self.entries = {}  # path -> (mode, data, target)

    def add_dir(self, path, mode=0o755):
        path = path.rstrip("/") or "/"
        parent = os.path.dirname(path)
        if parent not in ("", "/") and parent not in self.entries:
            self.add_dir(parent)
        if path != "/":
            self.entries.setdefault(path, (stat.S_IFDIR | mode, b""))

    def add_file(self, path, data, mode=0o644):
        self.add_dir(os.path.dirname(path))
        self.entries[path] = (stat.S_IFREG | mode, data)

    def add_symlink(self, path, target):
        self.add_dir(os.path.dirname(path))
        self.entries.setdefault(path, (stat.S_IFLNK | 0o777, target.encode()))

    def add_node(self, path, mode, major, minor):
        self.add_dir(os.path.dirname(path))
        self.entries[path] = (mode, b"", (major, minor))

    def write(self, out):
        ino = 1
        buf = bytearray()

        def hdr(name, mode, size, nlink=1, dev=(0, 0)):
            nonlocal ino
            n = name.encode() + b"\0"
            fields = [ino, mode, 0, 0, nlink, MTIME, size, 0, 0, dev[0], dev[1], len(n), 0]
            ino += 1
            h = b"070701" + b"".join(b"%08x" % f for f in fields)
            buf.extend(h + n)
            while len(buf) % 4:
                buf.append(0)

        # directories first (sorted so parents precede children)
        for path in sorted(self.entries):
            ent = self.entries[path]
            mode, data = ent[0], ent[1]
            dev = ent[2] if len(ent) > 2 else (0, 0)
            name = path.lstrip("/")
            hdr(name, mode, len(data), 2 if stat.S_ISDIR(mode) else 1, dev)
            buf.extend(data)
            while len(buf) % 4:
                buf.append(0)
        hdr("TRAILER!!!", 0, 0)
        while len(buf) % 512:
            buf.append(0)
        with open(out, "wb") as f:
            f.write(buf)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--busybox", required=True)
    ap.add_argument("--links", help="busybox.links (defaults next to --busybox)")
    ap.add_argument("--skel", default="rootfs")
    ap.add_argument("--bin", action="append", default=[], help="built user program")
    ap.add_argument("--prog-dir", default="user/prog")
    ap.add_argument("--extra", action="append", default=[], help="host_path:target_path")
    a = ap.parse_args()

    c = Cpio()
    for d in ["/bin", "/sbin", "/usr/bin", "/usr/sbin", "/etc", "/dev", "/proc", "/sys", "/tmp",
              "/run", "/var", "/var/log", "/root", "/home", "/mnt", "/data", "/lib"]:
        c.add_dir(d, 0o1777 if d == "/tmp" else 0o755)
    c.add_node("/dev/console", stat.S_IFCHR | 0o600, 5, 1)

    # skeleton
    if os.path.isdir(a.skel):
        for root, dirs, files in os.walk(a.skel):
            dirs.sort()
            rel = os.path.relpath(root, a.skel)
            base = "/" if rel == "." else "/" + rel
            c.add_dir(base)
            for fn in sorted(files):
                src = os.path.join(root, fn)
                dst = os.path.join(base, fn)
                if os.path.islink(src):
                    c.add_symlink(dst, os.readlink(src))
                    continue
                mode = 0o755 if os.access(src, os.X_OK) else 0o644
                with open(src, "rb") as f:
                    c.add_file(dst, f.read(), mode)

    # busybox + applets
    with open(a.busybox, "rb") as f:
        c.add_file("/bin/busybox", f.read(), 0o755)
    links = a.links or os.path.join(os.path.dirname(a.busybox), "busybox.links")
    if os.path.exists(links):
        for line in open(links):
            p = line.strip()
            if not p or p == "/bin/busybox":
                continue
            depth = p.count("/") - 1
            target = "../" * (depth - 1) + "bin/busybox" if depth > 1 else "busybox" if p.startswith("/bin/") else "/bin/busybox"
            if p.startswith("/bin/"):
                target = "busybox"
            else:
                target = "/bin/busybox"
            c.add_symlink(p, target)
    else:
        print("warning: no busybox.links; only /bin/sh is linked", file=sys.stderr)
        c.add_symlink("/bin/sh", "busybox")

    # user programs
    for b in a.bin:
        name = os.path.basename(b)
        dest = "/bin/" + name
        destfile = os.path.join(a.prog_dir, name, "dest")
        if os.path.exists(destfile):
            dest = open(destfile).read().strip()
        with open(b, "rb") as f:
            c.add_file(dest, f.read(), 0o755)

    for e in a.extra:
        src, dst = e.split(":", 1)
        with open(src, "rb") as f:
            c.add_file(dst, f.read(), 0o755 if os.access(src, os.X_OK) else 0o644)

    c.write(a.output)


if __name__ == "__main__":
    main()
