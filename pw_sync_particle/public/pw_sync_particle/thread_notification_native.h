// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0
//
// pw_sync thread_notification backend native type for Particle Device OS

#pragma once

#include "concurrent_hal.h"
#include "pw_sync/interrupt_spin_lock.h"

namespace pw::sync::backend {

struct NativeThreadNotification {
  os_thread_t blocked_thread;
  bool notified;
  // We use a global ISL for all thread notifications because these backends
  // only support uniprocessor targets and ergo we reduce the memory cost for
  // all ISL instances without any risk of spin contention between different
  // instances.
  PW_CONSTINIT inline static InterruptSpinLock shared_spin_lock = {};
};
using NativeThreadNotificationHandle = NativeThreadNotification&;

}  // namespace pw::sync::backend
