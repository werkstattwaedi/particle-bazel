// Copyright Offene Werkstatt Wädenswil
// SPDX-License-Identifier: MIT
//
// pw_sync mutex backend for Particle Device OS

#pragma once

#include <cstdint>

namespace pw::sync::backend {

// os_mutex_t in Device OS is void* (handle to FreeRTOS mutex)
using NativeMutex = void*;
using NativeMutexHandle = NativeMutex&;

}  // namespace pw::sync::backend
