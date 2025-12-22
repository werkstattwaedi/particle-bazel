// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0
//
// pw_sync mutex backend for Particle Device OS

#pragma once

#include <cstdint>

namespace pw::sync::backend {

// os_mutex_t in Device OS is void* (handle to FreeRTOS mutex)
using NativeMutex = void*;
using NativeMutexHandle = NativeMutex&;

}  // namespace pw::sync::backend
