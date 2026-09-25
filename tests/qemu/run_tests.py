#!/usr/bin/env python3
"""OluxOS QEMU integration tests.

Boots the kernel + initramfs in QEMU (AArch64 virt), drives the serial
console like a user would and checks the results. Uses only the Python
standard library so it runs anywhere QEMU does.

  tests/qemu/run_tests.py --out out [--smp 4] [--gic 2] [-k pattern]
"""
import argparse
import os
import re
import select
import signal
import subprocess
import sys
import time

PROMPT = re.compile(rb"root@[\w-]+:[^#\n]*# ")


class Console:
    def __init__(self, cmd, log_path):
        self.log = open(log_path, "wb")
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, bufsize=0, start_new_session=True)
        self.buf = b""
        self.echo = True

    def quiet(self):
        """Disable terminal echo so command output is not mixed with input."""
        self.run("stty -echo")
        self.echo = False

    def _fill(self, timeout):
        r, _, _ = select.select([self.proc.stdout], [], [], timeout)
        if not r:
            return False
        data = os.read(self.proc.stdout.fileno(), 65536)
        if not data:
            raise EOFError("QEMU exited (status %s)" % self.proc.poll())
        self.log.write(data)
        self.log.flush()
        self.buf += data
        return True

    def expect(self, pattern, timeout=30):
        """Wait until regex `pattern` (bytes) appears; returns the text before it."""
        rx = re.compile(pattern) if isinstance(pattern, (bytes, str)) else pattern
        if isinstance(pattern, str):
            rx = re.compile(pattern.encode())
        deadline = time.time() + timeout
        while True:
            m = rx.search(self.buf)
            if m:
                before = self.buf[: m.start()]
                self.buf = self.buf[m.end():]
                return before, m
            left = deadline - time.time()
            if left <= 0:
                tail = self.buf[-800:].decode(errors="replace")
                raise TimeoutError("timed out waiting for %r; last output:\n%s" % (rx.pattern, tail))
            self._fill(min(left, 0.5))

    def send(self, text):
        self.proc.stdin.write(text.encode() if isinstance(text, str) else text)
        self.proc.stdin.flush()

    def run(self, cmd, timeout=60):
        """Run a shell command, return (output, exit status)."""
        marker = "__RC_%d__" % int(time.time() * 1000 % 1000000)
        self.send(cmd + "; echo " + marker + "=$?\n")
        before, m = self.expect(re.compile((marker + r"=(\d+)").encode()), timeout)
        out = before.decode(errors="replace").replace("\r", "")
        if self.echo:  # drop the echoed command line (may wrap)
            idx = out.find(marker)
            out = out[out.find("\n", idx) + 1:] if idx >= 0 else out
        self.expect(PROMPT, 10)
        return out.lstrip("\n"), int(m.group(1))

    def close(self):
        if self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        self.proc.wait()
        self.log.close()


def sd_image(args):
    """The test disk padded to a power-of-two size, as QEMU's SD card needs."""
    if getattr(args, "sd", None):
        return args.sd
    disk = os.path.join(args.out, "disk.img")
    if not os.path.exists(disk):
        return None
    sd = os.path.join(args.logdir, "sd.img")
    if not os.path.exists(sd) or os.path.getmtime(sd) < os.path.getmtime(disk):
        size = 1 << max(os.path.getsize(disk) - 1, 1).bit_length()
        with open(disk, "rb") as src, open(sd, "wb") as dst:
            dst.write(src.read())
            dst.truncate(size)
    return sd


def qemu_cmd(args, extra_append=""):
    if args.machine == "raspi4b":
        # Raspberry Pi 4B emulation (QEMU >= 9.0): fixed 4x Cortex-A72, GIC-400,
        # PL011 on the first serial port. The Pi DTB comes from `make rpi4`.
        cmd = [args.qemu, "-M", "raspi4b", "-kernel", os.path.join(args.out, "Image"),
               "-initrd", os.path.join(args.out, "initramfs.cpio"),
               "-dtb", args.dtb or os.path.join(args.out, "rpi4", "bcm2711-rpi-4-b.dtb"),
               "-append", ("console=ttyAMA0 " + extra_append).strip(),
               "-display", "none", "-serial", "stdio", "-no-reboot"]
        sd = sd_image(args)
        if sd:
            cmd += ["-drive", "if=sd,file=%s,format=raw,snapshot=on" % sd]
        return cmd
    cmd = [args.qemu, "-M", "virt,gic-version=%s" % args.gic, "-cpu", args.cpu, "-smp", str(args.smp),
           "-m", args.mem, "-kernel", os.path.join(args.out, "Image"),
           "-initrd", os.path.join(args.out, "initramfs.cpio"),
           "-nographic", "-no-reboot", "-append", ("console=ttyAMA0 " + extra_append).strip()]
    disk = getattr(args, "disk", None) or os.path.join(args.out, "disk.img")
    if os.path.exists(disk):
        snap = "" if getattr(args, "disk", None) else ",snapshot=on"
        cmd += ["-drive", "if=none,file=%s,format=raw,id=hd0%s" % (disk, snap),
                "-device", "virtio-blk-device,drive=hd0"]
    cmd += ["-device", "virtio-rng-device", "-netdev", "user,id=n0", "-device", "virtio-net-device,netdev=n0"]
    return cmd


