#!/usr/bin/env python3
"""
TunaCan Plasma Brake Controller
Aurora Propulsion Technologies IDD rev 5.0
"""

import asyncio
import serial
import struct
import crcmod
import threading
import time
from typing import Optional
from bleak import BleakClient, BleakScanner
from flask import Flask, render_template_string, jsonify, request
from flask_cors import CORS

try:
    import gpiod
    from gpiod.line_settings import LineSettings, Direction, Value
    GPIO_AVAILABLE = True
except ImportError:
    GPIO_AVAILABLE = False
    print("[WARN] gpiod not available — GPIO calls will be simulated (macOS dev mode)")


# SERIAL_PORT = "/tmp/ttyVIRT0"    # Simulator mode
SERIAL_PORT = "/dev/ttyUSB0"   # Check with: ls /dev/ttyUSB*
BAUD_RATE   = 115200
TIMEOUT_S   = 1.0

# GPIO config
SOLENOID_GPIO          = 17          # BCM 17 = physical pin 11 (release signal)
GPIO_CHIP              = "/dev/gpiochip0"   # Pi 5 user GPIOs (pinctrl-rp1)
SOLENOID_PULSE_DURATION_S = 2.0      # seconds before auto-release goes LOW after START

# ESP32 UART config
# ESP32_SERIAL_PORT = "/tmp/ttyVIRT_ESP0"   # Simulator mode
ESP32_SERIAL_PORT = "/dev/ttyACM0"      # Real hardware — check with: ls /dev/ttyACM*
ESP32_BAUD_RATE   = 115200
ESP32_TIMEOUT_S   = 2.0

# Code Cell C6 BLE config (Nordic UART Service)
CODECELL_BLE_NAME     = "EMMA"
CODECELL_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
CODECELL_WRITE_UUID   = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
CODECELL_SCAN_TIMEOUT = 10.0

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


_solenoid_req = None

def _init_solenoid():
    """Request the solenoid GPIO line once, initially LOW."""
    global _solenoid_req
    if not GPIO_AVAILABLE or _solenoid_req is not None:
        return
    settings = LineSettings(direction=Direction.OUTPUT, output_value=Value.INACTIVE)
    _solenoid_req = gpiod.request_lines(
        GPIO_CHIP,
        consumer="solenoid-ctrl",
        config={SOLENOID_GPIO: settings},
    )

def set_solenoid(on: bool):
    """Drive the MOSFET gate HIGH (release on) or LOW (release off)."""
    if not GPIO_AVAILABLE:
        print(f"[GPIO-SIM] Solenoid {'ON' if on else 'OFF'}")
        return
    _init_solenoid()
    val = Value.ACTIVE if on else Value.INACTIVE
    _solenoid_req.set_value(SOLENOID_GPIO, val)
    print(f"[GPIO] GPIO {SOLENOID_GPIO} {'HIGH' if on else 'LOW'}")


