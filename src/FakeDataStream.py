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

# Simulated geometry (satellite fixed, end mass moves around it)
SATELLITE_X = 100.0
SATELLITE_Y = 100.0
# End mass: start (150,150), move to (160,160), back to (150,150) over a few frames
TETHER_LENGTH = 150
MAIN_SIZE = 20
END_MASS_RADIUS = 12


def make_end_mass_position(frame_id: int) -> tuple:
    """Simple toy trajectory for the end mass.

    Adjust this if you want different fake motion.
    """
    step = frame_id % 4
    if step == 0 or step >= 2:
        return (150.0, 150.0)
    return (160.0, 160.0)


def angle_from_center(sat_x: float, sat_y: float, ex: float, ey: float) -> float:
    """Angle of end mass from satellite (radians, 0 to 2*pi)."""
    dx = ex - sat_x
    dy = ey - sat_y
    return (math.atan2(dy, dx) + 2 * math.pi) % (2 * math.pi)


def frame_data_to_gui_payload(frame_data, satellite_xy: tuple) -> dict:
    """Convert FrameData + satellite position to the object shape dataService.js expects."""
    sx, sy = satellite_xy
    return {
        "mainPosition": {"x": sx, "y": sy},
        "endMassPosition": {
            "x": frame_data.position.x,
            "y": frame_data.position.y,
        },
        "linearSpeed": {
            "x": frame_data.velocity.x,
            "y": frame_data.velocity.y,
            "z": 0,
        },
        "angularSpeed": {
            "x": 0,
            "y": 0,
            "z": frame_data.angular_velocity,
        },
        "tetherLength": TETHER_LENGTH,
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
        # 1) Generate fake end‑mass position for this frame
        ex, ey = make_end_mass_position(frame_id)
        frame_id += 1

        angle = angle_from_center(SATELLITE_X, SATELLITE_Y, ex, ey)
        # 2) Store position in MemoryManager (same format as PositionDetector)
        position_data = {
            "timestamp": timestamp,
            "frame_id": frame_id,
            "position": Vector2D(ex, ey),
            "angular_position": angle,
            "tracking_confidence": 1.0,
        }
        memory.store_position_data(position_data)
        # 3) Run kinematics on recent history
        frame_data = kinematics.process_frame()

        if frame_data is not None:
            # 4) Convert to GUI payload shape and send over WebSocket
            payload = frame_data_to_gui_payload(
                frame_data, (SATELLITE_X, SATELLITE_Y)
            )
            try:
                await websocket.send(json.dumps(payload))
            except Exception:
                break

        elapsed = time.perf_counter() - t0
        sleep_time = FRAME_INTERVAL - elapsed
        if sleep_time > 0:
            await asyncio.sleep(sleep_time)


async def handler(websocket, path):
    """Handle one client: run stream until disconnect."""
    print(f"Client connected from {websocket.remote_address}")
    try:
        await stream_loop(websocket)
    except Exception as e:
        print(f"Stream error: {e}")
    finally:
        print("Client disconnected")


async def run_server():
    import websockets
    async with websockets.serve(handler, "localhost", DATA_PORT):
        await asyncio.Future()  # run forever


def main():
    try:
        import websockets
    except ImportError:
        print("Install websockets: pip install websockets")
        sys.exit(1)

    print(f"Fake data stream starting on ws://localhost:{DATA_PORT} at {FPS} fps")
    print("In gui set USE_MOCK_DATA = false to connect to this stream.")
    asyncio.run(run_server())


if __name__ == "__main__":
    main()
