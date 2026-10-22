#!/usr/bin/env python3
"""
BLE DFU flash script for Adafruit nRF52 Bootloader.
Uses Nordic Legacy DFU protocol over BLE via the 'bleak' library.

Usage:
    pip install bleak
    python ble_dfu_flash.py <dfu_package.zip> [device_name_or_address]

Examples:
    python ble_dfu_flash.py firmware_left_ble.zip "ocdmk L"
    python ble_dfu_flash.py firmware_right_ble.zip "ocdmk R"
    python ble_dfu_flash.py firmware.zip 997C31E1-B599-4571-C008-1E59340F054E

Create DFU package with:
    adafruit-nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0x0100 \
        --application build/left_ble/zephyr/zmk.bin firmware_left_ble.zip
"""

import asyncio
import struct
import sys
import zipfile
import json
import os
from bleak import BleakClient, BleakScanner

# Nordic DFU Service UUIDs
DFU_SERVICE_UUID = "00001530-1212-efde-1523-785feabcd123"
DFU_CONTROL_UUID = "00001531-1212-efde-1523-785feabcd123"
DFU_PACKET_UUID  = "00001532-1212-efde-1523-785feabcd123"
DFU_VERSION_UUID = "00001534-1212-efde-1523-785feabcd123"

# DFU opcodes
OP_START_DFU = 1
OP_INIT_DFU = 2
OP_RECV_FIRMWARE = 3
OP_VALIDATE = 4
OP_ACTIVATE = 5
OP_RESET = 6
OP_PKT_RCPT_NOTIF_REQ = 8
OP_RESPONSE = 16
OP_PKT_RCPT_NOTIF = 17

# DFU image types
TYPE_SOFTDEVICE = 1
TYPE_BOOTLOADER = 2
TYPE_APPLICATION = 4

MTU_SIZE = 20
PKT_RCPT_INTERVAL = 10  # request receipt notification every N packets


class BleDfu:
    def __init__(self, address, zip_path):
        self.address = address
        self.zip_path = zip_path
        self.client = None
        self.response_event = asyncio.Event()
        self.response_data = None
        self.pkt_rcpt_event = asyncio.Event()

    def _parse_zip(self):
        with zipfile.ZipFile(self.zip_path) as zf:
            manifest = json.loads(zf.read("manifest.json"))
            app_info = manifest["manifest"]["application"]
            bin_file = app_info["bin_file"]
            dat_file = app_info["dat_file"]
            self.firmware = zf.read(bin_file)
            self.init_packet = zf.read(dat_file)
        print(f"Firmware: {len(self.firmware)} bytes, init packet: {len(self.init_packet)} bytes")

    def _notification_handler(self, sender, data):
        if data[0] == OP_RESPONSE:
            self.response_data = data
            self.response_event.set()
        elif data[0] == OP_PKT_RCPT_NOTIF:
            self.pkt_rcpt_event.set()

    async def _wait_response(self, timeout=10.0):
        self.response_event.clear()
        await asyncio.wait_for(self.response_event.wait(), timeout)
        return self.response_data

    async def _send_control(self, data):
        await self.client.write_gatt_char(DFU_CONTROL_UUID, data, response=True)

    async def _send_packet(self, data):
        await self.client.write_gatt_char(DFU_PACKET_UUID, data, response=False)

    async def flash(self):
        self._parse_zip()

        print(f"Connecting to {self.address}...")
        self.client = BleakClient(self.address)
        await self.client.connect()
        print(f"Connected: MTU={self.client.mtu_size}")

        # Subscribe to notifications on control point
        await self.client.start_notify(DFU_CONTROL_UUID, self._notification_handler)
        await asyncio.sleep(0.5)  # let subscription settle

        # Start DFU - application image
        print("Starting DFU...")
        await self._send_control(struct.pack("<BB", OP_START_DFU, TYPE_APPLICATION))
        # Send firmware size (softdevice=0, bootloader=0, application=size)
        await self._send_packet(struct.pack("<III", 0, 0, len(self.firmware)))
        resp = await self._wait_response(timeout=15.0)
        assert resp[0] == OP_RESPONSE and resp[1] == OP_START_DFU and resp[2] == 1, \
            f"START_DFU failed: {resp.hex()}"
        print("DFU started")

        # Init DFU - send init packet
        await self._send_control(struct.pack("<BB", OP_INIT_DFU, 0))  # 0 = receive init
        # Send init packet in chunks of MTU_SIZE
        for i in range(0, len(self.init_packet), MTU_SIZE):
            await self._send_packet(self.init_packet[i:i + MTU_SIZE])
        await self._send_control(struct.pack("<BB", OP_INIT_DFU, 1))  # 1 = init complete
        resp = await self._wait_response()
        assert resp[0] == OP_RESPONSE and resp[1] == OP_INIT_DFU and resp[2] == 1, \
            f"INIT_DFU failed: {resp.hex()}"
        print("Init packet sent")

        # Set packet receipt notification interval
        await self._send_control(struct.pack("<BH", OP_PKT_RCPT_NOTIF_REQ, PKT_RCPT_INTERVAL))

        # Receive firmware
        await self._send_control(struct.pack("<B", OP_RECV_FIRMWARE))

        chunk_size = MTU_SIZE
        total = len(self.firmware)
        sent = 0
        pkt_count = 0

        while sent < total:
            chunk = self.firmware[sent:sent + chunk_size]
            await self._send_packet(chunk)
            sent += len(chunk)
            pkt_count += 1

            if pkt_count % PKT_RCPT_INTERVAL == 0:
                # Wait for receipt notification
                self.pkt_rcpt_event.clear()
                try:
                    await asyncio.wait_for(self.pkt_rcpt_event.wait(), 10.0)
                except asyncio.TimeoutError:
                    pass  # continue anyway

            pct = sent * 100 // total
            print(f"\rUploading: {pct}% ({sent}/{total})", end="", flush=True)

        print()
        resp = await self._wait_response(timeout=30.0)
        assert resp[0] == OP_RESPONSE and resp[1] == OP_RECV_FIRMWARE and resp[2] == 1, \
            f"RECV_FIRMWARE failed: {resp.hex()}"
        print("Firmware uploaded")

        # Validate
        await self._send_control(struct.pack("<B", OP_VALIDATE))
        resp = await self._wait_response()
        assert resp[0] == OP_RESPONSE and resp[1] == OP_VALIDATE and resp[2] == 1, \
            f"VALIDATE failed: {resp.hex()}"
        print("Firmware validated")

        # Activate and reset
        await self._send_control(struct.pack("<B", OP_ACTIVATE))
        print("Firmware activated — device is rebooting")

        try:
            await self.client.disconnect()
        except Exception:
            pass  # device may have already reset


