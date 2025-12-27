// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0
//
// Minimal pw_assert_basic handler for Particle P2 firmware.
// Triggers a breakpoint on assertion failure for debugger attachment.

#include "pw_assert_basic/handler.h"

extern "C" {

// Handler called by pw_assert_basic macros when an assertion fails.
// Simply triggers a breakpoint for the debugger.
void pw_assert_basic_HandleFailure(const char* /* file_name */,
                                    int /* line_number */,
                                    const char* /* function_name */,
                                    const char* /* format */,
                                    ...) {
    // Trigger debugger breakpoint and loop forever
    while (1) {
        __asm volatile("bkpt #0");
    }
}

}  // extern "C"