# ─────────────────────────────────────────
# ESP32 UART connection
# ─────────────────────────────────────────
class ESP32Connection:
    """Persistent UART link to the ESP32 spinup controller."""

    def __init__(self, port, baudrate, timeout):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self._ser = None
        self._lock = threading.Lock()
        self._listener_running = False

    def connect(self):
        """Open the serial port. Safe to call if already connected."""
        if self._ser and self._ser.is_open:
            return True
        try:
            self._ser = serial.Serial(self.port, self.baudrate, timeout=self.timeout)
            time.sleep(2)                    # wait for ESP32 DTR-reset to finish booting
            self._ser.reset_input_buffer()   # discard boot messages before sending commands
            print(f"[ESP32] Connected on {self.port}")
            if not self._listener_running:
                self.start_listener()
            return True
        except serial.SerialException as e:
            print(f"[ESP32] Connection failed: {e}")
            self._ser = None
            return False

    def send(self, command: str) -> Optional[str]:
        """Send a text command and return None (responses handled by listener)."""
        with self._lock:
            if not self.connect():
                return None
            try:
                self._ser.write((command.strip() + "\n").encode())
                print(f"[ESP32] Sent: {command.strip()}")
                return "sent"
            except serial.SerialException as e:
                print(f"[ESP32] Serial error: {e}")
                self._ser = None
                return None

    def start_listener(self):
        """Background thread that reads unsolicited messages from ESP32."""
        self._listener_running = True
        def _listen():
            while self._listener_running:
                try:
                    if not self._ser or not self._ser.is_open:
                        time.sleep(0.5)
                        continue
                    line = self._ser.readline().decode(errors='replace').strip()
                    if not line:
                        continue
                    print(f"[ESP32] Received: {line}")
                    if line == "DETACHED":
                        system_state["esp32_detached"] = True
                        try:
                            codecell.send("1")
                        except Exception as e:
                            print(f"[CodeCell] activate failed: {e}")
                        try:
                            set_solenoid(True)
                            print("[ESP32] DETACHED received — release activated (GPIO 17 HIGH)")
                        except Exception as e:
                            print(f"[ESP32] Release GPIO failed: {e}")
                    system_state["esp32_last_response"] = line
                except serial.SerialException:
                    time.sleep(0.5)
                except Exception as e:
                    print(f"[ESP32] Listener error: {e}")
                    time.sleep(0.5)

        t = threading.Thread(target=_listen, daemon=True)
        t.start()

    def close(self):
        self._listener_running = False
        if self._ser and self._ser.is_open:
            self._ser.close()
            self._ser = None


esp32 = ESP32Connection(ESP32_SERIAL_PORT, ESP32_BAUD_RATE, ESP32_TIMEOUT_S)


# ─────────────────────────────────────────
# Code Cell C6 BLE link
# ─────────────────────────────────────────
class CodeCellBLE:
    """Persistent BLE link to the Code Cell C6 (NUS write-only protocol)."""

    def __init__(self, name: str, service_uuid: str, write_uuid: str):
        self.name = name
        self.service_uuid = service_uuid
        self.write_uuid = write_uuid

        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._loop.run_forever, daemon=True)
        self._thread.start()

        self._client: Optional[BleakClient] = None
        self._lock = threading.Lock()

    def _run(self, coro, timeout: float):
        return asyncio.run_coroutine_threadsafe(coro, self._loop).result(timeout=timeout)

    async def _async_connect(self) -> bool:
        if self._client and self._client.is_connected:
            return True
        device = await BleakScanner.find_device_by_name(self.name, timeout=CODECELL_SCAN_TIMEOUT)
        if device is None:
            print(f"[CodeCell] Device '{self.name}' not found")
            return False
        client = BleakClient(device, disconnected_callback=self._on_disconnect)
        await client.connect()
        self._client = client
        print(f"[CodeCell] Connected to {self.name} ({device.address})")
        return True

    async def _async_send(self, payload: bytes):
        if not (self._client and self._client.is_connected):
            await self._async_connect()
        if not (self._client and self._client.is_connected):
            raise RuntimeError("CodeCell not connected")
        await self._client.write_gatt_char(self.write_uuid, payload, response=False)

    def _on_disconnect(self, _client):
        print("[CodeCell] Disconnected")

    def connect(self) -> bool:
        with self._lock:
            try:
                return self._run(self._async_connect(), timeout=CODECELL_SCAN_TIMEOUT + 5)
            except Exception as e:
                print(f"[CodeCell] Connect failed: {e}")
                return False

    def send(self, command: str) -> Optional[str]:
        """Send 'on'/'off' style command. Anything starting with '1' → b'1', else b'0'."""
        cmd = command.strip()
        payload = b"1" if cmd.startswith("1") else b"0"
        with self._lock:
            try:
                self._run(self._async_send(payload), timeout=5.0)
                print(f"[CodeCell] Sent: {payload.decode()}")
                return "sent"
            except Exception as e:
                print(f"[CodeCell] Send failed: {e}")
                self._client = None
                return None

    def close(self):
        if self._client and self._client.is_connected:
            try:
                self._run(self._client.disconnect(), timeout=5.0)
            except Exception:
                pass
        self._loop.call_soon_threadsafe(self._loop.stop)


