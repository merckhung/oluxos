import math
import os
import re
import socket
import subprocess
import threading
import time
from PIL import Image

fb_addr = None
fb_addr_event = threading.Event()
scheduler_started_event = threading.Event()

font = {
    "O": [0x7C, 0x82, 0x82, 0x82, 0x82, 0x82, 0x7C, 0x00],
    "l": [0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x18, 0x00],
    "u": [0x00, 0x00, 0x84, 0x84, 0x84, 0x8C, 0x74, 0x00],
    "x": [0x00, 0x00, 0x84, 0x48, 0x30, 0x48, 0x84, 0x00],
    "o": [0x00, 0x00, 0x78, 0x84, 0x84, 0x84, 0x78, 0x00],
    "S": [0x78, 0x84, 0x80, 0x78, 0x04, 0x84, 0x78, 0x00],
    "s": [0x00, 0x00, 0x78, 0x80, 0x70, 0x08, 0xF0, 0x00],
    " ": [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
}


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
          print(f"Found Framebuffer Address: {fb_addr}")
          fb_addr_event.set()
      if "Starting scheduler..." in line:
        scheduler_started_event.set()
      buffer = ""


def ocr(image_path):
  img = Image.open(image_path).convert("1")  # Convert to binary (black/white)
  width, height = img.size
  pixels = img.load()

  # Find bounding box of white pixels
  min_x, max_x = width, 0
  min_y, max_y = height, 0

  for y in range(height):
    for x in range(width):
      if pixels[x, y] > 0:  # White pixel
        if x < min_x:
          min_x = x
        if x > max_x:
          max_x = x
        if y < min_y:
          min_y = y
        if y > max_y:
          max_y = y

  if max_x < min_x or max_y < min_y:
    print("No text found in image!")
    return ""

  bbox_w = max_x - min_x + 1
  bbox_h = max_y - min_y + 1
  print(
      f"Bounding box: ({min_x}, {min_y}) - ({max_x}, {max_y}), size"
      f" {bbox_w}x{bbox_h}"
  )

  scale = int(round(bbox_h / 7.0))
  if scale == 0:
    scale = 1
  print(f"Detected scale: {scale}")

  char_w = 8 * scale
  num_chars = int(math.ceil(bbox_w / char_w))
  print(f"Detected number of characters: {num_chars}")

  recognized = []

  for c_idx in range(num_chars):
    char_x = min_x + c_idx * char_w
    char_y = min_y

    bitmap = []
    for row in range(8):
      row_byte = 0
      for col in range(8):
        white_count = 0
        for sy in range(scale):
          for sx in range(scale):
            px = char_x + col * scale + sx
            py = char_y + row * scale + sy
            if px < width and py < height and pixels[px, py] > 0:
              white_count += 1
        if white_count > (scale * scale) / 2:
          row_byte |= 1 << (7 - col)
      bitmap.append(row_byte)

    matched_char = "?"
    for char, font_bytes in font.items():
      if bitmap == font_bytes:
        matched_char = char
        break
    recognized.append(matched_char)

  return "".join(recognized)


def main():
  print("Building RPi4 kernel...")
  subprocess.run(["bazel", "build", ":rpi4_kernel_elf"], check=True)

  print("Launching QEMU with monitor...")
  if os.path.exists("qemu-monitor.sock"):
    os.remove("qemu-monitor.sock")

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
          "-monitor",
          "unix:qemu-monitor.sock,server,nowait",
      ],
      stdout=subprocess.PIPE,
      stderr=subprocess.STDOUT,
      text=True,
      bufsize=0,
  )

  t = threading.Thread(target=read_stdout, args=(proc,))
  t.daemon = True
  t.start()

  print("Waiting for framebuffer address...")
  if not fb_addr_event.wait(timeout=10):
    print("Timeout waiting for framebuffer address!")
    proc.kill()
    exit(1)

  print("Waiting for scheduler to start...")
  if not scheduler_started_event.wait(timeout=5):
    print("Timeout waiting for scheduler to start!")
    proc.kill()
    exit(1)

  # Wait a tiny bit to ensure fb_init finished drawing
  time.sleep(0.5)

  print("Connecting to QEMU monitor...")
  sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  sock.connect("qemu-monitor.sock")

  # Discard greeting
  sock.recv(4096)

  fb_raw_path = "fb.raw"
  if os.path.exists(fb_raw_path):
    os.remove(fb_raw_path)

  # Framebuffer width=640, height=480, bpp=3. Size = 640 * 480 * 3 = 921600
  dump_cmd = f"pmemsave {fb_addr} 921600 {fb_raw_path}\n"
  print(f"Sending monitor command: {dump_cmd.strip()}")
  sock.sendall(dump_cmd.encode("utf-8"))

  # Wait for save to complete
  time.sleep(2)
  sock.close()

  print("Terminating QEMU...")
  proc.kill()

  fb_ppm_path = "fb.ppm"
  if os.path.exists(fb_raw_path):
    print(f"Creating {fb_ppm_path}...")
    with open(fb_ppm_path, "wb") as out_f:
      out_f.write(b"P6\n640 480\n255\n")
      with open(fb_raw_path, "rb") as in_f:
        out_f.write(in_f.read())
    os.remove(fb_raw_path)
    print("fb.ppm created successfully.")
  else:
    print("FAILED: fb.raw was not created!")
    exit(1)

  # Run OCR
  print("Running OCR on fb.ppm...")
  result = ocr(fb_ppm_path)
  print(f"OCR Result: {result}")

  if os.path.exists(fb_ppm_path):
    os.remove(fb_ppm_path)

  if result == "OluxOS":
    print("OCR Test SUCCESS!")
    exit(0)
  else:
    print("OCR Test FAILED!")
    exit(1)


if __name__ == "__main__":
  main()