async def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <dfu_package.zip> [device_name_or_address]")
        print(f"  device_name_or_address: BLE name (e.g. 'ocdmk L') or macOS UUID")
        sys.exit(1)

    zip_path = sys.argv[1]
    target = sys.argv[2] if len(sys.argv) > 2 else None

    if not os.path.exists(zip_path):
        print(f"File not found: {zip_path}")
        sys.exit(1)

    print(f"Scanning for '{target or 'any DFU device'}'...")
    devices = await BleakScanner.discover(timeout=5.0, return_adv=True)

    address = None
    for addr, (dev, adv) in devices.items():
        name = dev.name or ""
        local = adv.local_name or ""
        if target:
            # Match by name, local_name, or address
            if name == target or local == target or dev.address == target:
                address = dev.address
                print(f"Found: {name} (air: {local}) ({dev.address}) RSSI={adv.rssi}")
                break
        else:
            # Auto-detect: look for DFU service UUID or known names
            if DFU_SERVICE_UUID in adv.service_uuids or "dfu" in name.lower() or "ocdmk" in local.lower():
                address = dev.address
                print(f"Found: {name} (air: {local}) ({dev.address}) RSSI={adv.rssi}")
                break

    if not address:
        print(f"Device not found. Available:")
        for addr, (dev, adv) in sorted(devices.items(), key=lambda x: x[1][1].rssi, reverse=True)[:10]:
            print(f"  {dev.address}  {dev.name or '(unnamed)':20s}  air: {adv.local_name or '':15s}  RSSI={adv.rssi}  svcs={adv.service_uuids}")
        sys.exit(1)

    # Warn when matching by address — macOS caches BLE names, so the user
    # might be flashing the wrong half (e.g. right firmware onto the left).
    # Flashing the wrong side can remove the ability to enter bootloader
    # again since the right half has no reset button and depends on the
    # left being running to trigger bootloader via keymap.
    matched_by_address = target and target == address and name != target and local != target
    if matched_by_address:
        print(f"\nWARNING: Matched by address only (cached name: '{name}', air name: '{local}').")
        print(f"Make sure you are flashing the correct half!")
        print(f"Flashing the wrong firmware can remove bootloader access on the right side.")
        confirm = input("Continue? [y/N] ")
        if confirm.lower() != "y":
            print("Aborted.")
            sys.exit(0)

    dfu = BleDfu(address, zip_path)
    await dfu.flash()

if __name__ == "__main__":
    asyncio.run(main())
