#!/usr/bin/python

##
# @file image_player.py
# @brief This script reads images from the Pixy's SD Card over USB.
#
# @copyright Copyright 2021 Matternet. All rights reserved.
#

import crcmod
import cv2
import numpy as np
import pixy
import struct
import sys


BYTES_PER_BLOCK = 512
FRAME_WIDTH = 320
FRAME_HEIGHT = 200
FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT
FRAMES_PER_SESSION = 15000
BLOCKS_PER_FRAME = FRAME_BYTES / BYTES_PER_BLOCK
SESSION_BLOCK_START = 2
MAX_SESSIONS = 32
TIMESTAMP_LEN = 6
HEADER_LEN = 14


def verify_header(hdr):
    magic, version, boot_cnt, crc16 = struct.unpack_from('<4sIIH', hdr)
    crc_func = crcmod.predefined.Crc('crc-ccitt-false')
    crc_func.update(hdr[:HEADER_LEN-2])
    calc_crc16 = int(crc_func.hexdigest(), 16)
    print(magic, version, boot_cnt, crc16, calc_crc16)
    return calc_crc16 == crc16


def read_header(block_num):
    data = pixy.byteArray(BYTES_PER_BLOCK)
    pixy.pixy_read_blocks(block_num, 1, data)
    return pixy.cdata(data, HEADER_LEN)


def show_image(session, index):
    # Calculate the block to read from SD Card
    session = session % MAX_SESSIONS
    session_block = SESSION_BLOCK_START + (session * BLOCKS_PER_FRAME * FRAMES_PER_SESSION)
    block_num = session_block + (index * BLOCKS_PER_FRAME)

    # Grab image over USB interface
    data = pixy.byteArray(FRAME_BYTES)
    pixy.pixy_read_blocks(block_num, BLOCKS_PER_FRAME, data)

    # Extract the timestamp from the image data and make the pixels black
    timestamp = pixy.cdata(data, TIMESTAMP_LEN)
    boot_cnt, time_ms = struct.unpack_from('>HI', timestamp)
    for i in xrange(TIMESTAMP_LEN):
        data[i] = 0

    print boot_cnt, time_ms

    # Convert to numpy matrix
    frame = np.zeros((FRAME_HEIGHT, FRAME_WIDTH, 1), dtype=np.uint8)
    for h in xrange(FRAME_HEIGHT):
        for w in xrange(FRAME_WIDTH):
            frame[h,  w] = data[h * FRAME_WIDTH + w]

    # Show image and quit if user hits the Q key
    cv2.imshow('pixy', frame)
    if cv2.waitKey(10) == ord('q'):
        sys.exit(-1)


def main():
    pixy.pixy_init()            # Initialize Pixy interface
    pixy.pixy_command("stop")   # Stop default program

    # Get HeaderA
    header = read_header(0)
    verify_header(header)

    # Get HeaderB
    header = read_header(1)
    verify_header(header)

    # DEMO: Loop through the first 20 images of every session
    for session in xrange(32):
        print("Session: " + str(session))
        for frame in xrange(20):
            show_image(session, frame)

    pixy.pixy_close()


if __name__ == '__main__':
    main()
