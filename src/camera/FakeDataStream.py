"""
Fake data stream for development and testing.

Sends position data at 60 fps in the format described by the data stream interface:
- Sampling: 60 frames per second
- Per frame: satellite (x, y), end mass (x, y)
- JSON format: [satellite_x, satellite_y, end_mass_x, end_mass_y] or full object for GUI

The script runs a WebSocket server on DATA_PORT (8080). The GUI's dataService.js
connects to ws://localhost:8080 when USE_MOCK_DATA is false.

Usage:
  python -m src.FakeDataStream
  # or from project root: python src/FakeDataStream.py
"""

import asyncio
import json
import math
import sys
import time
from pathlib import Path

# Ensure src directory is on path so imports like "from memory" work
_src_dir = Path(__file__).resolve().parent
if str(_src_dir) not in sys.path:
    sys.path.insert(0, str(_src_dir))

from data_structures import Vector2D
from memory import MemoryManager
from kinematics import KinematicsCalculator

# Must match gui/src/services/dataService.js
DATA_PORT = 8080
FPS = 60
FRAME_INTERVAL = 1.0 / FPS

# Simulated geometry
SATELLITE_X = 100.0
SATELLITE_Y = 100.0
SATELLITE_DRIFT_RADIUS = 20.0
MAIN_SIZE = 20
END_MASS_RADIUS = 12


def make_satellite_position(frame_id: int) -> tuple:
    """Satellite drifts slowly to model free movement."""
    angle = (frame_id / FPS) * 0.08
    sx = SATELLITE_X + SATELLITE_DRIFT_RADIUS * math.cos(angle)
    sy = SATELLITE_Y + SATELLITE_DRIFT_RADIUS * math.sin(angle)
    return (sx, sy)


def make_end_mass_position(frame_id: int, sat_x: float, sat_y: float) -> tuple:
    """Simple toy trajectory for the end mass.

    Circular motion around satellite so angular velocity is non-zero.
    """
    # Rotate around satellite at ~0.5 rad/s (one full rotation in ~12 seconds)
    angle = (frame_id / FPS) * 0.5  # radians
    radius = 50.0  # distance from satellite
    ex = sat_x + radius * math.cos(angle)
    ey = sat_y + radius * math.sin(angle)
    return (ex, ey)


def angle_from_center(sat_x: float, sat_y: float, ex: float, ey: float) -> float:
    """Angle of end mass from satellite (radians, 0 to 2*pi)."""
    dx = ex - sat_x
    dy = ey - sat_y
    return (math.atan2(dy, dx) + 2 * math.pi) % (2 * math.pi)


def frame_data_to_gui_payload(frame_data) -> dict:
    """Convert FrameData + satellite position to the object shape dataService.js expects."""
    sx, sy = frame_data.satellite_position.x, frame_data.satellite_position.y
    # Represent angular speed in x/y components for GUI display requirements.
    angle = frame_data.angular_position
    omega = frame_data.angular_velocity
    return {
        "satellitePosition": {"x": sx, "y": sy},
        # Keep legacy field for compatibility with older GUI code.
        "mainPosition": {"x": sx, "y": sy},
        "endMassPosition": {
            "x": frame_data.position.x,
            "y": frame_data.position.y,
        },
        "linearSpeed": {
            "x": frame_data.velocity.x,
            "y": frame_data.velocity.y,
        },
        "angularSpeed": {
            "x": omega * math.cos(angle),
            "y": omega * math.sin(angle),
        },
        "tetherLength": frame_data.tether_length,
        "mainSize": MAIN_SIZE,
        "endMassRadius": END_MASS_RADIUS,
        "timestamp": int(frame_data.timestamp * 1000),
    }


async def stream_loop(websocket):
    """Generate fake frames, run kinematics, send JSON to a single client."""
    memory = MemoryManager(buffer_size=100)
    kinematics = KinematicsCalculator(memory)
    frame_id = 0

    while True:
        t0 = time.perf_counter()
        timestamp = time.time()
        # 1) Generate fake satellite + end-mass positions for this frame
        sx, sy = make_satellite_position(frame_id)
        ex, ey = make_end_mass_position(frame_id, sx, sy)
        frame_id += 1

        angle = angle_from_center(sx, sy, ex, ey)
        # 2) Store position in MemoryManager (same format as PositionDetector)
        position_data = {
            "timestamp": timestamp,
            "frame_id": frame_id,
            "satellite_position": Vector2D(sx, sy),
            "end_mass_position": Vector2D(ex, ey),
            "orbital_angular_position": angle,
            "tracking_confidence": 1.0,
        }
        memory.store_position_data(position_data)
        # 3) Run kinematics on recent history
        frame_data = kinematics.process_frame()

        if frame_data is not None:
            # 4) Convert to GUI payload shape and send over WebSocket
            payload = frame_data_to_gui_payload(frame_data)
            try:
                await websocket.send(json.dumps(payload))
            except Exception:
                break

        elapsed = time.perf_counter() - t0
        sleep_time = FRAME_INTERVAL - elapsed
        if sleep_time > 0:
            await asyncio.sleep(sleep_time)


async def handler(websocket):
    """Handle one client: run stream until disconnect."""
    print(f"Client connected")
    try:
        await stream_loop(websocket)
    except Exception as e:
        print(f"Stream error: {e}")
    finally:
        print("Client disconnected")


async def run_server():
    import websockets
    async with websockets.serve(handler, "0.0.0.0", DATA_PORT):
        await asyncio.Future()  # run forever


def main():
    try:
        import websockets
    except ImportError:
        print("Install websockets: pip install websockets")
        sys.exit(1)

    print(f"Fake data stream starting on ws://0.0.0.0:{DATA_PORT} at {FPS} fps")
    print("In gui set USE_MOCK_DATA = false to connect to this stream.")
    asyncio.run(run_server())


if __name__ == "__main__":
    main()