# ---------------------------------------------------------------------------
# Test cases. Each takes a booted Console and raises AssertionError on failure.
# ---------------------------------------------------------------------------

def t_boot_banner(c):
    out, rc = c.run("cat /proc/version; uname -sm")
    assert rc == 0 and "OluxOS" in out and "aarch64" in out, out


def t_selftest(c):
    out, rc = c.run("olux-selftest", timeout=180)
    failed = [l for l in out.splitlines() if l.startswith("not ok")]
    assert rc == 0 and "SELFTEST PASSED" in out, "\n".join(failed) or out[-2000:]


def t_smp(c):
    out, rc = c.run("nproc")
    assert rc == 0 and int(out.strip().splitlines()[-1]) == c.args.smp, out


def t_pipeline_and_redirect(c):
    out, rc = c.run("seq 1 100 | grep 7 | sort -n | tail -1 > /tmp/x && cat /tmp/x && rm /tmp/x")
    assert rc == 0 and out.strip().endswith("97"), out


def t_shell_scripting(c):
    script = "i=0; s=0; while [ $i -lt 50 ]; do i=$((i+1)); s=$((s+i)); done; echo sum=$s"
    out, rc = c.run(script)
    assert "sum=1275" in out, out


def t_file_utilities(c):
    out, rc = c.run("mkdir -p /tmp/a/b && echo hi > /tmp/a/b/f && tar -C /tmp -czf /tmp/a.tgz a && rm -r /tmp/a && "
                    "tar -C /tmp -xzf /tmp/a.tgz && cat /tmp/a/b/f && md5sum /tmp/a/b/f")
    assert rc == 0 and "hi" in out and "764efa883dda1e11db47671c4a3bbd9e" in out, out


def t_background_jobs(c):
    out, rc = c.run("sleep 1 & sleep 1 & wait; echo waited")
    assert rc == 0 and "waited" in out, out


def t_ctrl_c(c):
    c.send("sleep 30\n")
    time.sleep(1.0)
    c.send("\x03")
    c.expect(PROMPT, 10)
    out, rc = c.run("echo alive")
    assert "alive" in out


def t_job_control(c):
    c.send("sleep 30\n")
    time.sleep(1.0)
    c.send("\x1a")  # ^Z
    c.expect(PROMPT, 10)
    out, rc = c.run("jobs")
    assert "sleep" in out and ("Stopped" in out or "stopped" in out.lower()), out
    out, rc = c.run("kill %1; sleep 0.2; jobs; echo done")
    assert "done" in out


def t_proc_tools(c):
    out, rc = c.run("ps; free; uptime; cat /proc/loadavg")
    assert rc == 0 and "PID" in out and "Mem:" in out and "load average" in out, out


def t_mounts(c):
    out, rc = c.run("mount; df")
    assert rc == 0 and "proc" in out and "tmpfs" in out, out


def t_segfault_contained(c):
    out, rc = c.run("sh -c 'kill -SEGV $$'; echo status=$?")
    assert "status=139" in out, out
    out, rc = c.run("echo still-alive")
    assert "still-alive" in out


def t_memory_stress(c):
    out, rc = c.run("for i in 1 2 3 4 5 6 7 8; do dd if=/dev/zero of=/tmp/f$i bs=64k count=64 2>/dev/null & done; "
                    "wait; ls /tmp | grep -c '^f'; rm -f /tmp/f*; free", timeout=120)
    assert rc == 0 and "8" in out.splitlines()[0], out


