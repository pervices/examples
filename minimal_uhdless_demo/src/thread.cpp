//
// Copyright 2010-2011,2015 Ettus Research LLC
// Copyright 2018-2020 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "../include/thread.hpp"
#include <stdexcept>
#include <iostream>

#include <cmath>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>

static void check_priority_range(float priority)
{
    if (priority > +1.0 or priority < -1.0) {
        throw std::invalid_argument("priority out of range [-1.0, +1.0]");
    }
}

void uhd::set_thread_priority(float priority, bool realtime)
{
    if (realtime) {
        // Shift priority range so pseudo realtime threading can always be higher
        priority = ((priority + 1) * 0.25) + 0.5;
        check_priority_range(priority);

        set_thread_priority_non_realtime(priority);
    } else {
        // Shift priority range so pseudo realtime threading can always be higher
        if (priority > 0) {
            priority = (priority * 0.5);
        }
        check_priority_range(priority);

        set_thread_priority_non_realtime(priority);
    }
}

void uhd::set_thread_priority_non_realtime(float priority)
{
    int target_niceness = -(int)std::round(priority * 20);
    // Niceness is in a range of -20 to 19, to keep priority 0 as neutral the value is mapped to -20 to 20 then capped
    if (target_niceness == 20) {
        target_niceness = 19;
    }

    // Set to a schedueller that supports niceness
    int policy = SCHED_OTHER;

    sched_param sp;

    // Only realtime scheduling has priority levels
    sp.sched_priority = 0;
    int ret = pthread_setschedparam(pthread_self(), policy, &sp);

    if (ret != 0) {
        throw std::runtime_error("Error when attempting to call pthread_setschedparam to set the schedueller to SCHED_OTHER: " + std::string(strerror(errno)));
    }

    // As per the manual: set errno to 0 before calling nice
    // -1 is returned by nice on both error, and a valid return value
    // Use errno to figure out if -1 was a correct value or an error occured
    errno = 0;

    // Get current niceness
    int current_niceness = nice(0);
    if (current_niceness == -1 && errno != 0) {
        throw std::runtime_error("Failed to get current nice value: " + std::string(strerror(errno)));
    }

    int nice_change = target_niceness - current_niceness;

    errno = 0;
    // Shift nice value to the target
    int new_niceness = nice(nice_change);

    if (new_niceness == -1 && errno != 0) {
        throw std::runtime_error("Failed to set nice value: " + std::string(strerror(errno)));
    } else if (new_niceness != target_niceness) {
        throw std::runtime_error("Unable to set nice value to desired value. Target: " + std::to_string(target_niceness) + ", Actual: " + std::to_string(new_niceness));
    }
}

bool uhd::set_thread_priority_safe(float priority, bool realtime)
{
    try {
        set_thread_priority(priority, realtime);
        return true;
    } catch (const std::exception& e) {
        std::cout << "[UHD] WARNING: Unable to set the thread priority. Performance may be negatively affected: " << e.what() << std::endl;
        return false;
    }
}
