import subprocess
import threading
import time

shell_ready_event = threading.Event()
success = False


def read_stdout(proc):
  global success
  buffer = ""
  while True:
    char = proc.stdout.read(1)
    if not char:
      break
    buffer += char
    if char == "\n":
      line = buffer.strip()
      print(f"QEMU: {line}")
      if "Hello from FAT32 file!" in line:
        success = True
      buffer = ""
    else:
      if buffer.endswith("OluxOS > "):
        print(f"QEMU: {buffer}")
        shell_ready_event.set()
        buffer = ""


def main():
  global success
  print("Launching OluxOS RPi4 in QEMU...")
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
          "loader,file=fat.img,addr=0x48000000,force-raw=on",
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

  if success:
    print("Test PASSED!")
    exit(0)
  else:
    print("Test FAILED! Did not read expected file content.")
    exit(1)


if __name__ == "__main__":
  main()
