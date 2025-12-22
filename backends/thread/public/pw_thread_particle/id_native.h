// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0
//
// pw_thread ID backend for Particle Device OS

#pragma once

#include <cstdint>

namespace pw::thread::backend {

// os_thread_t in Device OS is void* (FreeRTOS TaskHandle_t)
class NativeId {
 public:
  constexpr NativeId(void* task_handle = nullptr)
      : task_handle_(task_handle) {}

  constexpr bool operator==(NativeId other) const {
    return task_handle_ == other.task_handle_;
  }
  constexpr bool operator!=(NativeId other) const {
    return task_handle_ != other.task_handle_;
  }
  constexpr bool operator<(NativeId other) const {
    return task_handle_ < other.task_handle_;
  }
  constexpr bool operator<=(NativeId other) const {
    return task_handle_ <= other.task_handle_;
  }
  constexpr bool operator>(NativeId other) const {
    return task_handle_ > other.task_handle_;
  }
  constexpr bool operator>=(NativeId other) const {
    return task_handle_ >= other.task_handle_;
  }

  [[nodiscard]] void* native() const { return task_handle_; }

 private:
  void* task_handle_;
};

}  // namespace pw::thread::backend
