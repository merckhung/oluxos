# Booting OluxOS on Physical Raspberry Pi 4B

This guide describes how to build and deploy OluxOS to a physical Raspberry Pi 4B board.

## Prerequisites

1.  **Micro SD Card**: Formatted as FAT32.
2.  **USB-to-UART Serial Cable**: (e.g., PL2303, CP2102) to connect to the Pi's GPIO pins.
3.  **Raspberry Pi 4 Firmware**:
    *   Download `start4.elf` and `fixup4.dat` from the official [Raspberry Pi Firmware repository](https://github.com/raspberrypi/firmware/tree/master/boot).
    *   Download the Bluetooth disable overlay `disable-bt.dtbo` from the [overlays directory](https://github.com/raspberrypi/firmware/blob/master/boot/overlays/disable-bt.dtbo).

## Step 1: Build the Boot Package

Run the following Bazel command in the workspace root:

```bash
bazel build :rpi4_boot_zip
```

This target compiles the kernel with Raspberry Pi 4 support, converts the ELF to raw binary (`kernel8.img`), and packages it with the RAM disk (`fat.img`) and a pre-configured `config.txt` into `bazel-bin/rpi4_boot.zip`.

## Step 2: Prepare the SD Card

1.  Insert your FAT32-formatted SD card.
2.  Copy the official firmware files to the root of the SD card:
    *   `start4.elf`
    *   `fixup4.dat`
3.  Create a folder named `overlays` in the root of the SD card.
4.  Copy `disable-bt.dtbo` into the `overlays/` folder.
5.  Extract the contents of `bazel-bin/rpi4_boot.zip` directly into the root of the SD card. This will place:
    *   `kernel8.img` (The OluxOS kernel)
    *   `fat.img` (The filesystem ramdisk)
    *   `config.txt` (Bootloader configuration)

The resulting SD card directory structure should look like this:

```
/ (SD Card Root)
├── start4.elf
├── fixup4.dat
├── kernel8.img
├── fat.img
├── config.txt
└── overlays/
    └── disable-bt.dtbo
```

## Step 3: Connect Serial Console

Connect your USB-to-UART adapter to the Raspberry Pi 4B GPIO header:

*   **TX** (Adapter) -> **Pin 10** (GPIO 15 / RXD0)
*   **RX** (Adapter) -> **Pin 8** (GPIO 14 / TXD0)
*   **GND** (Adapter) -> **Pin 6** (GND)

*Note: We connect TX to RX and RX to TX.*

Open your terminal emulator (e.g., `screen`, `minicom`, `picocom`, or PuTTY) with the following settings:
*   **Baud Rate**: 115200
*   **Data bits**: 8
*   **Parity**: None
*   **Stop bits**: 1
*   **Flow Control**: None

## Step 4: Boot

Insert the SD card into the Raspberry Pi 4B and plug in the power. You should see the bootloader debug logs (if any) followed by the OluxOS kernel booting and launching the BusyBox shell.
