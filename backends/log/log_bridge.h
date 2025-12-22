// Copyright 2024 Werkstatt Waedi
// SPDX-License-Identifier: Apache-2.0
//
// Minimal HAL-level bridge from Particle Device OS logging to pw_log.

#pragma once

namespace particle_log {

// Initialize the log bridge. Call this early in setup() to intercept
// all Device OS system logs and route them through pw_log.
void InitLogBridge();

}  // namespace particle_log
