// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0

#include "pw_sync/timed_mutex.h"

#include "concurrent_hal.h"
#include "pw_chrono/system_clock.h"

using pw::chrono::SystemClock;

namespace pw::sync {

bool TimedMutex::try_lock_for(SystemClock::duration timeout) {
  // Use non-blocking try_lock for negative and zero length durations.
  if (timeout <= SystemClock::duration::zero()) {
    return try_lock();
  }

  // Convert duration to milliseconds for Device OS API.
  // Device OS uses system_tick_t which is milliseconds.
  const int64_t timeout_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();

  // Handle timeouts that exceed the max value for system_tick_t.
  // CONCURRENT_WAIT_FOREVER is (system_tick_t)-1, so we need to stay below that.
  constexpr int64_t kMaxTimeoutMs = static_cast<int64_t>(CONCURRENT_WAIT_FOREVER) - 1;

  if (timeout_ms > kMaxTimeoutMs) {
    // For very long timeouts, we loop with max timeout chunks.
    int64_t remaining_ms = timeout_ms;
    while (remaining_ms > kMaxTimeoutMs) {
      if (os_mutex_lock_timeout(native_handle(),
                                static_cast<system_tick_t>(kMaxTimeoutMs)) == 0) {
        return true;
      }
      remaining_ms -= kMaxTimeoutMs;
    }
    return os_mutex_lock_timeout(native_handle(),
                                 static_cast<system_tick_t>(remaining_ms)) == 0;
  }

  return os_mutex_lock_timeout(native_handle(),
                               static_cast<system_tick_t>(timeout_ms)) == 0;
}

}  // namespace pw::sync
