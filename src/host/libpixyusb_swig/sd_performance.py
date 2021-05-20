#!/usr/bin/python

##
# @file sd_performance.py
# @brief This script extracts the write timings from the recorded sessions on the SD Card.
#
# @copyright Copyright 2021 Matternet. All rights reserved.
#

import argparse
from image_player import Player, FRAMES_PER_SESSION
import pixy
import sys


def main(session):
    # Initialize Pixy interface
    if pixy.pixy_init() < 0:
        print("Failed to initialize USB interface")
        sys.exit(-1)

    # Stop default program
    pixy.pixy_command("stop")

    player = Player(session)
    hdr0 = player.get_image_header(session, 0)

    sum = 0
    max = 0
    min = 999999999
    cnt = 0
    above20 = 0

    for i in range(FRAMES_PER_SESSION):
        hdr = player.get_image_header(session, i)
        if (hdr is None) or (hdr.session_cnt != hdr0.session_cnt):
            break
        if hdr.last_write_time_us == 0:
            continue

        cnt = cnt + 1
        sum = sum + hdr.last_write_time_us
        if hdr.last_write_time_us > max:
            max = hdr.last_write_time_us
        elif hdr.last_write_time_us < min:
            min = hdr.last_write_time_us
        if hdr.last_write_time_us > 20000:
            above20 = above20 + 1

    print("cnt: " + str(cnt))
    print("min: {} ms".format(min/1000.0))
    print("max: {} ms".format(max/1000.0))
    print("avg: {} ms".format(sum/float(cnt)/1000.0))
    print("above 20ms: {} %".format(above20 * 100.0 / cnt))

    # Close connection to Pixy
    pixy.pixy_close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-s', '--session', default=0)
    args = parser.parse_args()
    main(int(args.session))