def t_fork_bomb_limited(c):
    # many short-lived processes; the kernel must not leak memory
    before, _ = c.run("grep MemFree /proc/meminfo")
    c.run("for i in $(seq 1 200); do /bin/true; done", timeout=120)
    after, _ = c.run("grep MemFree /proc/meminfo")
    b = int(re.search(r"(\d+)", before).group(1))
    a = int(re.search(r"(\d+)", after).group(1))
    assert b - a < 2048, "leaked %d kB" % (b - a)


def t_block_device(c):
    if c.args.machine != "virt":
        return
    out, rc = c.run("ls /dev/vda1 /dev/vda2 && dd if=/dev/urandom of=/tmp/r bs=4096 count=16 2>/dev/null && "
                    "dd if=/tmp/r of=/dev/vda bs=4096 seek=20000 2>/dev/null && sync && "
                    "echo 3 > /dev/null && dd if=/dev/vda bs=4096 skip=20000 count=16 2>/dev/null | md5sum && md5sum < /tmp/r")
    sums = re.findall(r"([0-9a-f]{32})", out)
    assert rc == 0 and len(sums) == 2 and sums[0] == sums[1], out


def wait_mount(c, path):
    out, rc = c.run("for i in $(seq 1 100); do grep -q ' %s userfs' /proc/mounts && break; sleep 0.1; done; "
                    "grep ' %s userfs' /proc/mounts" % (path, path))
    assert rc == 0, "%s not mounted: %s" % (path, out)


def fat_dir(c):
    return "/mnt/fat" if c.args.machine == "virt" else "/boot"


def t_fatfs(c):
    F = fat_dir(c)
    wait_mount(c, F)
    out, rc = c.run("cat %s/hello.txt '%s/docs/nested/A Long File Name.txt' && cmp %s/busybox /bin/busybox" % (F, F, F))
    assert rc == 0 and out.count("Hello from FAT32") == 2, out
    out, rc = c.run("cd " + F + " && mkdir -p 'Dir One/sub' && echo payload > 'Dir One/sub/Mixed Case Name.TXT' && "
                    "mv 'Dir One/sub/Mixed Case Name.TXT' 'Dir One/renamed.txt' && mv 'Dir One' dir2 && "
                    "cat dir2/renamed.txt && ls dir2 && rmdir dir2/sub && rm dir2/renamed.txt && rmdir dir2 && "
                    "! ls dir2 2>/dev/null; cd /")
    assert rc == 0 and "payload" in out and "renamed.txt" in out, out
    out, rc = c.run("dd if=/dev/urandom of=/tmp/big bs=4096 count=700 2>/dev/null && cp /tmp/big {0}/big.bin && "
                    "sha256sum /tmp/big {0}/big.bin && rm {0}/big.bin /tmp/big".format(F))
    sums = re.findall(r"([0-9a-f]{64})", out)
    assert rc == 0 and len(sums) == 2 and sums[0] == sums[1], out
    out, rc = c.run("f=" + F + "/sparse; echo hi > $f && truncate -s 70000 $f && stat -c %s $f && "
                    "tail -c 5 $f | tr '\\0' Z && echo && truncate -s 2 $f && cat $f && rm $f")
    assert rc == 0 and "70000" in out and "ZZZZZ" in out and out.strip().endswith("hi"), out
    out, rc = c.run("for i in $(seq 1 200); do echo $i > {0}/docs/file_number_$i.txt; done; "
                    "ls {0}/docs | wc -l; cat {0}/docs/file_number_177.txt; rm {0}/docs/file_number_*".format(F))
    assert rc == 0 and "201" in out and "177" in out, out


def t_ext4fs(c):
    wait_mount(c, "/mnt/ext")
    out, rc = c.run("cd /mnt/ext && sha256sum -c data/blob.sha256 && cat link-to-hello etc/app.conf; cd /")
    assert rc == 0 and "blob.bin: OK" in out and "Hello from ext4" in out and "config=1" in out, out
    out, rc = c.run("touch /mnt/ext/newfile")
    assert rc != 0 and "Read-only" in out, out


def t_fs_server_restart(c):
    """Killing a filesystem server must not affect the kernel: I/O fails
    with EIO, init restarts the server, it re-attaches and I/O resumes."""
    F = fat_dir(c)
    wait_mount(c, F)
    c.run("echo survives > %s/keep.txt" % F)
    out, rc = c.run("kill -9 $(pidof fatfsd); sleep 0.2; cat %s/keep.txt" % F)
    assert rc != 0 and "I/O error" in out, out
    out, rc = c.run("for i in $(seq 1 50); do cat %s/keep.txt 2>/dev/null && break; sleep 0.2; done" % F)
    assert rc == 0 and "survives" in out, out
    out, rc = c.run("dmesg | grep -c 're-attached'; rm %s/keep.txt" % F)
    assert rc == 0, out


