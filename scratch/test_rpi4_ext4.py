import os
import socket
import subprocess
import threading
import time

shell_ready_event = threading.Event()
success = False
ext4_detected = False


def read_stdout(proc):
  global success, ext4_detected
  buffer = ""
  while True:
    char = proc.stdout.read(1)
    if not char:
      break
    buffer += char
    if char == "\n":
      line = buffer.strip()
      print(f"QEMU: {line}")
      if "Detected EXT4 Filesystem" in line:
        ext4_detected = True
      if "Hello from EXT4 file!" in line:
        success = True
      buffer = ""
    else:
      if buffer.endswith("OluxOS > "):
        print(f"QEMU: {buffer}")
        shell_ready_event.set()
        buffer = ""


def main():
  global success, ext4_detected
  print("Creating EXT4 image...")
  # Create 16MB ext4.img with 4KB block size
  subprocess.run(
      ["dd", "if=/dev/zero", "of=ext4.img", "bs=1M", "count=16"], check=True
  )
  subprocess.run(["mkfs.ext4", "-b", "4096", "-F", "ext4.img"], check=True)

  # Create temp test file
  with open("hello_ext4.txt", "w") as f:
    f.write("Hello from EXT4 file!\nThis is line 2.\n")

  # Write file to image using debugfs
  print("Writing hello.txt to EXT4 image...")
  # debugfs -w -R "write hello_ext4.txt hello.txt" ext4.img
  subprocess.run(
      [
          "debugfs",
          "-w",
          "-R",
          "write hello_ext4.txt hello.txt",
          "ext4.img",
      ],
      check=True,
  )

  print("Launching OluxOS RPi4 in QEMU with EXT4 image...")
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
          "-device",
          "loader,file=ext4.img,addr=0x48000000,force-raw=on",
      ],
      stdin=subprocess.PIPE,
      stdout=subprocess.PIPE,
      stderr=subprocess.STDOUT,
      text=True,
      bufsize=0,
  )

  t = threading.Thread(target=read_stdout, args=(proc,))
  t.daemon = True
  t.start()

  # Wait for shell prompt
  print("Waiting for OluxOS shell prompt...")
  if not shell_ready_event.wait(timeout=15):
    print("Timeout waiting for shell prompt!")
    proc.kill()
    cleanup()
    exit(1)

  # Send 'cat hello.txt' command
  print("Sending 'cat hello.txt' command...")
  proc.stdin.write("cat hello.txt\n")
  proc.stdin.flush()

  # Wait a bit for output
  time.sleep(2)

  # Terminate QEMU
  print("Terminating QEMU...")
  proc.kill()

  cleanup()

  if ext4_detected and success:
    print("EXT4 Test PASSED!")
    exit(0)
  else:
    print(
        f"EXT4 Test FAILED! ext4_detected={ext4_detected}, success={success}"
    )
    exit(1)


def cleanup():
  if os.path.exists("ext4.img"):
    os.remove("ext4.img")
  if os.path.exists("hello_ext4.txt"):
    os.remove("hello_ext4.txt")


if __name__ == "__main__":
  main()
