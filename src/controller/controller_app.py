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
from flask import Flask, render_template_string, jsonify, request
from flask_cors import CORS

try:
    import gpiod
    from gpiod.line_settings import LineSettings, Direction, Value
    GPIO_AVAILABLE = True
except ImportError:
    GPIO_AVAILABLE = False
    print("[WARN] gpiod not available — GPIO calls will be simulated (macOS dev mode)")


SERIAL_PORT = "/tmp/ttyVIRT0"    # Simulator mode
# SERIAL_PORT = "/dev/ttyUSB0"   # Check with: ls /dev/ttyUSB*
BAUD_RATE   = 115200
TIMEOUT_S   = 1.0

# GPIO config
SOLENOID_GPIO = 17          # BCM 17 = physical pin 11 (release signal)
GPIO_CHIP     = "/dev/gpiochip4"   # Pi 5 user GPIOs

# ESP32 UART config
ESP32_SERIAL_PORT = "/tmp/ttyVIRT_ESP0"   # Simulator mode
# ESP32_SERIAL_PORT = "/dev/ttyACM0"      # Real hardware — check with: ls /dev/ttyACM*
ESP32_BAUD_RATE   = 115200
ESP32_TIMEOUT_S   = 2.0

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


def set_solenoid(on: bool):
    """Drive the MOSFET gate HIGH (release on) or LOW (release off)."""
    if not GPIO_AVAILABLE:
        print(f"[GPIO-SIM] Solenoid {'ON' if on else 'OFF'}")
        return
    val = Value.ACTIVE if on else Value.INACTIVE
    settings = LineSettings(direction=Direction.OUTPUT, output_value=val)
    req = gpiod.request_lines(
        GPIO_CHIP,
        consumer="solenoid-ctrl",
        config={SOLENOID_GPIO: settings},
    )
    req.release()


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
            print(f"[ESP32] Connected on {self.port}")
            if not self._listener_running:
                self.start_listener()
            return True
        except serial.SerialException as e:
            print(f"[ESP32] Connection failed: {e}")
            self._ser = None
            return False

    def send(self, command: str) -> str | None:
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

    # Step 3: Signal ESP32 to run spinup cycle
    # GPIO 17 (release) is NOT activated here — the ESP32 listener thread
    # will activate it when it receives DETACHED from the ESP32.
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
        "targetPWM": target_pwm
    })

@app.route('/cmd/stop', methods=['GET', 'POST'])
def cmd_stop():
    # Kill release signal first (safe state)
    try:
        set_solenoid(False)
    except Exception as e:
        pass  # Still try to send safe mode even if GPIO fails

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
    print("=" * 40)
    print("Plasma Brake Controller starting...")
    print(f"Serial port: {SERIAL_PORT} @ {BAUD_RATE} baud")
    print("Open browser at: http://plasmabrake.local:5001")
    print("=" * 40)
    # host='0.0.0.0' makes it accessible from any device on the network
    app.run(host='0.0.0.0', port=5001, debug=False)