// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0
//
// pw_thread ID inline implementation for Particle Device OS

#pragma once

#include "concurrent_hal.h"
#include "pw_thread/id.h"

namespace pw::this_thread {

inline thread::Id get_id() noexcept {
  return thread::Id(os_thread_current(nullptr));
}

}  // namespace pw::this_thread
