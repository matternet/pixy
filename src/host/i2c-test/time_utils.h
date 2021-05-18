/**
 * @file time_utils.h
 * @brief Functions related to time.
 * @copyright Copyright © 2021 Matternet. All rights reserved.
 */

#ifndef __TIME_UTILS_H__
#define __TIME_UTILS_H__

#include <stdint.h>
#include <time.h>

#define TIMESPEC_TO_MICRO64(ts) \
    (uint64_t)((uint64_t)(ts)->tv_sec * (uint64_t)1000000 + (uint64_t)(ts)->tv_nsec / (uint64_t)1000L)

inline uint64_t time_monotonic_us()
{
    struct timespec time = {0,0};
    (void)clock_gettime(CLOCK_MONOTONIC, &time);
    return TIMESPEC_TO_MICRO64(&time);
}

inline uint64_t time_monotonic_ms()
{
    return time_monotonic_us() / 1000;
}

#define msleep(ms)  usleep(ms * 1000)

#endif  // __TIME_UTILS_H__
