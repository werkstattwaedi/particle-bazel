// Copyright Offene Werkstatt Wädenswil
// SPDX-License-Identifier: MIT
//
// Minimal pw_assert_basic handler for Particle P2 firmware.
// Logs assertion failure and resets the device.

#include "pw_assert_basic/handler.h"

#include <cstdarg>
#include <cstdio>

#include "core_hal.h"
#include "delay_hal.h"
#include "pw_log/log.h"

extern "C" {

// Handler called by pw_assert_basic macros when an assertion fails.
// Logs the failure 5 times, then resets the device.
void pw_assert_basic_HandleFailure(
    const char* file_name,
    int line_number,
    const char* function_name,
    const char* format,
    ...
) {
  char buffer[150];
  if (format != nullptr) {
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
  }

  for (int i = 0; i < 5; i++) {
    PW_LOG_CRITICAL(
        "ASSERT FAILED: %s:%d in %s()", file_name, line_number, function_name
    );
    if (format != nullptr) {
      PW_LOG_CRITICAL("  %s", buffer);
    }
    HAL_Delay_Milliseconds(1000);
  }

  HAL_Core_Enter_Safe_Mode(nullptr);
}

}  // extern "C"
