#!/usr/bin/env python3
"""Soak test: hours of mixed load on QEMU virt, watching for hangs, panics
and leaks (the plan's 7-day Pi 4 soak, scaled down and automated).

    tests/qemu/soak.py --minutes 30 [--out out] [--smp 4]

The guest runs, in the background:
  - file-system churn on the FAT server (write, checksum, delete),
  - HTTP requests against the local httpd and pipe/fork churn,
and the host, every --interval seconds:
  - checks that the shell answers and no panic or oops was logged,
  - samples MemAvailable, kernel heap (Slab) and the process count,
  - every few rounds kills the FAT server, which init must restart.
At the end, available memory (free plus buffer cache) must be back within
--leak-mb of the first sample taken after warm-up. Exit status 0 = pass.
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_tests as T  # noqa: E402

WORKLOAD = r"""
mkdir -p /mnt/fat/soak /www && echo soak > /www/index.html
(httpd -p 8080 -h /www) 2>/dev/null
cat > /tmp/soak.sh <<'W'
i=0
while :; do
  i=$((i + 1))
  f=/mnt/fat/soak/f$((i % 4))
  if dd if=/dev/urandom of=/tmp/s.bin bs=4096 count=64 2>/dev/null && cp /tmp/s.bin $f 2>/dev/null; then
    a=$(md5sum < /tmp/s.bin); b=$(md5sum < $f 2>/dev/null)
    [ -n "$b" ] && [ "$a" != "$b" ] && grep -q ' /mnt/fat ' /proc/mounts && echo "SOAK-MISMATCH $f" > /dev/console
  fi
  wget -q -O /dev/null http://127.0.0.1:8080/ 2>/dev/null
  seq 1 200 | sort -r | tail -1 > /dev/null
  rm -f $f 2>/dev/null
done
W
(sh /tmp/soak.sh > /dev/null 2>&1 &)
"""


def meminfo(c):
    out, rc = c.run("grep -E 'MemAvailable|Slab' /proc/meminfo; ls -d /proc/[0-9]* | wc -l", timeout=60)
    free = int(re.search(r"MemAvailable:\s+(\d+)", out).group(1))  # free + reclaimable buffer cache
    slab = int(re.search(r"Slab:\s+(\d+)", out).group(1))
    procs = int(out.strip().split()[-1])
    return free, slab, procs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="out")
    ap.add_argument("--qemu", default="qemu-system-aarch64")
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--mem", default="512M")
    ap.add_argument("--minutes", type=float, default=30)
    ap.add_argument("--interval", type=int, default=20)
    ap.add_argument("--warmup", type=int, default=60, help="seconds before the baseline sample")
    ap.add_argument("--leak-mb", type=int, default=8)
    args = ap.parse_args()
    args.machine, args.gic, args.cpu, args.dtb = "virt", "2", "cortex-a72", ""
    args.logdir = os.path.join(args.out, "test-logs")
    os.makedirs(args.logdir, exist_ok=True)
    log = os.path.join(args.logdir, "soak.log")
    c = T.Console(T.qemu_cmd(args), log)
    ok = True
    try:
        c.expect(T.PROMPT, 90)
        c.quiet()
        T.wait_mount(c, "/mnt/fat")
        c.send(WORKLOAD.strip() + "\n")
        c.expect(T.PROMPT, 30)
        c.quiet()
        start = time.time()
        end = start + args.minutes * 60
        base = None
        rnd = 0
        print("soak: %.0f minutes, log %s" % (args.minutes, log))
        while time.time() < end:
            time.sleep(args.interval)
            rnd += 1
            free, slab, procs = meminfo(c)
            el = time.time() - start
            print("soak: t=%5.0fs MemAvailable %6d kB Slab %6d kB procs %3d" % (el, free, slab, procs), flush=True)
            if base is None and el >= args.warmup:
                base = (free, slab)
            if rnd % 5 == 0:  # supervisor recovery under load
                c.run("kill $(pidof fatfsd | tr ' ' '\\n' | head -1) 2>/dev/null; sleep 2", timeout=60)
            text = c.buf.decode(errors="replace")
            for bad in ("Kernel panic", "Internal error", "SOAK-MISMATCH", "BUG at", "WARNING at"):
                if bad in text:
                    print("soak: FAIL: '%s' in the console log" % bad)
                    ok = False
            if not ok:
                break
        if ok:
            c.run("pkill -f soak.sh; sleep 3; sync", timeout=60)
            time.sleep(5)
            free, slab, procs = meminfo(c)
            if base:
                lost = (base[0] - free) // 1024
                print("soak: MemAvailable %d -> %d kB (%+d MiB), Slab %d -> %d kB" % (base[0], free, -lost, base[1], slab))
                if lost > args.leak_mb:
                    print("soak: FAIL: %d MiB of memory not returned (budget %d MiB)" % (lost, args.leak_mb))
                    ok = False
    except (TimeoutError, EOFError, AssertionError) as e:
        print("soak: FAIL: %s" % e)
        ok = False
    finally:
        c.close()
    print("soak: %s" % ("PASS" if ok else "FAIL"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
