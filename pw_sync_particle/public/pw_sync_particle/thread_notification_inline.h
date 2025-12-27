// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0
//
// pw_sync thread_notification backend inline implementations for Particle Device OS

#pragma once

#include "concurrent_hal.h"
#include "pw_assert/assert.h"
#include "pw_sync/thread_notification.h"

namespace pw::sync {

inline ThreadNotification::ThreadNotification()
    : native_type_{
          .blocked_thread = nullptr,
          .notified = false,
      } {}

inline ThreadNotification::~ThreadNotification() = default;

inline bool ThreadNotification::try_acquire() {
  native_type_.shared_spin_lock.lock();
  const bool notified = native_type_.notified;
  native_type_.notified = false;
  native_type_.shared_spin_lock.unlock();
  return notified;
}

inline ThreadNotification::native_handle_type
ThreadNotification::native_handle() {
  return native_type_;
}

}  // namespace pw::sync
