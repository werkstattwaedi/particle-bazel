# Claude Code Instructions for particle-bazel

## Copyright Header

All new source files (.cc, .h) MUST use this copyright header:

```cpp
// Copyright Offene Werkstatt Wädenswil
// SPDX-License-Identifier: MIT
```

For Python and Starlark files (.py, .bzl), use:

```python
# Copyright Offene Werkstatt Wädenswil
# SPDX-License-Identifier: MIT
```

For BUILD.bazel files, use:

```python
# Copyright Offene Werkstatt Wädenswil
# SPDX-License-Identifier: MIT
```

## Project Structure

This repository provides Pigweed backends for Particle Device OS:

- `pw_thread_particle/` - Thread backend using Device OS threading
- `pw_sync_particle/` - Synchronization primitives using Device OS semaphores/mutexes
- `pw_chrono_particle/` - Clock backend using Device OS time functions
- `pw_sys_io_particle/` - I/O backend using USB CDC serial
- `pw_assert_particle/` - Assert handler for Device OS
- `pw_digital_io_particle/` - Digital I/O using Device OS HAL
- `pw_stream_particle/` - UART stream using Device OS HAL (non-blocking)
- `pw_system_particle/` - pw_system integration (scheduler stub)
- `rules/` - Bazel rules for Particle firmware builds

## Python Tooling

The `tools/` directory contains Python libraries for Particle device operations:

### Modules

| Module | Target | Description |
|--------|--------|-------------|
| `tools.cli` | `@particle_bazel//tools:particle_cli_wrapper` | Hermetic particle-cli wrapper with retries |
| `tools.cloud` | `@particle_bazel//tools:particle_cloud` | Cloud REST API (devices, functions, variables, ledger) |
| `tools.usb` | `@particle_bazel//tools:particle_usb` | USB device operations (flash, reset, serial port detection) |
| Combined | `@particle_bazel//tools:particle_tools` | All modules combined |

### Entry Point Scripts

```bash
# Flash firmware to device
bazel run @particle_bazel//tools:flash_firmware -- /path/to/firmware.bin

# Wait for device to appear on USB
bazel run @particle_bazel//tools:wait_for_device

# Serial monitor
bazel run @particle_bazel//tools:serial_monitor
```

### Python Usage

```python
from tools.cloud import ParticleCloudClient, LedgerClient
from tools.cli import ParticleCli
from tools.usb import ParticleDevice, ParticleFlasher

# Cloud operations
with ParticleCloudClient() as client:
    devices = client.list_devices()
    result = client.call_function("my-device", "doSomething", "arg")
    value = client.get_variable("my-device", "temperature")

# Ledger operations
with LedgerClient() as ledger:
    data = ledger.get("my-device", "terminal-config")
    ledger.set("my-device", "terminal-config", {"enabled": True})

# USB flashing
flasher = ParticleFlasher()
device = flasher.flash_local("firmware.bin")
```

### Dependencies

- **Python packages**: `requests`, `pyserial` (managed via `requirements_lock.txt`)
- **particle-cli**: Hermetic npm package (via `pnpm-lock.yaml`)

## Important Notes

- Device OS functions available to user applications are limited to those exported via dynalib
- Check `hal_dynalib_*.h` headers to verify function availability
- The FreeRTOS scheduler is already running when user code starts - don't call `vTaskStartScheduler`
