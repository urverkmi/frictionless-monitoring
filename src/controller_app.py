#!/usr/bin/env python3
"""
TunaCan Plasma Brake Controller
Aurora Propulsion Technologies IDD rev 5.0
"""

import serial
import struct
import crcmod
import threading
import time
from flask import Flask, render_template_string, jsonify
from flask_cors import CORS


SERIAL_PORT = "/dev/ttyUSB0"   # Check with: ls /dev/ttyUSB*
BAUD_RATE   = 115200
TIMEOUT_S   = 1.0

# ─────────────────────────────────────────
# CRC setup (matches IDD spec)
# poly=0x1021, init=0xFFFF, reverse input=True, reverse output=False, XOR out=0x0000
# ─────────────────────────────────────────
crc16 = crcmod.mkCrcFun(0x11021, initCrc=0xFFFF, rev=True, xorOut=0x0000)

SYNC = bytes([0x1F, 0x01])

# Command codes from IDD Table 9
CMD_SET_MODE = 0x01
CMD_GET_INFO = 0x06
CMD_GET_MODE = 0x08

# Mode codes from IDD Table 8
MODE_SAFE              = 0
MODE_STANDBY           = 1
MODE_EARLY_DEPLOYMENT  = 2
MODE_LATE_DEPLOYMENT   = 3
MODE_INACTIVE_DEORBIT  = 4
MODE_ACTIVE_DEORBIT    = 5


def build_packet(cmd: int, payload: bytes = b'') -> bytes:
    """Build a complete command packet per IDD Table 7."""
    payload_length = len(payload)
    
    # Data CRC (over payload bytes only)
    data_crc = crc16(payload) if payload else 0x0000
    
    # Header CRC input: sync + cmd + length + data_crc
    header_input = SYNC + bytes([cmd]) + struct.pack('<H', payload_length) + struct.pack('<H', data_crc)
    header_crc = crc16(header_input)
    
    packet = (
        SYNC
        + bytes([cmd])
        + struct.pack('<H', payload_length)
        + struct.pack('<H', data_crc)
        + struct.pack('<H', header_crc)
        + payload
    )
    return packet


def send_command(cmd: int, payload: bytes = b'') -> dict:
    """Send a command and return parsed response."""
    packet = build_packet(cmd, payload)
    try:
        with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=TIMEOUT_S) as ser:
            ser.write(packet)
            time.sleep(0.05)
            response = ser.read(64)  # Read up to 64 bytes
            if response:
                return {"status": "ok", "raw": response.hex(), "length": len(response)}
            else:
                return {"status": "timeout", "detail": "No response within timeout"}
    except serial.SerialException as e:
        return {"status": "error", "detail": str(e)}


# ─────────────────────────────────────────
# System state
# ─────────────────────────────────────────
system_state = {
    "running": False,
    "last_action": "Idle",
    "last_response": "—"
}


# ─────────────────────────────────────────
# Flask Web App
# ─────────────────────────────────────────
app = Flask(__name__)
CORS(app)  # Allow React frontend to call this backend

@app.route('/cmd/start', methods=['GET', 'POST'])
def cmd_start():
    """Set mode to Standby (1) then Early Deployment (2)."""
    # Step 1: Enter Standby
    r1 = send_command(CMD_SET_MODE, bytes([MODE_STANDBY]))
    time.sleep(0.5)
    # Step 2: Enter Early Deployment (begins spin/deployment sequence)
    r2 = send_command(CMD_SET_MODE, bytes([MODE_EARLY_DEPLOYMENT]))
    
    system_state["running"] = True
    system_state["last_action"] = "START → Mode 2 (Early Deployment)"
    system_state["last_response"] = r2.get("raw", r2.get("detail", "?"))
    
    return jsonify({
        "action": system_state["last_action"],
        "response": system_state["last_response"],
        "running": system_state["running"]
    })

@app.route('/cmd/stop', methods=['GET', 'POST'])
def cmd_stop():
    """Emergency stop: immediately set Safe mode (0)."""
    r = send_command(CMD_SET_MODE, bytes([MODE_SAFE]))
    
    system_state["running"] = False
    system_state["last_action"] = "EMERGENCY STOP → Mode 0 (Safe)"
    system_state["last_response"] = r.get("raw", r.get("detail", "?"))
    
    return jsonify({
        "action": system_state["last_action"],
        "response": system_state["last_response"],
        "running": system_state["running"]
    })

@app.route('/status')
def status():
    return jsonify(system_state)


if __name__ == '__main__':
    print("=" * 40)
    print("Plasma Brake Controller starting...")
    print(f"Serial port: {SERIAL_PORT} @ {BAUD_RATE} baud")
    print("Open browser at: http://plasmabrake.local:5000")
    print("=" * 40)
    # host='0.0.0.0' makes it accessible from any device on the network
    app.run(host='0.0.0.0', port=5000, debug=False)