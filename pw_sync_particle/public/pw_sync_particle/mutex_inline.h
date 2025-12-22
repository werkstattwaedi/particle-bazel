// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0
//
// pw_sync mutex backend inline implementations for Particle Device OS

#pragma once

#include "concurrent_hal.h"
#include "pw_assert/assert.h"
#include "pw_sync/mutex.h"

namespace pw::sync {

inline Mutex::Mutex() : native_type_(nullptr) {
  int result = os_mutex_create(&native_type_);
  PW_DASSERT(result == 0);
}

inline Mutex::~Mutex() {
  if (native_type_ != nullptr) {
    os_mutex_destroy(native_type_);
  }
}

inline void Mutex::lock() {
  int result = os_mutex_lock(native_type_);
  PW_DASSERT(result == 0);
}

inline bool Mutex::try_lock() {
  return os_mutex_trylock(native_type_) == 0;
}

inline void Mutex::unlock() {
  int result = os_mutex_unlock(native_type_);
  PW_ASSERT(result == 0);
}

inline Mutex::native_handle_type Mutex::native_handle() { return native_type_; }

}  // namespace pw::sync
