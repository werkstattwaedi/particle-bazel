// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0

#include "pw_sync/thread_notification.h"

#include <mutex>

#include "concurrent_hal.h"
#include "pw_assert/check.h"

namespace pw::sync {

void ThreadNotification::acquire() {
  {
    std::lock_guard lock(backend::NativeThreadNotification::shared_spin_lock);

    // Enforce that only a single thread can block at a time.
    PW_DCHECK(native_type_.blocked_thread == nullptr);

    if (native_type_.notified) {
      native_type_.notified = false;
      return;
    }
    // Not notified yet, set the task handle for a one-time notification.
    native_type_.blocked_thread = os_thread_current(nullptr);
  }

  // Wait indefinitely for notification.
  // os_thread_wait may spuriously return, so we loop.
  while (os_thread_wait(CONCURRENT_WAIT_FOREVER, nullptr) == 0) {
    std::lock_guard lock(backend::NativeThreadNotification::shared_spin_lock);
    if (native_type_.notified) {
      break;
    }
  }

  std::lock_guard lock(backend::NativeThreadNotification::shared_spin_lock);
  // The task handle was cleared by the notifier.
  native_type_.notified = false;
}

void ThreadNotification::release() {
  std::lock_guard lock(backend::NativeThreadNotification::shared_spin_lock);
  if (native_type_.blocked_thread != nullptr) {
    os_thread_notify(native_type_.blocked_thread, nullptr);
    native_type_.blocked_thread = nullptr;
  }
  native_type_.notified = true;
}

}  // namespace pw::sync
