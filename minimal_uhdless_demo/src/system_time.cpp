//
// Copyright 2017 Ettus Research (National Instruments Corp.)
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../include/system_time.hpp"
#include <time.h>

time_spec_t get_system_time(void)
{
    struct ::timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    const auto seconds = time.tv_sec;
    const auto nanoseconds = time.tv_nsec;
    return time_spec_t(seconds, nanoseconds, 1e9);
}
