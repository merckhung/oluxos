import socket
import subprocess
import time


def recv_until_prompt(sock):
  buffer = ""
  while True:
    data = sock.recv(1024).decode("utf-8", errors="ignore")
    if not data:
      break
    buffer += data
    if "(qemu)" in buffer:
      break
  return buffer


def main():
  print("Launching QEMU frozen...")
  proc = subprocess.Popen([
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
      "-S",
      "-monitor",
      "unix:qemu-monitor.sock,server,nowait",
  ])

  time.sleep(1)  # Wait for socket to create

  print("Connecting to monitor...")
  sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  sock.connect("qemu-monitor.sock")

  # Read initial prompt
  out = recv_until_prompt(sock)
  print(f"Initial Monitor:\n{out}")

  # Check registers
  print("Sending 'info registers'...")
  sock.sendall(b"info registers\n")
  out = recv_until_prompt(sock)
  print(f"Registers at start:\n{out}")

  # Step one instruction
  print("Sending 'singlestep on'...")
  sock.sendall(b"singlestep on\n")
  recv_until_prompt(sock)

  print("Sending 'c' (continue/step)...")
  sock.sendall(b"c\n")
  time.sleep(0.1)
  # Wait, 'c' with singlestep on will stop after 1 instr.
  # But we might need to send stop or it might stop automatically?
  # Actually 'singlestep' in QEMU monitor makes 'c' step one instruction.
  # Let's check registers again
  sock.sendall(b"info registers\n")
  out = recv_until_prompt(sock)
  print(f"Registers after step:\n{out}")

  sock.close()
  proc.kill()


if __name__ == "__main__":
  main()
