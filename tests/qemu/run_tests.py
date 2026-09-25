#!/usr/bin/env python3
"""OluxOS QEMU integration tests.

Boots the kernel + initramfs in QEMU (AArch64 virt), drives the serial
console like a user would and checks the results. Uses only the Python
standard library so it runs anywhere QEMU does.

  tests/qemu/run_tests.py --out out [--smp 4] [--gic 2] [-k pattern]
"""
import argparse
import io
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
        """Disable terminal echo so command output is not mixed with input,
        and keep asynchronous info-level kernel messages (USB, mounts) off the
        console; notices, warnings and panics still show."""
        self.run("stty -echo; dmesg -n 5")
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


def pi_dtb(args):
    """The Pi 4 DTB with I2C1 and SPI0 enabled, as `dtparam=i2c_arm=on,spi=on`
    makes the firmware do on real hardware (needs fdtput; else the stock DTB)."""
    dtb = args.dtb or os.path.join(args.out, "rpi4", "bcm2711-rpi-4-b.dtb")
    patched = os.path.join(args.logdir, "rpi4-test.dtb")
    try:
        with open(dtb, "rb") as src, open(patched, "wb") as dst:
            dst.write(src.read())
        for node in ("/soc/i2c@7e804000", "/soc/spi@7e204000"):
            subprocess.run(["fdtput", "-t", "s", patched, node, "status", "okay"], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return patched
    except (OSError, subprocess.CalledProcessError):
        return dtb


def qemu_cmd(args, extra_append=""):
    if args.machine == "raspi4b":
        # Raspberry Pi 4B emulation (QEMU >= 9.0): fixed 4x Cortex-A72, GIC-400,
        # PL011 on the first serial port. The Pi DTB comes from `make rpi4`.
        cmd = [args.qemu, "-M", "raspi4b", "-kernel", os.path.join(args.out, "Image"),
               "-initrd", os.path.join(args.out, "initramfs.cpio"),
               "-dtb", pi_dtb(args),
               "-append", ("console=ttyAMA0 " + extra_append).strip(),
               "-display", "none", "-serial", "stdio", "-no-reboot"]
        sd = sd_image(args)
        if sd:
            snap = "" if getattr(args, "sd_persist", False) else ",snapshot=on"
            cmd += ["-drive", "if=sd,file=%s,format=raw%s" % (sd, snap)]
        return cmd
    cmd = [args.qemu, "-M", "virt,gic-version=%s" % args.gic, "-cpu", args.cpu, "-smp", str(args.smp),
           "-m", args.mem, "-kernel", os.path.join(args.out, "Image"),
           "-initrd", os.path.join(args.out, "initramfs.cpio"),
           "-nographic", "-append", ("console=ttyAMA0 " + extra_append).strip()]
    if not getattr(args, "allow_reboot", False):
        cmd.insert(cmd.index("-append"), "-no-reboot")
    disk = getattr(args, "disk", None) or os.path.join(args.out, "disk.img")
    if os.path.exists(disk):
        snap = "" if getattr(args, "disk", None) else ",snapshot=on"
        cmd += ["-drive", "if=none,file=%s,format=raw,id=hd0%s" % (disk, snap),
                "-device", "virtio-blk-device,drive=hd0"]
    fwd = ""
    if getattr(args, "fwd_http", 0):
        fwd = ",hostfwd=tcp:127.0.0.1:%d-:80,hostfwd=tcp:127.0.0.1:%d-:23,hostfwd=tcp:127.0.0.1:%d-:22" % (
            args.fwd_http, args.fwd_telnet, args.fwd_ssh)
    cmd += ["-device", "virtio-rng-device", "-netdev", "user,id=n0" + fwd, "-device", "virtio-net-device,netdev=n0"]
    # USB: xHCI with a keyboard and a mouse on root ports, and a hub with a
    # thumb drive (a copy of the test disk) behind it
    if os.path.exists(disk) and getattr(args, "logdir", None):
        cmd += ["-device", "qemu-xhci,id=xhci", "-device", "usb-kbd,bus=xhci.0,port=1",
                "-device", "usb-mouse,bus=xhci.0,port=2", "-device", "usb-hub,bus=xhci.0,port=3,id=usbhub",
                "-drive", "if=none,id=usbdisk,file=%s,format=raw,snapshot=on" % os.path.join(args.out, "disk.img"),
                "-device", "usb-storage,drive=usbdisk,bus=xhci.0,port=3.2"]
        # (not in logdir: CI uploads that directory, and zip cannot hold sockets)
        import tempfile
        args.monitor = os.path.join(tempfile.gettempdir(), "olux-monitor-%d.sock" % os.getpid())
        cmd += ["-monitor", "unix:%s,server=on,wait=off" % args.monitor]
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
    nums = [l.strip() for l in out.splitlines() if l.strip().isdigit()]
    assert rc == 0 and nums and int(nums[-1]) == c.args.smp, out


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
    out, rc = c.run("ls /dev/i2c-1 /dev/spidev0.0 && i2cdetect -y 1 && i2cget -y 1 0x50 0 b; echo rc=$?")
    assert "/dev/i2c-1" in out and "70:" in out and "rc=1" in out, out
    out, rc = c.run("olux-selftest spidev")
    assert rc == 0 and "SELFTEST PASSED" in out, out


def free_port():
    import socket
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def t_network(c):
    """DHCP at boot, ICMP, outbound HTTP to a server on the host, inbound
    HTTP (BusyBox httpd) and an interactive telnet session over a pty."""
    if c.args.machine != "virt":
        return
    import hashlib
    import http.server
    import socket
    import threading
    import urllib.request
    out, rc = c.run("for i in $(seq 1 100); do netcfg | grep -q dhcp && break; sleep 0.1; done; netcfg; "
                    "cat /etc/resolv.conf; ping -c 2 -W 2 10.0.2.2")
    assert rc == 0 and "10.0.2.15/24 (dhcp)" in out and "nameserver 10.0.2.3" in out and "2 packets received" in out, out
    # IPv6: SLAAC address from QEMU's router advertisements, ICMPv6, TCP over IPv6
    out, rc = c.run("for i in $(seq 1 50); do grep -q '^fec0' /proc/net/if_inet6 && break; sleep 0.1; done; "
                    "ifconfig eth0 | grep inet6; ping6 -c 2 fec0::2 && "
                    "(echo v6-server | nc -l -p 7777 > /tmp/v6got &) && sleep 0.3 && echo v6-client | nc ::1 7777 && "
                    "sleep 0.3 && cat /tmp/v6got && netstat -tan")
    assert rc == 0 and "fec0::" in out and "2 packets received" in out and "v6-server" in out and "v6-client" in out, out
    # outbound: fetch from a server on the host (10.0.2.2 is the host for QEMU)
    payload = os.urandom(1 << 20)
    class H(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        def log_message(self, *a):
            pass
    srv = http.server.HTTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    try:
        out, rc = c.run("wget -q -O - http://10.0.2.2:%d/blob | sha256sum" % srv.server_address[1], timeout=120)
    finally:
        srv.shutdown()
    assert rc == 0 and hashlib.sha256(payload).hexdigest() in out, out
    # inbound HTTP
    out, rc = c.run("mkdir -p /tmp/www && dd if=/dev/urandom of=/tmp/www/big bs=4096 count=1024 2>/dev/null && "
                    "echo hello-from-guest > /tmp/www/index.html && httpd -p 80 -h /tmp/www && "
                    "for i in $(seq 1 50); do netstat -tln | grep -q ':80 ' && break; sleep 0.1; done; "
                    "sha256sum /tmp/www/big")
    want = re.search(r"([0-9a-f]{64})", out).group(1)
    base = "http://127.0.0.1:%d/" % c.args.fwd_http
    body = urllib.request.urlopen(base + "index.html", timeout=30).read()
    assert body == b"hello-from-guest\n", body
    big = urllib.request.urlopen(base + "big", timeout=120).read()
    assert hashlib.sha256(big).hexdigest() == want, "download corrupted (%d bytes)" % len(big)
    # telnet: a login-less shell on a pty
    c.run("telnetd -p 23 -l /bin/sh; for i in $(seq 1 50); do netstat -tln | grep -q ':23 ' && break; sleep 0.1; done")
    tel = socket.create_connection(("127.0.0.1", c.args.fwd_telnet), timeout=30)
    try:
        tel.sendall(b"echo TEL$((40+2))NET; tty\r\n")
        data, deadline = b"", time.time() + 30
        while b"TEL42NET" not in data or b"/dev/pts/" not in data:
            if time.time() > deadline:
                break
            try:
                chunk = tel.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            data += chunk
    finally:
        tel.close()
    assert b"TEL42NET" in data and b"/dev/pts/" in data, data
    c.run("killall httpd telnetd")
    # SSH (Dropbear): key login with a pty, from inside the guest
    out, rc = c.run("for i in $(seq 1 50); do netstat -tln | grep -q ':22 ' && break; sleep 0.1; done; "
                    "dropbearkey -t ed25519 -f /tmp/id_test | grep ^ssh-ed25519 >> /root/.ssh/authorized_keys && "
                    "ssh -y -t -i /tmp/id_test root@127.0.0.1 'tty; echo SSH-$((6*7))' && "
                    "grep -c 'Pubkey auth succeeded' /var/log/messages", timeout=120)
    assert rc == 0 and "SSH-42" in out and "/dev/pts/" in out, out
    # ... and from the host when it has an OpenSSH client (CI runners do)
    import shutil
    import subprocess as sp
    if shutil.which("ssh") and shutil.which("ssh-keygen"):
        key = os.path.join(c.args.logdir, "id_host")
        for f in (key, key + ".pub"):
            if os.path.exists(f):
                os.unlink(f)
        sp.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", key], check=True)
        pub = open(key + ".pub").read().strip()
        c.run("echo '%s' >> /root/.ssh/authorized_keys" % pub)
        r = sp.run(["ssh", "-i", key, "-p", str(c.args.fwd_ssh), "-o", "StrictHostKeyChecking=no",
                    "-o", "UserKnownHostsFile=/dev/null", "-o", "BatchMode=yes", "root@127.0.0.1",
                    "uname -sm; echo HOST-SSH-OK"], capture_output=True, text=True, timeout=60)
        assert "HOST-SSH-OK" in r.stdout and "OluxOS" in r.stdout, r.stdout + r.stderr


def t_ntp(c):
    """ntpd sets the clock from an SNTP server on the host; adjtimex works."""
    if c.args.machine != "virt":
        return
    import socket
    import struct
    import threading
    srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    srv.bind(("127.0.0.1", 0))
    srv.settimeout(0.5)
    port = srv.getsockname()[1]
    stop = []

    def serve():
        while not stop:
            try:
                data, addr = srv.recvfrom(512)
            except socket.timeout:
                continue
            if len(data) < 48:
                continue
            now = time.time() + 2208988800
            ts = struct.pack("!II", int(now), int((now % 1) * (1 << 32)))
            reply = struct.pack("!BBbb", 0x24, 1, 4, -20) + struct.pack("!II", 0, 0) + b"GPS\0" + ts + data[40:48] + ts + ts
            srv.sendto(reply, addr)
    th = threading.Thread(target=serve, daemon=True)
    th.start()
    try:
        out, rc = c.run("killall ntpd; date -s 2001-01-01 >/dev/null; ntpd -n -q -p 10.0.2.2:%d; date +%%Y" % port,
                        timeout=120)
    finally:
        stop.append(1)
        th.join()
        srv.close()
    assert rc == 0 and time.strftime("%Y") in out.split()[-1], out
    out, rc = c.run("adjtimex | grep -E 'status|frequency'")
    assert rc == 0 and "status" in out, out


def monitor(args, *commands):
    """Send commands to the QEMU monitor."""
    import socket
    m = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    m.connect(args.monitor)
    m.settimeout(2)
    try:
        m.recv(4096)  # banner
        for cmd in commands:
            m.sendall((cmd + "\n").encode())
            time.sleep(0.05)
            try:
                m.recv(4096)
            except socket.timeout:
                pass
    finally:
        m.close()


def t_usb(c):
    """xHCI: keyboard typing into the console, mouse events on evdev, and a
    USB thumb drive as /dev/sda with partitions and a FAT filesystem."""
    if c.args.machine != "virt" or not getattr(c.args, "monitor", None):
        return
    out, rc = c.run("ls /dev/input/event0 /dev/input/event1 /dev/sda1 && dmesg | grep -E 'input:|sda:'")
    assert rc == 0 and "USB Keyboard" in out and "USB mass storage" in out, out
    # keyboard: type a command at the shell prompt
    keys = ["e", "c", "h", "o", "spc", "k", "b", "d", "minus", "shift-k", "shift-b", "shift-d", "ret"]
    monitor(c.args, *("sendkey " + k for k in keys))
    c.expect(rb"kbd-KBD", 20)
    c.expect(PROMPT, 10)
    # mouse: relative motion arrives as evdev events
    c.send("dd if=/dev/input/event1 bs=24 count=3 2>/dev/null | wc -c > /tmp/ev &\n")
    c.expect(PROMPT, 10)
    time.sleep(0.5)
    monitor(c.args, "mouse_move 10 20", "mouse_move 5 5")
    out, rc = c.run("sleep 1; cat /tmp/ev")
    assert "72" in out, "mouse events: " + out
    # storage: raw contents match the image; FAT on the first partition
    out, rc = c.run("dd if=/dev/sda2 bs=65536 count=64 2>/dev/null | md5sum; dd if=/dev/vda2 bs=65536 count=64 2>/dev/null"
                    " | md5sum; mkdir -p /mnt/usb && (fatfsd /dev/sda1 /mnt/usb &) && "
                    "for i in $(seq 1 50); do grep -q /mnt/usb /proc/mounts && break; sleep 0.1; done; "
                    "cat /mnt/usb/hello.txt && dd if=/dev/urandom of=/tmp/u bs=1024 count=1024 2>/dev/null && "
                    "cp /tmp/u /mnt/usb/u.bin && sync && md5sum /tmp/u /mnt/usb/u.bin; kill $(pidof fatfsd | tr ' ' '\\n' | tail -1)")
    sums = re.findall(r"([0-9a-f]{32})", out)
    assert len(sums) == 4 and sums[0] == sums[1] and sums[2] == sums[3] and "Hello from FAT32" in out, out
    # the drive sits behind a hub: unplugging the hub removes both
    out, rc = c.run("dmesg | grep -E 'port USB 1.1 hub|HARDDRIVE.*hub'")
    assert rc == 0 and "8-port" in out and "hub 3 port 2" in out, out
    monitor(c.args, "device_del usbhub")
    out, rc = c.run("for i in $(seq 1 30); do [ $(dmesg | grep -c 'disconnected') -ge 2 ] && break; sleep 0.1; done; "
                    "dmesg | grep -E 'disconnected|removed'; dd if=/dev/sda of=/dev/null bs=512 skip=190000 count=1")
    assert "usb 3: disconnected" in out and "usb 4: disconnected" in out and rc != 0, out


def t_rtc(c):
    """PL031 on virt: the boot sets the system time from it, setting the
    time writes it back, and hwclock reads it through /dev/rtc0."""
    if c.args.machine != "virt":
        return
    out, rc = c.run("dmesg | grep rtc0; date -u +%Y")
    assert "system time set to" in out and int(out.strip().split()[-1]) >= 2024, out
    out, rc = c.run("date -u -s '2031-05-06 07:08:09' >/dev/null && hwclock -r -u")
    assert rc == 0 and "May  6 07:08" in out and "2031" in out, out


def t_latency(c):
    """Timer wake-up latency and pipe round trips; the budget is loose (QEMU
    TCG timing is only meaningful relatively; budgets for the Pi are in
    docs)."""
    out, rc = c.run("olux-latency -d 2 -n 2000 -l 250000", timeout=120)
    assert rc == 0 and re.search(r"timer: samples \d+ min", out) and re.search(r"ipc: samples 2000 ", out), out


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
    t_memory_stress, t_fork_bomb_limited, t_block_device, t_fatfs, t_ext4fs, t_fs_server_restart, t_network, t_ntp, t_usb, t_rtc, t_latency, t_rpi_platform, t_init_respawn,
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


def t_crash_recovery(args):
    """Separate boot that may reboot: a kernel panic reboots the machine and
    the next boot finds the crash in /proc/last_kmsg (pstore); then a
    watchdog expiry is recorded the same way."""
    if args.machine != "virt":
        return
    boot_args = argparse.Namespace(**vars(args))
    boot_args.allow_reboot = True
    c = Console(qemu_cmd(boot_args, "panic=1 init.watchdog=0"), os.path.join(args.logdir, "crash.log"))
    try:
        c.expect(PROMPT, 90)
        c.quiet()
        c.send("echo c > /proc/sysrq-trigger\n")
        c.expect(rb"Kernel panic - not syncing: sysrq triggered crash", 20)
        c.expect(rb"OluxOS \d", 30)  # the next boot's banner
        c.expect(PROMPT, 90)
        c.quiet()
        out, rc = c.run("dmesg | grep pstore; grep -c 'sysrq triggered crash' /proc/last_kmsg")
        assert "ended with a kernel panic" in out and "boot #2" in out and out.strip().endswith("1"), out
        c.run("watchdog -T 2 -t 1 /dev/watchdog && sleep 1 && kill -9 $(pidof watchdog)")
        c.expect(rb"no keepalive", 20)
        c.expect(PROMPT, 90)
        c.quiet()
        out, rc = c.run("dmesg | grep 'previous boot'; head -1 /proc/last_kmsg")
        assert "boot #2 ended with a watchdog reset" in out, out
    finally:
        c.close()


def t_sdcard(args):
    """Separate raspi4b boot from the `make sdcard` image: boot slot A at
    /boot, writable data partition at /data."""
    img = os.path.join(args.out, "sdcard.img")
    boot_args = argparse.Namespace(**vars(args))
    boot_args.sd = img
    # the firmware would pass slot A's cmdline.txt
    # (and a trip point below QEMU's constant 25 'C, to see the thermal governor act)
    c = Console(qemu_cmd(boot_args, "olux.slot=a rpi_thermal.trip=20000"), os.path.join(args.logdir, "sdcard.log"))
    c.args = boot_args
    try:
        c.expect(PROMPT, 90)
        c.quiet()
        wait_mount(c, "/data")
        wait_mount(c, "/boot")
        out, rc = c.run("ls /boot && echo ok > /data/probe && cat /data/probe /data/README.txt")
        assert rc == 0 and "kernel8.img" in out and "start4.elf" in out and "ok" in out, out
        out, rc = c.run("sleep 2; dmesg | grep rpi-thermal")
        assert "trip point: ARM clock" in out, out
    finally:
        c.close()


def mbr_partition_offset(img, n):
    import struct
    with open(img, "rb") as f:
        mbr = f.read(512)
    return struct.unpack_from("<I", mbr, 446 + 16 * (n - 1) + 8)[0] * 512


def power_off(c):
    c.send("poweroff\n")
    deadline = time.time() + 30
    while c.proc.poll() is None and time.time() < deadline:
        try:
            c._fill(0.5)
        except EOFError:
            break
    c.proc.wait(timeout=10)


def t_ab_update(args):
    """A/B update on the raspi4b SD card: a signed bundle goes into the
    inactive slot (bad signatures and tampered files are refused); the
    next boot from that slot (as the firmware's tryboot would pick it)
    confirms it as the default in autoboot.txt."""
    import shutil
    import tarfile
    import tempfile
    # scratch files (keys, bundles, a card image) stay out of the uploaded logs
    tmp = tempfile.mkdtemp(prefix="olux-ab-")
    try:
        _ab_update(args, tmp, shutil, tarfile)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _ab_update(args, tmp, shutil, tarfile):
    img = os.path.join(tmp, "ab-sdcard.img")
    shutil.copyfile(os.path.join(args.out, "sdcard.img"), img)
    key = os.path.join(args.out, "keys", "update-dev.pem")
    good = os.path.join(tmp, "update.tar")
    subprocess.run(["scripts/mkupdate.sh", os.path.join(args.out, "rpi4"), key, good, "ab-test-2"], check=True,
                   stderr=subprocess.DEVNULL)
    other_key = os.path.join(tmp, "other.pem")
    subprocess.run(["openssl", "genpkey", "-algorithm", "ed25519", "-out", other_key], check=True)
    badsig = os.path.join(tmp, "badsig.tar")
    subprocess.run(["scripts/mkupdate.sh", os.path.join(args.out, "rpi4"), other_key, badsig, "evil"], check=True,
                   stderr=subprocess.DEVNULL)
    tampered = os.path.join(tmp, "tampered.tar")
    with tarfile.open(good) as src, tarfile.open(tampered, "w") as dst:
        for m in src.getmembers():
            data = src.extractfile(m).read() if m.isfile() else None
            if m.name == "files/config.txt":
                data += b"# tampered\n"
                m.size = len(data)
            dst.addfile(m, io.BytesIO(data) if data is not None else None)
    off = mbr_partition_offset(img, 4)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    for f in (good, badsig, tampered):
        subprocess.run(["mcopy", "-i", "%s@@%d" % (img, off), f, "::/"], check=True, env=env)
    # an SSH key dropped on the boot partition, as a user would
    keyfile = os.path.join(tmp, "authorized_keys")
    with open(keyfile, "w") as f:
        f.write("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOluxosTestKeyOnly0000000000000000000000 test@host\n")
    subprocess.run(["mcopy", "-i", "%s@@%d" % (img, mbr_partition_offset(img, 2)), keyfile, "::/"], check=True,
                   env=env)
    boot_args = argparse.Namespace(**vars(args))
    boot_args.sd = img
    boot_args.sd_persist = True
    for slot in ("a", "b"):
        c = Console(qemu_cmd(boot_args, "olux.slot=" + slot), os.path.join(args.logdir, "ab-%s.log" % slot))
        c.args = boot_args
        try:
            c.expect(PROMPT, 90)
            c.quiet()
            wait_mount(c, "/boot")
            wait_mount(c, "/data")
            out, rc = c.run("cat /boot/cmdline.txt")
            assert "olux.slot=" + slot in out, out
            if slot == "a":
                out, rc = c.run("for i in $(seq 1 50); do [ -f /data/ssh/dropbear_ed25519_host_key ] && break; "
                                "sleep 0.2; done; grep -c OluxosTestKey /root/.ssh/authorized_keys; "
                                "ls /data/ssh; pidof dropbear")
                assert "dropbear_ed25519_host_key" in out and re.search(r"^1\r?$", out, re.M) and rc == 0, out
                out, rc = c.run("olux-update install /data/badsig.tar")
                assert rc != 0 and "bad signature" in out, out
                out, rc = c.run("olux-update install /data/tampered.tar")
                assert rc != 0 and "do not match" in out, out
                out, rc = c.run("olux-update install /data/update.tar && olux-update status")
                assert rc == 0 and "into slot b" in out and "other slot:   b (ab-test-2)" in out, out
                assert "default slot: a" in out, out
            else:
                out, rc = c.run("cat /boot/VERSION; olux-update confirm --now && olux-update status")
                assert rc == 0 and "ab-test-2" in out and "slot b is now the default" in out, out
                assert "default slot: b" in out, out
            power_off(c)
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
    args.fwd_http, args.fwd_telnet, args.fwd_ssh = free_port(), free_port(), free_port()
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

    if (not args.pattern or "crash" in args.pattern) and args.machine == "virt":
        try:
            t_crash_recovery(args)
            print("PASS crash_recovery")
        except (AssertionError, TimeoutError, EOFError) as e:
            failures.append("crash_recovery")
            print("FAIL crash_recovery %s" % e)

    sdcard = os.path.join(args.out, "sdcard.img")
    if (not args.pattern or "sdcard" in args.pattern) and args.machine == "raspi4b" and os.path.exists(sdcard):
        try:
            t_sdcard(args)
            print("PASS sdcard")
        except (AssertionError, TimeoutError, EOFError) as e:
            failures.append("sdcard")
            print("FAIL sdcard %s" % e)

    if (not args.pattern or "ab_update" in args.pattern) and args.machine == "raspi4b" and os.path.exists(sdcard):
        try:
            t_ab_update(args)
            print("PASS ab_update")
        except (AssertionError, TimeoutError, EOFError, subprocess.CalledProcessError) as e:
            failures.append("ab_update")
            print("FAIL ab_update %s" % e)

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
