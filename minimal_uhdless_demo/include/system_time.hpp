//
// Copyright 2017 Ettus Research (National Instruments Corp.)
// Copyright 2026 Per Vices Corporation
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include "time_spec.hpp"

/*!
 * Get the system time in time_spec_t format.
 * Uses the highest precision clock available.
 * \return the system time as a time_spec_t
 */
time_spec_t get_system_time(void);
