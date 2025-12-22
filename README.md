# Particle Bazel

Bazel infrastructure for building Particle firmware with [Pigweed](https://pigweed.dev/).

This module provides:
- Device OS SDK integration for Particle P2 (RTL8721DM / Cortex-M33)
- Pigweed backend implementations for Particle hardware
- Two-pass linking for OTA-compatible firmware binaries
- Build rules for firmware compilation and flashing

## Directory Structure

```
particle-bazel/
├── BUILD.bazel          # Device OS targets (headers, dynalibs, user-part glue)
├── MODULE.bazel         # Bazel module definition
├── backends/            # Pigweed backend implementations
│   ├── assert/          # pw_assert backend (breakpoint on failure)
│   ├── chrono/          # pw_chrono backend (HAL timer)
│   ├── gpio/            # pw_digital_io wrapper (pb::ParticleDigitalIn/Out)
│   ├── log/             # Log bridge (Device OS -> pw_log)
│   ├── sync/            # pw_sync backend (HAL mutex)
│   ├── sys_io/          # pw_sys_io backend (USB serial)
│   ├── thread/          # pw_thread backends (id, yield, sleep)
│   └── watchdog/        # Watchdog wrapper (pb::watchdog::Watchdog)
├── rules/               # Bazel build rules
│   └── particle_firmware.bzl
├── third_party/
│   └── device-os/       # Particle Device OS submodule
├── platforms/           # Platform definitions
├── toolchain/           # ARM toolchain configuration
└── tools/               # Utility scripts (CRC patching, size extraction)
```

## Usage

### 1. Add as a Bazel module dependency

In your `MODULE.bazel`:

```starlark
bazel_dep(name = "particle_bazel")
local_path_override(
    module_name = "particle_bazel",
    path = "particle",  # Path to particle-bazel submodule
)

bazel_dep(name = "pigweed")
local_path_override(
    module_name = "pigweed",
    path = "third_party/pigweed",
)
```

### 2. Import the bazelrc configuration

In your `.bazelrc`:

```
import %workspace%/particle/.bazelrc
```

### 3. Create firmware targets

In your `BUILD.bazel`:

```starlark
load("@particle_bazel//rules:particle_firmware.bzl",
     "particle_cc_binary",
     "particle_firmware_binary",
     "particle_flash_binary")

cc_library(
    name = "my_firmware_lib",
    srcs = ["main.cc"],
    deps = [
        "@particle_bazel//:device_os_headers",
        "@particle_bazel//backends/gpio:digital_io",
        "@particle_bazel//backends/log:log_bridge",
        "@pigweed//pw_log",
    ],
    target_compatible_with = ["@pigweed//pw_build/constraints/arm:cortex-m33"],
)

particle_cc_binary(
    name = "my_firmware.elf",
    deps = [":my_firmware_lib"],
)

particle_firmware_binary(
    name = "my_firmware",
    elf = ":my_firmware.elf",
)

particle_flash_binary(
    name = "flash",
    firmware = ":my_firmware.bin",
)
```

### 4. Build and flash

```bash
# Build firmware
bazel build --config=p2 //path/to:my_firmware.elf

# Build and flash
bazel run --config=p2 //path/to:flash
```

## Available Targets

### Device OS Integration (`@particle_bazel//`)

| Target | Description |
|--------|-------------|
| `:device_os_headers` | Device OS header files |
| `:device_os_user_part` | Complete user-part dependencies |
| `:hal_dynalib` | HAL dynamic library exports |
| `:services_dynalib` | Services dynamic library exports |
| `:linker_scripts` | Linker scripts for firmware |
| `:wiring` | Arduino-compatible Wiring library |

### Pigweed Backends (`@particle_bazel//backends/`)

| Target | Pigweed Facade | Description |
|--------|----------------|-------------|
| `assert:assert_backend` | `pw_assert` | Triggers debugger breakpoint on assert |
| `chrono:system_clock` | `pw_chrono` | System clock using HAL timer |
| `gpio:digital_io` | `pw_digital_io` | GPIO wrapper classes |
| `log:log_bridge` | - | Bridges Device OS logs to pw_log |
| `sync:mutex` | `pw_sync` | Mutex using HAL concurrency |
| `sys_io:sys_io` | `pw_sys_io` | USB serial I/O |
| `thread:id` | `pw_thread` | Thread ID |
| `thread:yield` | `pw_thread` | Thread yield |
| `thread:sleep` | `pw_thread` | Thread sleep |
| `watchdog:watchdog` | - | Hardware watchdog wrapper |

### Particle-Bazel Utilities

These use the `pb::` namespace:

```cpp
#include "pb_digital_io/digital_io.h"
#include "pb_log/log_bridge.h"
#include "pb_watchdog/watchdog.h"

pb::ParticleDigitalOut led(D7);
pb::ParticleDigitalIn button(D0, pb::ParticleDigitalIn::Mode::kInputPulldown);
pb::log::InitLogBridge();
pb::watchdog::Watchdog wdt;
```

## Build Rules

### `particle_cc_binary`

Creates a Particle firmware ELF with two-pass linking for precise memory boundaries.

```starlark
particle_cc_binary(
    name = "firmware.elf",
    srcs = ["main.cc"],      # Optional: source files
    deps = [...],            # Dependencies
    copts = [...],           # Additional compiler flags
    defines = [...],         # Preprocessor defines
    linkopts = [...],        # Additional linker flags
    two_pass = True,         # Use two-pass linking (default: True)
)
```

### `particle_firmware_binary`

Converts ELF to flashable .bin with SHA256/CRC32 patched.

```starlark
particle_firmware_binary(
    name = "firmware",
    elf = ":firmware.elf",
)
```

### `particle_flash_binary`

Creates a target to flash firmware via USB.

```starlark
particle_flash_binary(
    name = "flash",
    firmware = ":firmware.bin",
)
```

## Configuration

The `.bazelrc` provides the `p2` config for Particle P2 builds:

```bash
bazel build --config=p2 //...
```

This configures:
- Target platform: Cortex-M33
- C++ standard: C++17
- Pigweed backends for embedded target

## License

- particle-bazel: Apache-2.0
- Device OS: LGPL v3
