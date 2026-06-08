import os
import re
import select
import subprocess
import threading
import time

pty_path = None
pty_found_event = threading.Event()


def read_qemu_stdout(proc):
  global pty_path
  buffer = ""
  while True:
    char = proc.stdout.read(1)
    if not char:
      break
    buffer += char
    if char == "\n":
      line = buffer.strip()
      print(f"QEMU STDOUT: {line}")
      m = re.search(r"char device redirected to (/dev/pts/\d+)", line)
      if m:
        pty_path = m.group(1)
        print(f"Found QEMU serial PTY: {pty_path}")
        pty_found_event.set()
      buffer = ""


def main():
  global pty_path
  print("Building RPi4 kernel...")
  subprocess.run(["bazel", "build", ":rpi4_kernel_elf"], check=True)

  print("Launching QEMU with -serial pty...")
  proc = subprocess.Popen(
      [
          "qemu-system-aarch64",
          "-M",
          "raspi4b",
          "-cpu",
          "cortex-a72",
          "-m",
          "2G",
          "-kernel",
          "bazel-bin/rpi4_kernel.elf",
          "-nographic",
          "-serial",
          "pty",
      ],
      stdout=subprocess.PIPE,
      stderr=subprocess.STDOUT,
      text=True,
      bufsize=0,
  )

  t = threading.Thread(target=read_qemu_stdout, args=(proc,))
  t.daemon = True
  t.start()

  print("Waiting for QEMU to print PTY path...")
  if not pty_found_event.wait(timeout=10):
    print("Timeout waiting for QEMU to redirect serial!")
    proc.kill()
    exit(1)

  print(f"Connecting to PTY: {pty_path}")
  fd = os.open(pty_path, os.O_RDWR | os.O_NOCTTY | os.O_NDELAY)

  # Configure PTY to 115200 baud, 8N1 raw mode
  import termios

  attrs = termios.tcgetattr(fd)
  attrs[4] = termios.B115200
  attrs[5] = termios.B115200
  attrs[3] &= ~(
      termios.ICANON
      | termios.ECHO
      | termios.ECHOE
      | termios.ISIG
      | termios.IEXTEN
  )
  attrs[0] &= ~(
      termios.IXON | termios.IXOFF | termios.IXANY | termios.ICRNL | termios.INPCK
  )
  attrs[1] &= ~termios.OPOST
  termios.tcsetattr(fd, termios.TCSANOW, attrs)

  # Read and print boot logs for 3 seconds
  print("Reading boot logs from PTY...")
  start_time = time.time()
  boot_logs = ""
  while time.time() - start_time < 3.0:
    r, _, _ = select.select([fd], [], [], 0.1)
    if fd in r:
      data = os.read(fd, 1024)
      boot_logs += data.decode("utf-8", errors="ignore")

  print("--- BOOT LOGS START ---")
  print(boot_logs)
  print("--- BOOT LOGS END ---")

  # Flush input buffer to clear any remaining logs
  termios.tcflush(fd, termios.TCIFLUSH)

  # Send KDBGER_REQ_CONNECT
  req_packet = b"\x01\x00\x00\x00\x08\x00\x00\x00"
  print("Sending KDBGER_REQ_CONNECT packet...")
  os.write(fd, req_packet)

  # Read response (expect 8 bytes)
  print("Waiting for response...")
  r, _, _ = select.select([fd], [], [], 3.0)
  if fd in r:
    rsp = os.read(fd, 8)
    print(f"Received response: {rsp}")
    expected = b"\x02\x00\x00\x00\x08\x00\x00\x00"
    if rsp == expected:
      print("SUCCESS: KDBGER connection test PASSED!")
      test_passed = True
    else:
      print(f"FAILED: Expected {expected}, got {rsp}")
      test_passed = False
  else:
    print("FAILED: Timeout waiting for response!")
    test_passed = False

  os.close(fd)
  print("Terminating QEMU...")
  proc.kill()

  if test_passed:
    exit(0)
  else:
    exit(1)


if __name__ == "__main__":
  main()
