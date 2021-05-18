/**
 * @file blob_data.h
 * @brief Blob data structure read from I2C interface
 * @copyright Copyright © 2021 Matternet. All rights reserved.
 */

#ifndef __BLOB_DATA_HPP__
#define __BLOB_DATA_HPP__

#include "time_utils.h"
#include "i2c_utils.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define I2C_ADDR    0x54

// Protocol for reading blobs from Pixy
#define SYNC_BYTE0  0x55
#define SYNC_BYTE1  0xAA
#define SYNC_WORD   0xAA55

// Protocol for sending commands to Pixy
#define SER_SYNC_BYTE                 0xA5
#define SER_CMD_START_IMAGE_LOGGING   0xBE
#define SER_CMD_STOP_IMAGE_LOGGING    0xEF

/*
 * Blob structure from Pixy
 */
struct Blob
{
    uint16_t sync;
    uint16_t checksum;
    uint16_t signature;
    uint16_t x_center;
    uint16_t y_center;
    uint16_t width;
    uint16_t height;

    uint16_t calc_checksum()
    {
        return signature + x_center + y_center + width + height;
    }

    void print()
    {
        uint8_t *b = (uint8_t*)this;
        for (int i = 0; i < sizeof(Blob); ++i)
        {
            printf("%02x ", b[i]);
        }

        printf("[ %d, %d, %d, %d, %d, %d ]\n",
               checksum,
               signature,
               x_center,
               y_center,
               width,
               height);
    }
};


/*
 * Class to parse messages from Pixy
 */
class BlobParser
{
public:

    BlobParser()
        : i2c_(-1)
        , last_sync_time_(0)
        , buf_index_(0)
        , synced_(false)
    {
    }

    ~BlobParser()
    {
        if (i2c_ >= 0)
        {
            enable_image_logging(false);
            i2c_close(i2c_);
        }
    }

    int init(const char *i2c_dev, bool enable_logging)
    {
        i2c_ = i2c_open(i2c_dev);

        if (i2c_ >= 0 && enable_logging)
            enable_image_logging(true);

        return i2c_;
    }

    int run()
    {
        // If still looking for sync bytes then only read a minimal number of bytes (2)
        int read_len = (synced_) ? sizeof(Blob) : 2;
        if (read_data(read_len) < 0)
            return -1; // exit on any i2c error

        // Parse data received in buffer
        int bytes_parsed = parse_data();
        if (bytes_parsed <= 0)
        {
            // No valid messages found in buffer. Flush it and sleep for a bit
            buf_index_ = 0;
            msleep(5);
        }
        else
        {
            // Shift used bytes out
            buf_index_ -= bytes_parsed;
            memmove(buffer_, &buffer_[bytes_parsed], buf_index_);
        }

        return 0;
    }

private:

    void print_new_frame_separator()
    {
        printf("================================\n");
        printf("%" PRIu64 "\n", time_monotonic_ms() - last_sync_time_);
        last_sync_time_ = time_monotonic_ms();
    }

    int read_data(size_t len)
    {
        // Calculate free bytes in buffer
        size_t buf_avail = BUFFER_SIZE - buf_index_;
        if (len > buf_avail)
            len = buf_avail;

        // Read more data from I2C into buffer; not to exceed the max length.
        int ret = i2c_read(i2c_, I2C_ADDR, &buffer_[buf_index_], len);
        if (ret < 0)
            return ret;

        buf_index_ += len;
        return len;
    }

    int search_byte(uint8_t *buffer, int offset, int len, uint8_t byte)
    {
        int bytes_avail = len - offset;

        // return error if offset is beyond buffer length
        if (bytes_avail <= 0)
            return -1;

        // search for the particular byte in the buffer
        for (int i = offset; i < len; ++i)
            if (buffer[i] == byte)
                return i;

        return -1;
    }

    int parse_data()
    {
        int pos = 0;  // current position in buffer

        do
        {
            if (synced_ == false)  // search for sync bytes
            {
                // search for first sync byte
                pos = search_byte(buffer_, pos, buf_index_, SYNC_BYTE0);
                if (pos < 0)
                {
                    return -1;   // no sync byte found in buffer
                }
                else if (pos == buf_index_ - 1)
                {
                    return pos - 1;  // the last byte in the buffer is SYNC_BYTE0; need to read more
                }

                // search for second sync byte
                pos++;
                pos = search_byte(buffer_, pos, pos + 1, SYNC_BYTE1);
                if (pos < 0)
                {
                    return -1;   // no sync byte found in buffer
                }
                else
                {
                    print_new_frame_separator();
                    pos++;  // move to next byte
                    synced_ = true;
                    return pos;
                }
            }
            else  // sync bytes found. Blob data can be parsed now.
            {
                int rc;
                int bytes_avail = buf_index_ - pos;
                if (bytes_avail < sizeof(Blob))
                    break;  // not enough data for a Blob

                Blob *blob = (Blob*)&buffer_[pos];
                pos += sizeof(Blob);

                blob->print();

                // Zero byte means no more blob data
                if (blob->sync == 0)
                {
                    synced_ = false;
                    break;
                }
                else if (blob->sync != SYNC_WORD)
                {
                    synced_ = false;
                    printf("Invalid sync\n");
                    break;
                }

                // Sometimes the buffer can overlap to the next frame.
                if (blob->checksum == SYNC_WORD)
                {
                    // This is part of a next frame; revert back position
                    pos -= (sizeof(Blob) - 2);
                    print_new_frame_separator();
                }
                else if (blob->checksum != blob->calc_checksum())
                {
                    synced_ = false;
                    printf("Invalid checksum:  %d  %d\n", blob->checksum, blob->calc_checksum());
                    break;
                }
            }
        } while (true);

        return pos;
    }

    int enable_image_logging(bool enable)
    {
        uint8_t buf[2];
        buf[0] = SER_SYNC_BYTE;
        buf[1] = (enable) ? SER_CMD_START_IMAGE_LOGGING : SER_CMD_STOP_IMAGE_LOGGING;
        return i2c_write(i2c_, I2C_ADDR, buf, sizeof(buf));
    }

    int i2c_;
    uint64_t last_sync_time_;
    uint32_t buf_index_;
    uint8_t buffer_[1024];
    bool synced_;

    static const int BUFFER_SIZE = sizeof(buffer_);
};

#endif  // __BLOB_DATA_HPP__