import os
import re
import socket
import subprocess
import threading
import time

fb_addr = None
fb_addr_event = threading.Event()
shell_ready_event = threading.Event()
gui_mode_event = threading.Event()
redraw_completed_event = threading.Event()


def read_stdout(proc):
  global fb_addr
  buffer = ""
  while True:
    char = proc.stdout.read(1)
    if not char:
      break
    buffer += char
    if char == "\n":
      line = buffer.strip()
      print(f"QEMU: {line}")
      if "FRAMEBUFFER initialized at physical:" in line:
        match = re.search(
            r"FRAMEBUFFER initialized at physical:\s+(0x[0-9a-fA-F]+)", line
        )
        if match:
          fb_addr = match.group(1)
          fb_addr_event.set()
      if "Entering interactive GUI mode." in line:
        gui_mode_event.set()
      if "Redraw after click completed." in line:
        redraw_completed_event.set()
      buffer = ""
    else:
      if buffer.endswith("OluxOS > "):
        print(f"QEMU: {buffer}")
        shell_ready_event.set()
        buffer = ""


def send_keys(proc, keys):
  print(f"Sending keys: {keys}")
  proc.stdin.write(keys)
  proc.stdin.flush()
  time.sleep(0.5)


def dump_fb(sock, addr, name):
  fb_raw_path = f"{name}.raw"
  if os.path.exists(fb_raw_path):
    os.remove(fb_raw_path)

  dump_cmd = f"pmemsave {addr} 921600 {fb_raw_path}\n"
  print(f"Sending monitor command: {dump_cmd.strip()}")
  sock.sendall(dump_cmd.encode("utf-8"))
  time.sleep(1.5)
  sock.recv(4096)  # Flush response

  fb_ppm_path = f"{name}.ppm"
  if os.path.exists(fb_raw_path):
    print(f"Creating {fb_ppm_path}...")
    with open(fb_ppm_path, "wb") as out_f:
      out_f.write(b"P6\n640 480\n255\n")
      with open(fb_raw_path, "rb") as in_f:
        out_f.write(in_f.read())
    os.remove(fb_raw_path)
    print(f"{fb_ppm_path} created successfully.")
  else:
    print(f"Failed to create {fb_raw_path}!")


def main():
  qemu_cmd = [
      "qemu-system-aarch64",
      "-M",
      "virt",
      "-cpu",
      "cortex-a53",
      "-m",
      "256",
      "-kernel",
      "bazel-bin/arm64_kernel.elf",
      "-nographic",
      "-device",
      "loader,file=fat.img,addr=0x48000000,force-raw=on",
      "-monitor",
      "unix:qemu-monitor.sock,server,nowait",
  ]

  print("Starting QEMU...")
  if os.path.exists("qemu-monitor.sock"):
    os.remove("qemu-monitor.sock")

  proc = subprocess.Popen(
      qemu_cmd,
      stdin=subprocess.PIPE,
      stdout=subprocess.PIPE,
      stderr=subprocess.STDOUT,
      text=True,
  )

  t = threading.Thread(target=read_stdout, args=(proc,))
  t.daemon = True
  t.start()

  # Wait for framebuffer address
  if not fb_addr_event.wait(timeout=10):
    print("Timeout waiting for framebuffer address!")
    proc.kill()
    exit(1)

  print(f"Found Framebuffer address: {fb_addr}")

  # Wait for shell
  if not shell_ready_event.wait(timeout=10):
    print("Timeout waiting for shell to be ready!")
    proc.kill()
    exit(1)

  # Launch GUI
  send_keys(proc, "gui\n")

  if not gui_mode_event.wait(timeout=10):
    print("Timeout waiting for GUI mode!")
    proc.kill()
    exit(1)

  print("Connecting to QEMU monitor...")
  sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  sock.connect("qemu-monitor.sock")
  time.sleep(0.5)
  sock.recv(4096)  # greeting

  # 1. Dump Initial Frame (Cursor at center, Window 2 active)
  print("Dumping initial frame...")
  dump_fb(sock, fb_addr, "gui_initial")

  # 2. Move cursor left (15 times 'j') and up (10 times 'i')
  # moves X: 320 -> 320 - 15*15 = 95
  # moves Y: 240 -> 240 - 10*15 = 90
  print("Moving cursor to Window 1...")
  send_keys(proc, "j" * 15 + "i" * 10)

  # Dump Moved Frame (Cursor at (95, 90) over Window 1)
  print("Dumping moved frame...")
  dump_fb(sock, fb_addr, "gui_moved")

  # 3. Click (f) to activate Window 1
  print("Clicking to activate Window 1...")
  send_keys(proc, "f")

  # Wait for redraw completion
  if not redraw_completed_event.wait(timeout=10):
    print("Timeout waiting for click redraw completion!")
    proc.kill()
    exit(1)

  # Dump Clicked Frame (Window 1 active and on top, Window 2 inactive)
  print("Dumping clicked frame...")
  dump_fb(sock, fb_addr, "gui_clicked")

  # 4. Quit GUI
  print("Quitting GUI...")
  send_keys(proc, "q")

  sock.close()
  proc.kill()
  proc.wait()
  print("Test completed.")


if __name__ == "__main__":
  main()
