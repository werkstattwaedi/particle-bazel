#!/bin/bash
# Copyright Offene Werkstatt Wädenswil
# SPDX-License-Identifier: MIT
#
# Flash Particle test firmware and open serial monitor for test output.
# Usage: flash_test.sh <firmware_path>

set -e

# Resolve runfiles directory
if [[ -n "$RUNFILES_DIR" ]]; then
    RUNFILES="$RUNFILES_DIR"
elif [[ -d "$0.runfiles" ]]; then
    RUNFILES="$0.runfiles"
else
    # Fallback: script is in runfiles, go up to find root
    RUNFILES="$(cd "$(dirname "$0")" && pwd)"
    while [[ ! -d "$RUNFILES/_main" && "$RUNFILES" != "/" ]]; do
        RUNFILES="$(dirname "$RUNFILES")"
    done
fi

# Find the firmware file (passed as runfile path)
FIRMWARE_RELATIVE="$1"
FIRMWARE="$RUNFILES/_main/$FIRMWARE_RELATIVE"

if [ -z "$FIRMWARE_RELATIVE" ]; then
    echo "Error: No firmware file specified"
    echo "Usage: $0 <firmware_path>"
    exit 1
fi

if [ ! -f "$FIRMWARE" ]; then
    echo "Error: Firmware file not found: $FIRMWARE"
    echo "Runfiles dir: $RUNFILES"
    exit 1
fi

echo "=== Flashing test firmware: $FIRMWARE ==="
particle flash --local "$FIRMWARE"

echo ""
echo "=== Waiting for device to reconnect ==="

TIMEOUT=15
ELAPSED=0
while [ $ELAPSED -lt $TIMEOUT ]; do
    if particle serial list 2>/dev/null | grep -q "ttyACM\|ttyUSB"; then
        echo "Device found!"
        break
    fi
    sleep 1
    ELAPSED=$((ELAPSED + 1))
    echo "Waiting... ($ELAPSED/$TIMEOUT)"
done

if [ $ELAPSED -ge $TIMEOUT ]; then
    echo "Error: Device did not reconnect within $TIMEOUT seconds"
    exit 1
fi

echo ""
echo "=== Opening serial monitor (Ctrl+C to exit) ==="
particle serial monitor --follow