codecell = CodeCellBLE(CODECELL_BLE_NAME, CODECELL_SERVICE_UUID, CODECELL_WRITE_UUID)


# ─────────────────────────────────────────
# System state
# ─────────────────────────────────────────
system_state = {
    "running": False,
    "last_action": "Idle",
    "last_response": "—",
    "esp32_last_response": "—",
    "esp32_detached": False
}


# ─────────────────────────────────────────
# Flask Web App
# ─────────────────────────────────────────
app = Flask(__name__)
CORS(app)  # Allow React frontend to call this backend

@app.route('/cmd/start', methods=['GET', 'POST'])
def cmd_start():
    # Read target PWM from request body (default 200)
    data = request.get_json(silent=True) or {}
    target_pwm = data.get("targetPWM", 200)

    # Step 1: Enter Standby
    r1 = send_command(CMD_SET_MODE, bytes([MODE_STANDBY]))
    time.sleep(0.5)
    # Step 2: Enter Early Deployment
    r2 = send_command(CMD_SET_MODE, bytes([MODE_EARLY_DEPLOYMENT]))

    # Step 3: Set GPIO 17 HIGH (release signal), then auto-LOW after duration
    pulse_duration = data.get("solenoidDuration", SOLENOID_PULSE_DURATION_S)
    try:
        set_solenoid(True)
        def _auto_off():
            time.sleep(pulse_duration)
            try:
                set_solenoid(False)
                print(f"[GPIO] Auto-release OFF after {pulse_duration}s")
            except Exception as e:
                print(f"[GPIO] Auto-release failed: {e}")
        threading.Thread(target=_auto_off, daemon=True).start()
    except Exception as e:
        print(f"[WARN] GPIO set HIGH failed: {e}")

    # Step 4: Signal ESP32 to run spinup cycle
    system_state["esp32_detached"] = False
    esp32_result = esp32.send(f"PWM:{target_pwm}")
    if esp32_result is None:
        print("[WARN] ESP32 did not respond to PWM command")

    system_state["running"] = True
    system_state["last_action"] = f"START → Mode 2 + ESP32 PWM:{target_pwm}"
    system_state["last_response"] = r2.get("raw", r2.get("detail", "?"))

    return jsonify({
        "action": system_state["last_action"],
        "response": system_state["last_response"],
        "running": system_state["running"],
        "targetPWM": target_pwm,
        "solenoidDuration": pulse_duration
    })

@app.route('/cmd/stop', methods=['GET', 'POST'])
def cmd_stop():
    # Kill release signal first (safe state)
    try:
        set_solenoid(False)
    except Exception as e:
        print(f"[GPIO] set_solenoid(False) failed: {e}")

    # Deactivate Code Cell over BLE (best-effort)
    try:
        codecell.send("0")
    except Exception as e:
        print(f"[CodeCell] deactivate failed: {e}")

    # Emergency stop ESP32
    esp32_result = esp32.send("STOP")
    if esp32_result is None:
        print("[WARN] ESP32 did not respond to STOP command")

    r = send_command(CMD_SET_MODE, bytes([MODE_SAFE]))

    system_state["running"] = False
    system_state["esp32_detached"] = False
    system_state["last_action"] = "STOP → Mode 0 + Release OFF + ESP32 STOP"
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
    _init_solenoid()
    print("=" * 40)
    print("Plasma Brake Controller starting...")
    print(f"Serial port: {SERIAL_PORT} @ {BAUD_RATE} baud")
    print("Open browser at: http://plasmabrake.local:5001")
    print("=" * 40)
    # host='0.0.0.0' makes it accessible from any device on the network
    app.run(host='0.0.0.0', port=5001, debug=False)
