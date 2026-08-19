//
// Copyright 2010,2017 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
// Copyright 2026 Per Vices Corporation
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

constexpr float DEFAULT_THREAD_PRIORITY = float(0.5);

/*!
 * Set the scheduling priority on the current thread.
 *
 * A priority of zero corresponds to normal priority.
 * Positive priority values are higher than normal.
 * Negative priority values are lower than normal.
 *
 * \param priority a value between -1 and 1
 * \param realtime true to use realtime mode
 * \throw exception on set priority failure
 */
void set_thread_priority(
    float priority = DEFAULT_THREAD_PRIORITY, bool realtime = true);

void set_thread_priority_non_realtime(float priority);

/*!
 * Set the scheduling priority on the current thread.
 * Same as set_thread_priority but does not throw on failure.
 * \return true on success, false on failure
 */
bool set_thread_priority_safe(
    float priority = DEFAULT_THREAD_PRIORITY, bool realtime = true);
