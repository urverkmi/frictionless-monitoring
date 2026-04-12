# Aurora Propulsion Plasma Brake Test Rig Monitor

Real-time monitoring system for plasma brake test rig end mass tracking.

## Features
- Real-time position, velocity, and acceleration tracking
- Live, wireless data streaming
- Web-based GUI for visualization and control
- Calibrated measurements in SI units

## Hardware Requirements
- Raspberry Pi 5 (8GB)
- Raspberry Pi High Quality Camera
- ESP32-C6 (spinup controller)

## Setup
```
pip install -r requirements.txt
```

## Running
```bash
./start.sh          # Production mode (requires hardware)
./start.sh --sim    # Simulator mode (PCB + ESP32 simulators)
cd gui && npm start # GUI only (for frontend development)
```

## Vision Data Pipeline
See [docs/api.md](docs/api.md) for the data contract between the C++ AprilTag detector, Python backend, and GUI (UDP JSON format, coordinate frames, kinematics fields).

## Controller App

The Flask backend (`src/controller/controller_app.py`) on port 5001 orchestrates the test rig:

**Endpoints:**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/cmd/start` | POST | Start sequence: PCB deployment mode + ESP32 spinup cycle |
| `/cmd/stop` | POST | Emergency stop: release OFF + ESP32 stop + PCB safe mode |
| `/status` | GET | Current system state |

**Start sequence flow:**
1. PCB enters Standby mode, then Early Deployment mode (RS422 serial)
2. ESP32 receives `PWM:<value>` and runs the spinup cycle autonomously
3. When ESP32 releases its solenoid, it sends `DETACHED` back to the Pi
4. Pi activates GPIO 17 (release signal) upon receiving `DETACHED`

**Hardware interfaces:**
- **PCB**: RS422 binary protocol over `/dev/ttyUSB0` (see IDD rev 5.0)
- **ESP32**: UART text protocol over `/dev/ttyACM0` (see below)
- **Release**: GPIO 17 (BCM) drives MOSFET gate

## ESP32 UART Protocol

Text-based, newline-terminated protocol at 115200 baud.

**Commands (Pi -> ESP32):**
| Command | Description |
|---------|-------------|
| `PWM:<value>\n` | Set target PWM (50-200) and start spinup cycle |
| `STOP\n` | Emergency stop — solenoid OFF, motor OFF |
| `PING\n` | Health check |

**Responses (ESP32 -> Pi):**
| Message | Description |
|---------|-------------|
| `OK\n` | Command acknowledged / spinup cycle complete |
| `DETACHED\n` | Spinup solenoid released (triggers Pi release signal) |
| `PONG\n` | Ping response |

**Spinup cycle (triggered by `PWM:<value>`):**
1. Solenoid ON (400ms activation)
2. Motor ramps up to target PWM (10ms per step)
3. Spin at target speed (1000ms)
4. Solenoid OFF -> sends `DETACHED` to Pi
5. Motor ramps down -> sends `OK` when complete

Default target PWM is 200 if no value is specified from the GUI.

**Wiring options:**
- **USB cable**: ESP32-C6 USB port to Pi USB port. Pi sees `/dev/ttyACM0`.
- **UART wires** (3 wires): Pi GPIO14 (TX) to ESP32 RX, Pi GPIO15 (RX) to ESP32 TX, common GND. Both 3.3V — no level shifter needed.

## ESP32 Firmware
The Arduino sketch is in `esp32/esp32_uart.ino`. Flash to the ESP32-C6 using the Arduino IDE or PlatformIO.
