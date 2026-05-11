# Frictionless: A Plasma Brake Test Rig

A real-time monitoring system for our plasma brake deployment test rig. 

Designed for Aalto University's [PdP 25-26](https://pdp.fi/gala26/) by Team Friction<, sponsored by [Aurora Propulsions Technologies](https://aurorapt.space/).

## Features
- Real-time position, velocity, and acceleration tracking
- Live, wireless data streaming
- Web-based GUI for visualization and control
- Calibrated measurements in SI units

## Hardware Requirements
- Raspberry Pi 5 (8GB)
- Raspberry Pi High Quality Camera
- ESP32-C6 (spinup controller)
- 4 calibration AprilTags (IDs 2, 3, 4, 5) placed at the four corners of a
  known square on the test surface
- 2 tracking AprilTags (ID 0 = satellite, ID 1 = end mass)

All AprilTags use the **tag36h11** family.

## First-Time Setup

Clone the repository onto the Pi, then:

```bash
cd frictionless-monitoring

# Python deps (creates / reuses ./venv if present, otherwise system pip)
pip install -r requirements.txt

# C++ vision pipeline
cd vision
cmake -S . -B build
cmake --build build -j4
cd ..

# GUI deps (only needed the first time; start.sh runs npm install if missing)
cd gui && npm install && cd ..
```

## Running
```bash
./start.sh          # Production mode (requires hardware)
./start.sh --sim    # Simulator mode (PCB + ESP32 simulators)
cd gui && npm start # GUI only (for frontend development)
```

See [`vision/README.md`](vision/README.md) for full build details and
troubleshooting of the C++ pipeline (libcamera, GStreamer, AprilTag).

When `./start.sh` runs it will, in order:
1. Kill any stale processes from a previous run (ports 5001 / 3000 / 8080).
2. Install / update Python dependencies.
3. (Simulator mode only) start the PCB and ESP32 simulators.
4. Start the Flask control backend on **port 5001**.
5. Start the UDP-to-WebSocket bridge (`cpp_stream_bridge.py`) on **port 8080**.
6. Start the C++ AprilTag vision binary (`vision/build/apriltag_demo`).
7. Start the React GUI on **port 3000**.

Live logs are written to `logs/` (`flask.log`, `bridge.log`, `vision.log`,
`react.log`, and `simulator.log` / `esp32_sim.log` in sim mode).

## Extrinsic Calibration

The vision pipeline performs **extrinsic calibration automatically** on
start-up by detecting four reference AprilTags placed at the corners of a
known square on the test surface (IDs 2, 3, 4, 5). Calibration runs once,
the moment all four reference tags are visible in the frame, and you will
see this in `logs/vision.log`:

```
[INFO] Calibration successful
```

> **physical setup parameters must match the code.**
> Two constants in [`vision/main.cpp`](vision/main.cpp) define the physical
> geometry of the calibration setup, and **must be updated to match the
> actual printed tags and their placement on the table**:
>
> ```cpp
> constexpr double TAG_SIZE     = 0.056;   // metres — printed AprilTag side length
> constexpr double CALIB_SQUARE = 1.56;    // metres — side of the square formed
>                                          //          by the four calibration tags
> ```
>
> - `TAG_SIZE` is the black-border-to-black-border side length of a single
>   printed AprilTag, in metres. Measure your printed tags with calipers.
> - `CALIB_SQUARE` is the side length of the square whose four corners hold
>   calibration tags 2, 3, 4 and 5 (inner edges of the tags, see
>   `worldTagCorners` in `main.cpp` for the exact corner convention).
>
> After changing either value, **rebuild the C++ binary**:
>
> ```bash
> cd vision && cmake --build build -j4
> ```

If `[INFO] Calibration successful` never appears, check that all four
calibration tags are clearly visible to the camera, well-lit, and that
`TAG_SIZE` / `CALIB_SQUARE` reflect your physical setup.

The camera **intrinsics** (`K`, `D` in `main.cpp`) are pre-calibrated for the
HQ Camera at 2028×1520. If you change `resolution_divider` or swap the
camera/lens, redo intrinsic calibration — see `vision/README.md` and
`src/camera/calib_intrinsics.py`.

## Vision Data Pipeline
See [docs/api.md](docs/api.md) for the data contract between the C++ AprilTag detector, Python backend, and GUI (UDP JSON format, coordinate frames, kinematics fields).

## De-orbit Snoopy!

An interactive game that reuses the AprilTag camera pipeline, designed for our Gala visitors to have some fun while getting an experience of our system. A single puck (an air bearing carrying an AprilTag) sits on the table; the player can flick it toward a target zone shown in the GUI.

**Run:**
```bash
GAME_MODE=1 PUCK_TAG_ID=<id> ./start.sh   # <id> = AprilTag ID on your puck (e.g. 6)
```
Then open `http://<pi-host>:3000/game` in a browser and click **Start round**.

`GAME_MODE=1` makes `start.sh` launch the C++ detector with `--single-tag <id>`, which emits UDP whenever the puck is visible (no second tag required). Without `GAME_MODE` set, the launcher behaves identically to before — the plasma-brake monitor at `/` is untouched.

**Tuning:** all game parameters (table bounds, target size, round duration, scoring threshold) live in [`gui/src/gameConfig.js`](gui/src/gameConfig.js) with inline comments — edit and reload the GUI.


## Controller App
The Flask backend (`src/controller/controller_app.py`) on port 5001 orchestrates the test rig:

**Endpoints:**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/cmd/start` | POST | Start sequence: PCB deployment mode + ESP32 spinup cycle |
| `/cmd/stop` | POST | Emergency stop: release OFF + ESP32 stop + PCB safe mode |
| `/status` | GET | Current system state |

**Start sequence flow:**
1. ESP32 receives `PWM:<value>` and runs the spinup cycle autonomously
2. When ESP32 releases its solenoid, it sends `DETACHED` back to the Pi
3. Pi activates GPIO 17 (release signal) upon receiving `DETACHED`

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

**Wiring:**
ESP32-C6 USB port to Pi USB port. Pi sees `/dev/ttyACM0`.

## ESP32 Firmware
The Arduino sketch is in `esp32/esp32_uart.ino`. Flash to the ESP32-C6 using the Arduino IDE or PlatformIO.
