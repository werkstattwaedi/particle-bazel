// Copyright 2024. All rights reserved.
// SPDX-License-Identifier: MIT

#include "pw_sync/interrupt_spin_lock.h"

#include "hal_irq_flag.h"

namespace pw::sync {

void InterruptSpinLock::lock() {
  // Save the current interrupt state and disable interrupts.
  native_type_.saved_state = HAL_disable_irq();
  native_type_.locked = true;
}

bool InterruptSpinLock::try_lock() {
  // For a spin lock with interrupts disabled, try_lock behaves like lock.
  lock();
  return true;
}

void InterruptSpinLock::unlock() {
  native_type_.locked = false;
  // Restore the previous interrupt state.
  HAL_enable_irq(native_type_.saved_state);
}

}  // namespace pw::sync
