/**
 * @file i2c_utils.h
 * @brief Functions related to i2c.
 * @copyright Copyright © 2021 Matternet. All rights reserved.
 */

#ifndef __I2C_UTILS_H__
#define __I2D_UTILS_H__

#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

inline int i2c_open(const char *device)
{
    return open(device, O_RDWR);
}

inline int i2c_close(int fd)
{
    if (fd >= 0)
        return close(fd);
    return -1;
}

int i2c_transfer(int fd, uint8_t addr, uint8_t *buf, uint8_t len, bool is_read)
{
    if (fd < 0)
        return -1;

    if (len == 0)
        return 0;

    struct i2c_rdwr_ioctl_data packet;
    struct i2c_msg msg;
    msg.addr = addr;
    msg.len = len;
    msg.buf = buf;
    msg.flags = (is_read) ? I2C_M_RD : 0;
    packet.msgs = &msg;
    packet.nmsgs = 1;
    return ioctl(fd, I2C_RDWR, &packet);
}

inline int i2c_read(int fd, uint8_t addr, uint8_t *buf, uint8_t len)
{
    return i2c_transfer(fd, addr, buf, len, true);
}

inline int i2c_write(int fd, uint8_t addr, uint8_t *buf, uint8_t len)
{
    return i2c_transfer(fd, addr, buf, len, false);
}

#endif  // __I2C_UTILS_H__