def t_rpi_platform(c):
    if c.args.machine != "raspi4b":
        return
    out, rc = c.run("rpi-info && rpi-info temp && rpi-info clock arm")
    assert rc == 0 and "revision:" in out and "temp=" in out and "frequency(3)=" in out, out
    out, rc = c.run("gpio set 21 1 && gpio get 21 && gpio set 21 0 && gpio get 21 && gpio func 21 alt3 && "
                    "gpio info | grep 'GPIO21 '")
    assert rc == 0 and out.split()[:2] == ["1", "0"] and "alt3" in out, out
    out, rc = c.run("gpio wait 20 rising 200")
    assert rc == 1 and "timed out" in out, out
    out, rc = c.run("dmesg | grep -E 'mmc: .* card|last reset' && ls /dev/watchdog /dev/mmcblk0p1")
    assert rc == 0 and "card" in out, out


def t_init_respawn(c):
    c.send("exit\n")
    c.expect(PROMPT, 20)
    c.echo = True
    c.quiet()
    out, rc = c.run("echo respawned")
    assert "respawned" in out


TESTS = [
    t_boot_banner, t_smp, t_selftest, t_pipeline_and_redirect, t_shell_scripting, t_file_utilities,
    t_background_jobs, t_ctrl_c, t_job_control, t_proc_tools, t_mounts, t_segfault_contained,
    t_memory_stress, t_fork_bomb_limited, t_block_device, t_fatfs, t_ext4fs, t_fs_server_restart, t_rpi_platform, t_init_respawn,
]


def t_poweroff(args):
    """Separate boot: `poweroff` must shut QEMU down cleanly."""
    c = Console(qemu_cmd(args), os.path.join(args.logdir, "poweroff.log"))
    try:
        c.expect(PROMPT, 60)
        c.send("poweroff\n")
        deadline = time.time() + 30
        while c.proc.poll() is None and time.time() < deadline:
            try:
                c._fill(0.5)
            except EOFError:
                break
        try:
            c.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            pass
        assert c.proc.poll() is not None, "QEMU still running after poweroff"
        assert b"power down" in c.buf or b"power-off" in c.buf, c.buf[-500:]
    finally:
        c.close()


def wait_exit(c, seconds):
    deadline = time.time() + seconds
    while c.proc.poll() is None and time.time() < deadline:
        try:
            c._fill(0.5)
        except EOFError:
            break
    try:
        c.proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        pass
    return c.proc.poll() is not None


def t_watchdog(args):
    """Separate boot without init's supervision: a watchdog daemon that dies
    without the magic close must get the machine reset."""
    c = Console(qemu_cmd(args, "init.watchdog=0"), os.path.join(args.logdir, "watchdog.log"))
    try:
        c.expect(PROMPT, 90)
        c.quiet()
        out, rc = c.run("watchdog -T 3 -t 1 /dev/watchdog && sleep 2 && kill -9 $(pidof watchdog); echo killed")
        assert "killed" in out, out
        t0 = time.time()
        assert wait_exit(c, 20), "no reset after the watchdog expired"
        assert b"no keepalive" in c.buf, c.buf[-500:]
        assert time.time() - t0 < 15, "reset took %.1fs" % (time.time() - t0)
    finally:
        c.close()


def t_sdcard(args):
    """Separate raspi4b boot from the `make sdcard` image: boot partition at
    /boot, writable data partition at /data."""
    img = os.path.join(args.out, "sdcard.img")
    boot_args = argparse.Namespace(**vars(args))
    boot_args.sd = img
    c = Console(qemu_cmd(boot_args), os.path.join(args.logdir, "sdcard.log"))
    c.args = boot_args
    try:
        c.expect(PROMPT, 90)
        c.quiet()
        wait_mount(c, "/data")
        wait_mount(c, "/boot")
        out, rc = c.run("ls /boot && echo ok > /data/probe && cat /data/probe /data/README.txt")
        assert rc == 0 and "kernel8.img" in out and "start4.elf" in out and "ok" in out, out
    finally:
        c.close()


