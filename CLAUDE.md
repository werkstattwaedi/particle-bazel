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

## Important Notes

- Device OS functions available to user applications are limited to those exported via dynalib
- Check `hal_dynalib_*.h` headers to verify function availability
- The FreeRTOS scheduler is already running when user code starts - don't call `vTaskStartScheduler`
