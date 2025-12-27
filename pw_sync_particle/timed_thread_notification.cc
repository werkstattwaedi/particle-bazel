// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0

#include "pw_sync/timed_thread_notification.h"

#include <algorithm>
#include <mutex>

#include "concurrent_hal.h"
#include "pw_assert/check.h"
#include "pw_chrono/system_clock.h"

using pw::chrono::SystemClock;

namespace pw::sync {

bool TimedThreadNotification::try_acquire_until(
    const SystemClock::time_point deadline) {
  {
    std::lock_guard lock(backend::NativeThreadNotification::shared_spin_lock);

    // Enforce that only a single thread can block at a time.
    PW_DCHECK(native_handle().blocked_thread == nullptr);

    const bool notified = native_handle().notified;
    // Don't block if we've already reached the specified deadline time.
    if (notified || (SystemClock::now() >= deadline)) {
      native_handle().notified = false;
      return notified;
    }
    // Not notified yet, set the task handle for a one-time notification.
    native_handle().blocked_thread = os_thread_current(nullptr);
  }

  // os_thread_wait may spuriously return. Ergo, loop until we have been
  // notified or the specified deadline time has been reached (whichever
  // comes first).
  constexpr int64_t kMaxTimeoutMs =
      static_cast<int64_t>(CONCURRENT_WAIT_FOREVER) - 1;

  for (SystemClock::time_point now = SystemClock::now(); now < deadline;
       now = SystemClock::now()) {
    // Calculate timeout in milliseconds.
    const int64_t timeout_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
            .count();

    // Clamp to max timeout and ensure it's at least 1ms.
    const system_tick_t wait_ms =
        static_cast<system_tick_t>(std::min(timeout_ms, kMaxTimeoutMs));

    if (wait_ms == 0) {
      break;  // Deadline reached.
    }

    // Wait for notification with timeout.
    os_thread_wait(wait_ms, nullptr);

    // Check if we were notified.
    std::lock_guard lock(backend::NativeThreadNotification::shared_spin_lock);
    if (native_handle().notified) {
      break;  // We were notified!
    }
  }

  std::lock_guard lock(backend::NativeThreadNotification::shared_spin_lock);
  // Read the notification status. Include notifications which arrived after
  // we timed out but before we entered this critical section.
  const bool notified = native_handle().notified;
  native_handle().notified = false;
  native_handle().blocked_thread = nullptr;
  return notified;
}

}  // namespace pw::sync
