#!/usr/bin/env python3
"""
PCB RS422 Simulator
Mimics the plasma brake PCB responses for testing without hardware.
Run this BEFORE starting app.py.
"""

import os
import pty
import serial
import struct
import crcmod
import threading
import time
import sys

# ── CRC (must match app.py) ──────────────────
crc16 = crcmod.mkCrcFun(0x11021, initCrc=0xFFFF, rev=True, xorOut=0x0000)
SYNC = bytes([0x1F, 0x01])

# ── Mode names for logging ───────────────────
MODE_NAMES = {
    0: "SAFE",
    1: "STANDBY",
    2: "EARLY_DEPLOYMENT",
    3: "LATE_DEPLOYMENT",
    4: "INACTIVE_DEORBIT",
    5: "ACTIVE_DEORBIT",
}

CMD_NAMES = {
    0x01: "SET_MODE",
    0x06: "GET_INFO",
    0x08: "GET_MODE",
}

current_mode = 0  # Start in SAFE


def parse_packet(data: bytes) -> dict | None:
    """Parse an incoming command packet. Returns dict or None if invalid."""
    if len(data) < 9:
        return None
    if data[0:2] != SYNC:
        return None

    cmd = data[2]
    payload_length = struct.unpack_from('<H', data, 3)[0]
    data_crc_recv  = struct.unpack_from('<H', data, 5)[0]
    header_crc_recv = struct.unpack_from('<H', data, 7)[0]

    payload = data[9:9 + payload_length]

    # Validate data CRC
    data_crc_calc = crc16(payload) if payload else 0x0000
    if data_crc_calc != data_crc_recv:
        print(f"  [SIM] ✗ Data CRC mismatch: got {data_crc_recv:#06x}, expected {data_crc_calc:#06x}")
        return None

    # Validate header CRC
    header_input = SYNC + bytes([cmd]) + struct.pack('<H', payload_length) + struct.pack('<H', data_crc_recv)
    header_crc_calc = crc16(header_input)
    if header_crc_calc != header_crc_recv:
        print(f"  [SIM] ✗ Header CRC mismatch: got {header_crc_recv:#06x}, expected {header_crc_calc:#06x}")
        return None

    return {"cmd": cmd, "payload": payload}


def build_response(cmd: int, payload: bytes = b'') -> bytes:
    """Build a response packet (same format as command)."""
    data_crc = crc16(payload) if payload else 0x0000
    header_input = SYNC + bytes([cmd]) + struct.pack('<H', len(payload)) + struct.pack('<H', data_crc)
    header_crc = crc16(header_input)
    return (
        SYNC
        + bytes([cmd])
        + struct.pack('<H', len(payload))
        + struct.pack('<H', data_crc)
        + struct.pack('<H', header_crc)
        + payload
    )


def handle_command(parsed: dict) -> bytes:
    """Decide response based on command."""
    global current_mode
    cmd     = parsed["cmd"]
    payload = parsed["payload"]
    cmd_name = CMD_NAMES.get(cmd, f"UNKNOWN(0x{cmd:02x})")

    if cmd == 0x01:  # SET_MODE
        if payload:
            new_mode = payload[0]
            mode_name = MODE_NAMES.get(new_mode, f"UNKNOWN({new_mode})")
            print(f"  [SIM] ▶ SET_MODE: {MODE_NAMES.get(current_mode)} → {mode_name}")
            current_mode = new_mode
            # ACK: echo back the mode that was set
            return build_response(cmd, bytes([current_mode]))

    elif cmd == 0x06:  # GET_INFO
        print(f"  [SIM] ▶ GET_INFO request")
        # Return a fake info payload
        info = b"PCB_SIM_v1.0"
        return build_response(cmd, info)

    elif cmd == 0x08:  # GET_MODE
        print(f"  [SIM] ▶ GET_MODE → {MODE_NAMES.get(current_mode)}")
        return build_response(cmd, bytes([current_mode]))

    else:
        print(f"  [SIM] ? Unknown command: {cmd_name}")
        return build_response(cmd, b'\xFF')  # NACK


def run_simulator(port_path: str):
    """Main simulator loop — listen on port_path and respond."""
    print(f"[SIM] Opening simulator on {port_path}")
    with serial.Serial(port_path, 115200, timeout=0.5) as ser:
        print(f"[SIM] Ready. Waiting for commands...\n")
        buf = b''
        while True:
            chunk = ser.read(64)
            if not chunk:
                continue
            buf += chunk
            print(f"  [SIM] Raw bytes received: {buf.hex()}")

            parsed = parse_packet(buf)
            if parsed:
                response = handle_command(parsed)
                ser.write(response)
                print(f"  [SIM] Response sent: {response.hex()}\n")
                buf = b''
            elif len(buf) > 64:
                # Discard garbage if buffer grows too large
                print(f"  [SIM] Buffer overflow — clearing")
                buf = b''


def create_virtual_ports():
    """
    Use socat to create a linked virtual serial port pair:
      /tmp/ttyVIRT0  ←→  /tmp/ttyVIRT1
    app.py connects to ttyVIRT0, simulator listens on ttyVIRT1.
    """
    import subprocess
    print("[SIM] Creating virtual serial port pair via socat...")
    proc = subprocess.Popen([
        'socat',
        'PTY,link=/tmp/ttyVIRT0,raw,echo=0',
        'PTY,link=/tmp/ttyVIRT1,raw,echo=0'
    ])
    time.sleep(1)  # Give socat a moment to create the symlinks
    print("[SIM] Virtual ports ready:")
    print("       controller_app.py  → set SERIAL_PORT = '/tmp/ttyVIRT0'")
    print("       simulator → listening on /tmp/ttyVIRT1\n")
    return proc


if __name__ == '__main__':
    socat_proc = create_virtual_ports()
    try:
        run_simulator('/tmp/ttyVIRT1')
    except KeyboardInterrupt:
        print("\n[SIM] Shutting down simulator.")
    finally:
        socat_proc.terminate()