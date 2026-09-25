/* /dev/i2c-N ioctls (Linux ABI, as implemented by OluxOS). */
#ifndef _LINUX_I2C_DEV_H
#define _LINUX_I2C_DEV_H

#include <linux/i2c.h>
#include <linux/types.h>

#define I2C_RETRIES 0x0701
#define I2C_TIMEOUT 0x0702
#define I2C_SLAVE 0x0703
#define I2C_SLAVE_FORCE 0x0706
#define I2C_TENBIT 0x0704
#define I2C_FUNCS 0x0705
#define I2C_RDWR 0x0707
#define I2C_PEC 0x0708
#define I2C_SMBUS 0x0720

struct i2c_smbus_ioctl_data {
  __u8 read_write;
  __u8 command;
  __u32 size;
  union i2c_smbus_data *data;
};

struct i2c_rdwr_ioctl_data {
  struct i2c_msg *msgs;
  __u32 nmsgs;
};

#define I2C_RDWR_IOCTL_MAX_MSGS 42

#endif
