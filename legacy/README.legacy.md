# OluxOS

OluxOS is a minimalist, multi-architecture operating system structured around a **microkernel design**. Originally created as a simple monolithic IA32 kernel, it has been modernized into a platform featuring synchronous IPC, userspace driver/FS servers, a software-based OpenGL ES 1.1 graphics stack, a GUI window manager, and support for running a Linux-compatible **BusyBox** shell.

---

## 1. Architectural Highlights

*   **Microkernel Core**: The kernel minimizes supervisor-mode operations, providing thread scheduling, virtual memory management, and synchronous IPC (`SYS_SEND`/`SYS_RECV`).
*   **Userspace Drivers & Services**:
    *   **UART Driver (TID 1)**: Interacts with the serial console hardware via mapped MMIO.
    *   **Ramdisk Driver (TID 2)**: Exposes raw disk sectors to the filesystem server.
    *   **FS Server (TID 3)**: Auto-detects and mounts **FAT32** or **EXT4** (read-only) filesystems, managing open file tables and directory lookups.
*   **POSIX Compatibility Library (`ulib`)**: Implements standard headers (`<unistd.h>`, `<stdio.h>`, `<dirent.h>`, `<string.h>`) so that applications can interact with servers using familiar APIs like `open()`, `read()`, `write()`, `opendir()`, `fork()`, and `exec()`.
*   **Software-Based OpenGL ES 1.1 Rasterizer & GUI**: 
    *   A custom fixed-point (`GLfixed`) software-rendering library (`gles.c`) drawing points, lines, flat/Gouraud shaded triangles without hardware acceleration.
    *   A Window Manager (`gui.c`) supporting Z-ordered overlapping windows, title bars, buttons, scrollbars, and an interactive mouse cursor.
*   **Linux/BusyBox Shell Integration**: Boots directly into `/sh` (BusyBox) on the filesystem image using a custom ELF loader.

---

## 2. Supported Architectures & Platforms

OluxOS compiles and runs across several architectures on QEMU:

| Architecture | Target Machine | Core Components Enabled |
| :--- | :--- | :--- |
| **ARM64 (AArch64)** | QEMU `virt` / Physical Raspberry Pi 4B | Preemptive Multitasking, GICv2, Physical Timer, VC Mailbox Framebuffer, Userspace Servers, BusyBox. |
| **RISC-V 64-bit** | QEMU `virt` | Sv39 MMU Paging, preemption via Clint timer interrupts, RISC-V multitasking context switches. |
| **RISC-V 32-bit** | QEMU `virt` | Boot strap, MMU paging, interrupts, and cooperative multitasking. |
| **ARM32** | QEMU `virt` | Early boot, page table setup, GICv2, PL011 driver, scheduler. |
| **x86_64** | QEMU PC | PML4/PDPT paging, Long Mode transitions, GDT, interrupts, APIC, serial console, task execution. |
| **IA32** | QEMU PC | Original floppy-booting monolithic kernel with menu interface, PCI listing, and serial console. |

---

## 3. Prerequisites

To compile and execute the system, ensure the following are installed:

1.  **Bazel** (or `bazelisk`) for the build orchestration.
2.  **Toolchains**:
    *   `aarch64-linux-gnu-gcc` (for ARM64 targets)
    *   `riscv64-unknown-elf-gcc` (for RISC-V targets)
    *   `arm-linux-gnueabihf-gcc` (for ARM32 targets)
    *   `gcc` / `binutils` (for x86_64 / IA32 targets)
3.  **QEMU Simulators**:
    *   `qemu-system-aarch64`
    *   `qemu-system-riscv64`
    *   `qemu-system-i386`

---

## 4. Building & Running

OluxOS is compiled and executed via **Bazel** run configurations.

### ARM64 (QEMU Virt Machine)
*   **Build Kernel ELF**:
    ```bash
    bazel build //:arm64_kernel_elf
    ```
*   **Run in QEMU**:
    ```bash
    bazel run //:run_qemu_arm64
    ```

### Raspberry Pi 4B
*   **Build Deployable Boot ZIP**:
    ```bash
    bazel build //:rpi4_boot_zip
    ```
    *(Generates `bazel-bin/rpi4_boot.zip` containing `kernel8.img`, `fat.img`, and `config.txt` ready to copy to a FAT32 micro SD card.)*
*   **Run Pi 4 in QEMU**:
    ```bash
    bazel run //:run_qemu_rpi4
    ```

### RISC-V 64-bit
*   **Run in QEMU**:
    ```bash
    bazel run //:run_qemu_riscv64
    ```

### RISC-V 32-bit
*   **Run in QEMU**:
    ```bash
    bazel run //:run_qemu_riscv32
    ```

### ARM32
*   **Run in QEMU**:
    ```bash
    bazel run //:run_qemu_arm32
    ```

### x86_64
*   **Build Floppy Image**:
    ```bash
    bazel build //:OluxOS_img_x86_64
    ```

### IA32 (Original Floppy Target)
*   **Build Floppy Image**:
    ```bash
    bazel build //:OluxOS_img
    ```
*   **Run in QEMU**:
    ```bash
    bazel run //:run_qemu
    ```

---

## 5. Directory Layout

*   [`arch/`](file:///home/merck/Code/PACK/oluxos/arch): Platform-specific boot assembly, page table walkers, registers, and context switch mechanics.
*   [`driver/`](file:///home/merck/Code/PACK/oluxos/driver): Peripheral drivers (PL011/NS16550 serial, GICv2 interrupt handler, ARM timers, mailbox framebuffers).
*   [`kernel/`](file:///home/merck/Code/PACK/oluxos/kernel): Core PMM, virtual memory maps, spinlocks, and heap manager.
*   [`user/`](file:///home/merck/Code/PACK/oluxos/user): Userspace servers, standard library POSIX bindings (`ulib/`), shell built-in commands, and the ELF program loader.
*   [`utils/`](file:///home/merck/Code/PACK/oluxos/utils): Helper host tools (`krnimg`, `kdbger`, etc.) to bundle the kernel images.
