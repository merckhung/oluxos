/* Raspberry Pi VideoCore firmware property interface (mailbox channel 8). */
#ifndef OLUX_RPI_FIRMWARE_H
#define OLUX_RPI_FIRMWARE_H

#include <olux/types.h>

enum rpi_fw_tag {
  RPI_FW_GET_FIRMWARE_REVISION = 0x00000001,
  RPI_FW_GET_BOARD_MODEL = 0x00010001,
  RPI_FW_GET_BOARD_REVISION = 0x00010002,
  RPI_FW_GET_BOARD_MAC_ADDRESS = 0x00010003,
  RPI_FW_GET_BOARD_SERIAL = 0x00010004,
  RPI_FW_GET_ARM_MEMORY = 0x00010005,
  RPI_FW_GET_VC_MEMORY = 0x00010006,
  RPI_FW_GET_POWER_STATE = 0x00020001,
  RPI_FW_SET_POWER_STATE = 0x00028001,
  RPI_FW_GET_CLOCK_STATE = 0x00030001,
  RPI_FW_GET_CLOCK_RATE = 0x00030002,
  RPI_FW_GET_MAX_CLOCK_RATE = 0x00030004,
  RPI_FW_GET_MIN_CLOCK_RATE = 0x00030007,
  RPI_FW_SET_CLOCK_RATE = 0x00038002,
  RPI_FW_GET_TEMPERATURE = 0x00030006,
  RPI_FW_GET_MAX_TEMPERATURE = 0x0003000a,
  RPI_FW_GET_THROTTLED = 0x00030046,
  RPI_FW_FB_ALLOCATE = 0x00040001,
  RPI_FW_FB_RELEASE = 0x00048001,
  RPI_FW_FB_GET_PITCH = 0x00040008,
  RPI_FW_FB_SET_PHYSICAL_WH = 0x00048003,
  RPI_FW_FB_SET_VIRTUAL_WH = 0x00048004,
  RPI_FW_FB_SET_DEPTH = 0x00048005,
  RPI_FW_FB_SET_PIXEL_ORDER = 0x00048006,
  RPI_FW_FB_SET_VIRTUAL_OFFSET = 0x00048009,
  RPI_FW_FB_GET_PHYSICAL_WH = 0x00040003,
};

enum rpi_fw_clock {
  RPI_CLK_EMMC = 1,
  RPI_CLK_UART = 2,
  RPI_CLK_ARM = 3,
  RPI_CLK_CORE = 4,
  RPI_CLK_EMMC2 = 12,
};

bool rpi_fw_available(void);

/* One tag: `data` holds the request (req_len bytes) and receives the
 * response (up to buf_len bytes). Returns the response length or -errno. */
int rpi_fw_property(u32 tag, void *data, u32 req_len, u32 buf_len);

/* A complete property message (size, code, tags..., end tag). */
int rpi_fw_message(void *msg, u32 len);

/* Convenience wrappers; return 0 or -errno. */
int rpi_fw_get_u32(u32 tag, u32 arg, u32 *out);
int rpi_fw_clock_rate(u32 clock, u32 *hz);
int rpi_fw_set_power(u32 device, bool on);

/* Bus address the VideoCore uses for physical address `pa`. */
u32 rpi_fw_bus_addr(phys_addr_t pa);

#endif
