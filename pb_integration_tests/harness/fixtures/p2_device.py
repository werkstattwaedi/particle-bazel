# Copyright Offene Werkstatt Wädenswil
# SPDX-License-Identifier: MIT

"""P2 device fixture for integration tests.

Handles flashing firmware and establishing RPC communication with
a P2 device connected via USB.
"""

import asyncio
import logging
import sys
from pathlib import Path
from types import ModuleType
from typing import Iterable, Optional

import serial
from pw_hdlc import rpc as hdlc_rpc
from pw_log.log_decoder import timestamp_parser_ms_since_boot
from pw_stream import stream_readers
from pw_system.device import Device as PwSystemDevice
from pw_tokenizer import detokenize

from tools.usb.flash import ParticleFlasher, FlashError
from tools.usb.serial_port import wait_for_serial_port

_LOG = logging.getLogger(__name__)


class P2DeviceFixture:
    """Fixture for testing on real P2 hardware.

    This fixture:
    1. Flashes test firmware to the P2 device
    2. Waits for the device to reboot
    3. Opens USB serial connection
    4. Initializes pw_rpc client over HDLC (using pw_system.device.Device)

    The device is available after start() completes.

    Example:
        fixture = P2DeviceFixture(
            firmware_bin=Path("test_firmware.bin"),
            firmware_elf=Path("test_firmware"),  # For log detokenization
            proto_modules=[test_pb2],
        )
        await fixture.start()
        try:
            result = fixture.device.rpcs.test.TestControl.DoSomething()
            print(result)
        finally:
            await fixture.stop()
    """

    def __init__(
        self,
        firmware_bin: Path,
        proto_paths: Iterable[Path | ModuleType],
        firmware_elf: Optional[Path] = None,
        channel_id: int = hdlc_rpc.DEFAULT_CHANNEL_ID,
        flash_timeout: float = 120.0,
        device_timeout: float = 30.0,
        serial_number: Optional[str] = None,
        baudrate: int = 115200,
        rpc_timeout: float = 5.0,
    ) -> None:
        """Initialize the P2 device fixture.

        Args:
            firmware_bin: Path to firmware binary (.bin) to flash.
            proto_paths: Proto modules (compiled _pb2 modules).
            firmware_elf: Path to firmware ELF for log detokenization.
                         If not provided, logs will show raw tokens.
            channel_id: pw_rpc channel ID.
            flash_timeout: Timeout for flashing in seconds.
            device_timeout: Timeout for device to appear after flash in seconds.
            serial_number: Optional device serial number for selection.
            baudrate: Serial baud rate.
            rpc_timeout: Default RPC call timeout in seconds.
        """
        self._firmware_bin = Path(firmware_bin)
        self._firmware_elf = Path(firmware_elf) if firmware_elf else None
        self._proto_paths = list(proto_paths)
        self._channel_id = channel_id
        self._flash_timeout = flash_timeout
        self._device_timeout = device_timeout
        self._serial_number = serial_number
        self._baudrate = baudrate
        self._rpc_timeout = rpc_timeout

        self._device: Optional[PwSystemDevice] = None
        self._serial: Optional[serial.Serial] = None
        self._reader: Optional[stream_readers.SelectableReader] = None
        self._port: Optional[str] = None
        self._detokenizer: Optional[detokenize.Detokenizer] = None

    async def start(self) -> None:
        """Flash firmware and start RPC communication.

        Raises:
            FlashError: If flashing fails.
            RuntimeError: If device does not appear or RPC fails.
        """
        if self._device is not None:
            raise RuntimeError("Device fixture already started")

        # Flash firmware (synchronous, run in thread pool)
        _LOG.info("Flashing firmware: %s", self._firmware_bin)
        await asyncio.to_thread(self._flash_firmware)

        # Wait for device to reboot and appear
        _LOG.info("Waiting for device to appear...")
        self._port = await asyncio.to_thread(
            wait_for_serial_port,
            timeout=self._device_timeout,
            serial_number=self._serial_number,
        )
        if not self._port:
            raise RuntimeError(
                f"Device did not appear within {self._device_timeout}s"
            )
        _LOG.info("Device found at %s", self._port)

        # Set up detokenizer if ELF provided
        if self._firmware_elf and self._firmware_elf.exists():
            _LOG.info("Loading token database from %s", self._firmware_elf)
            # Use #.* domain pattern to match all token domains
            self._detokenizer = detokenize.AutoUpdatingDetokenizer(
                str(self._firmware_elf) + "#.*"
            )
            self._detokenizer.show_errors = True
        else:
            _LOG.warning("No firmware ELF provided - logs will show raw tokens")
            self._detokenizer = None

        # Initialize RPC using pw_system.device.Device (same as console)
        _LOG.info("Initializing RPC client...")
        self._serial = serial.Serial(
            self._port,
            baudrate=self._baudrate,
            timeout=0.1,
        )
        self._reader = stream_readers.SelectableReader(self._serial, 8192)

        # Create Device with HDLC encoding
        # Note: use_rpc_logging=False because RPC logging requires the pw.log
        # proto to be in proto_library. Log messages from the device will still
        # appear in HDLC frames but won't be processed by the log RPC handler.
        # TODO(b/xxx): Add pw.log proto to enable full log capture.
        self._device = PwSystemDevice(
            channel_id=self._channel_id,
            reader=self._reader,
            write=self._serial.write,
            proto_library=self._proto_paths,
            detokenizer=self._detokenizer,
            timestamp_decoder=timestamp_parser_ms_since_boot,
            rpc_timeout_s=self._rpc_timeout,
            use_rpc_logging=False,
            use_hdlc_encoding=True,
        )

        # Wait for pw_system to fully initialize after we open the serial port.
        # The firmware waits for USB "connected" (which happens when we open
        # the port), then needs time to register services and start pw_system.
        _LOG.info("Waiting for pw_system to initialize...")
        await asyncio.sleep(3.0)

        _LOG.info("P2 device fixture started")

    def _flash_firmware(self) -> None:
        """Flash firmware to the device (runs in thread pool)."""
        flasher = ParticleFlasher()
        flasher.flash_local(
            str(self._firmware_bin),
            wait_for_device=True,
            device_timeout=self._device_timeout,
            flash_timeout=self._flash_timeout,
        )

    async def stop(self) -> None:
        """Stop RPC communication and clean up."""
        if self._device:
            self._device.close()  # Stop background threads
            self._device = None

        if self._reader:
            self._reader.cancel_read()  # Cancel any pending reads
            self._reader = None

        if self._serial:
            self._serial.close()
            self._serial = None

        self._port = None
        _LOG.info("P2 device fixture stopped")

    @property
    def device(self) -> PwSystemDevice:
        """Get the pw_system Device for communicating with the device.

        Raises:
            RuntimeError: If fixture is not started.
        """
        if self._device is None:
            raise RuntimeError("Device fixture not started")
        return self._device

    # Alias for backwards compatibility
    @property
    def rpc(self) -> PwSystemDevice:
        """Alias for device property."""
        return self.device

    @property
    def port(self) -> str:
        """Get the serial port path.

        Raises:
            RuntimeError: If fixture is not started.
        """
        if self._port is None:
            raise RuntimeError("Device fixture not started")
        return self._port

    async def __aenter__(self) -> "P2DeviceFixture":
        await self.start()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        await self.stop()
