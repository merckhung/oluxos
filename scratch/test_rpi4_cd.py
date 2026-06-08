import os
import socket
import subprocess
import threading
import time

shell_ready_event = threading.Event()
output_lines = []


def read_stdout(proc):
  buffer = ""
  while True:
    char = proc.stdout.read(1)
    if not char:
      break
    buffer += char
    if char == "\n":
      line = buffer.strip()
      print(f"QEMU: {line}")
      output_lines.append(line)
      buffer = ""
    else:
      if buffer.endswith("OluxOS > "):
        print(f"QEMU: {buffer}")
        output_lines.append(buffer)
        shell_ready_event.set()
        buffer = ""


def main():
  print("Creating EXT4 image...")
  # Create 16MB ext4.img with 4KB block size
  subprocess.run(
      ["dd", "if=/dev/zero", "of=ext4.img", "bs=1M", "count=16"], check=True
  )
  subprocess.run(["mkfs.ext4", "-b", "4096", "-F", "ext4.img"], check=True)

  # Create temp test file
  with open("hello_ext4.txt", "w") as f:
    f.write("Hello from nested EXT4 file!\n")

  # Write directory and file to image using debugfs
  print("Creating dir1 and writing hello.txt to EXT4 image...")
  subprocess.run(
      ["debugfs", "-w", "-R", "mkdir /dir1", "ext4.img"], check=True
  )
  subprocess.run(
      [
          "debugfs",
          "-w",
          "-R",
          "write hello_ext4.txt /dir1/hello.txt",
          "ext4.img",
      ],
      check=True,
  )
  print("Filesystem layout:")
  subprocess.run(["dumpe2fs", "ext4.img"])

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

  # Send commands sequentially and wait for output
  def send_cmd(cmd):
    shell_ready_event.clear()
    print(f"Sending '{cmd}'...")
    proc.stdin.write(cmd + "\n")
    proc.stdin.flush()
    # Wait for the shell prompt to appear again
    if not shell_ready_event.wait(timeout=5):
      print(f"Timeout waiting for prompt after '{cmd}'!")

  # 1. pwd (expect '/')
  send_cmd("pwd")
  # 2. cd dir1
  send_cmd("cd dir1")
  # 3. pwd (expect '/dir1')
  send_cmd("pwd")
  # 4. ls (expect 'hello.txt')
  send_cmd("ls")
  # 5. cat hello.txt (expect 'Hello from nested EXT4 file!')
  send_cmd("cat hello.txt")
  # 6. cd ..
  send_cmd("cd ..")
  # 7. pwd (expect '/')
  send_cmd("pwd")

  # Terminate QEMU
  print("Terminating QEMU...")
  proc.kill()

  cleanup()

  # Verify the outputs
  success = True
  pwd_indices = [
      i for i, line in enumerate(output_lines) if "pwd" in line or line == "/" or line == "/dir1"
  ]
  print(f"Captured output lines related to test:")
  for idx in pwd_indices:
    print(f"[{idx}]: {output_lines[idx]}")

  # Check if '/dir1' exists in output
  if not any(line == "/dir1" for line in output_lines):
    print("FAILED: '/dir1' not printed by pwd!")
    success = False

  if not any("Hello from nested EXT4" in line for line in output_lines):
    print("FAILED: File content not printed by cat!")
    success = False

  # Check ls output inside /dir1 (should contain hello.txt)
  # Look for 'hello.txt' in output
  if not any(line == "hello.txt" for line in output_lines):
    print("FAILED: 'hello.txt' not listed by ls!")
    success = False

  if success:
    print("CD/PWD/LS/CAT Test PASSED!")
    exit(0)
  else:
    print("CD/PWD/LS/CAT Test FAILED!")
    exit(1)


def cleanup():
  if os.path.exists("ext4.img"):
    os.remove("ext4.img")
  if os.path.exists("hello_ext4.txt"):
    os.remove("hello_ext4.txt")


if __name__ == "__main__":
  main()