def t_persistence(args):
    """Two boots on a private disk copy: data written to FAT in the first
    boot survives a clean power-off; the second mount finds a clean volume."""
    import shutil
    disk = os.path.join(args.logdir, "persist-disk.img")
    shutil.copyfile(os.path.join(args.out, "disk.img"), disk)
    boot_args = argparse.Namespace(**vars(args))
    boot_args.disk = disk
    for boot in (1, 2):
        c = Console(qemu_cmd(boot_args), os.path.join(args.logdir, "persist-%d.log" % boot))
        c.args = boot_args
        try:
            c.expect(PROMPT, 90)
            c.quiet()
            wait_mount(c, "/mnt/fat")
            if boot == 1:
                out, rc = c.run("mkdir /mnt/fat/persist && seq 1 5000 > /mnt/fat/persist/numbers.txt && "
                                "sha256sum /mnt/fat/persist/numbers.txt")
                assert rc == 0, out
                want = re.search(r"([0-9a-f]{64})", out).group(1)
                c.send("poweroff\n")
                deadline = time.time() + 30
                while c.proc.poll() is None and time.time() < deadline:
                    try:
                        c._fill(0.5)
                    except EOFError:
                        break
                try:
                    c.proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    pass
                assert c.proc.poll() is not None, "QEMU still running after poweroff"
            else:
                out, rc = c.run("sha256sum /mnt/fat/persist/numbers.txt; dmesg | grep -c 'not cleanly unmounted'")
                assert want in out, out
                assert out.strip().endswith("0"), "volume was not marked clean: " + out
        finally:
            c.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="out")
    ap.add_argument("--qemu", default="qemu-system-aarch64")
    ap.add_argument("--machine", default="virt", choices=["virt", "raspi4b"])
    ap.add_argument("--dtb", default="")
    ap.add_argument("--cpu", default="cortex-a72")
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--gic", default="2")
    ap.add_argument("--mem", default="512M")
    ap.add_argument("-k", dest="pattern", default="")
    args = ap.parse_args()
    if args.machine == "raspi4b":
        args.smp = 4
    args.logdir = os.path.join(args.out, "test-logs")
    os.makedirs(args.logdir, exist_ok=True)

    selected = [t for t in TESTS if args.pattern in t.__name__]
    failures = []
    log = os.path.join(args.logdir, "console-%s-smp%d-gic%s.log" % (args.machine, args.smp, args.gic))
    c = Console(qemu_cmd(args), log)
    c.args = args
    t0 = time.time()
    try:
        c.expect(PROMPT, 90)
        print("booted in %.1fs" % (time.time() - t0))
        c.quiet()
        for t in selected:
            start = time.time()
            try:
                t(c)
                print("PASS %-28s (%.1fs)" % (t.__name__[2:], time.time() - start))
            except (AssertionError, TimeoutError, EOFError) as e:
                failures.append(t.__name__)
                print("FAIL %-28s %s" % (t.__name__[2:], str(e).strip()[:1500]))
                if isinstance(e, EOFError):
                    break
                # resynchronise with the shell
                try:
                    c.send("\x03\n")
                    c.expect(PROMPT, 10)
                except Exception:
                    pass
            if b"Kernel panic" in c.buf or b"Oops" in c.buf:
                failures.append("kernel-panic")
                print("FAIL kernel panic detected")
                break
    except (TimeoutError, EOFError) as e:
        failures.append("boot")
        print("FAIL boot: %s" % e)
    finally:
        c.close()

    if not args.pattern or "poweroff" in args.pattern:
        try:
            t_poweroff(args)
            print("PASS poweroff")
        except (AssertionError, TimeoutError, EOFError) as e:
            failures.append("poweroff")
            print("FAIL poweroff %s" % e)

    if not args.pattern or "watchdog" in args.pattern:
        try:
            t_watchdog(args)
            print("PASS watchdog")
        except (AssertionError, TimeoutError, EOFError) as e:
            failures.append("watchdog")
            print("FAIL watchdog %s" % e)

    sdcard = os.path.join(args.out, "sdcard.img")
    if (not args.pattern or "sdcard" in args.pattern) and args.machine == "raspi4b" and os.path.exists(sdcard):
        try:
            t_sdcard(args)
            print("PASS sdcard")
        except (AssertionError, TimeoutError, EOFError) as e:
            failures.append("sdcard")
            print("FAIL sdcard %s" % e)

    if (not args.pattern or "persist" in args.pattern) and args.machine == "virt":
        try:
            t_persistence(args)
            print("PASS persistence")
        except (AssertionError, TimeoutError, EOFError) as e:
            failures.append("persistence")
            print("FAIL persistence %s" % e)

    print("\n%d failure(s)%s" % (len(failures), (": " + ", ".join(failures)) if failures else ""))
    print("console log: %s" % log)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
