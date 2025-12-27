// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0
//
// pw_sync timed_mutex backend inline implementations for Particle Device OS

#pragma once

#include "pw_chrono/system_clock.h"
#include "pw_sync/timed_mutex.h"

namespace pw::sync {

inline bool TimedMutex::try_lock_until(
    chrono::SystemClock::time_point deadline) {
  return try_lock_for(deadline - chrono::SystemClock::now());
}

}  // namespace pw::sync
