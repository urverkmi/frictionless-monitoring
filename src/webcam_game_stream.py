"""
Webcam-based ArUco marker tracker for testing the velocity game locally.

Uses your laptop webcam to detect an ArUco marker (ID 0 by default),
compute its velocity from frame-to-frame position changes, and stream
the data over WebSocket on port 8080 — the same format the React GUI expects.

Requirements:
    pip install opencv-python opencv-contrib-python websockets numpy

Usage:
    python3 src/webcam_game_stream.py

Then in another terminal:
    cd gui && npm run game

Print any ArUco marker from the DICT_4X4_50 dictionary (ID 0-49).
A quick way to get one: search "ArUco marker generator 4x4" online,
or run:  python3 -c "import cv2; d=cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50); img=cv2.aruco.generateImageMarker(d,0,400); cv2.imwrite('marker.png',img)"
"""

import asyncio
import json
import math
import sys
import time
from pathlib import Path

import cv2
import numpy as np

# Ensure src is importable
_src_dir = Path(__file__).resolve().parent
if str(_src_dir) not in sys.path:
    sys.path.insert(0, str(_src_dir))

from data_structures import Vector2D
from memory import MemoryManager
from kinematics import KinematicsCalculator

WS_PORT = 8080
CAMERA_INDEX = 0

# Calibration: known marker physical size and distance from camera.
# The marker's apparent size in pixels lets us compute a px -> cm ratio
# each frame, so velocity is reported in cm/s.
MARKER_SIZE_CM = 10.0  # 10 cm x 10 cm printed marker


def frame_data_to_gui_payload(frame_data):
    angle = frame_data.angular_position
    omega = frame_data.angular_velocity
    return {
        "satellitePosition": {"x": 0.0, "y": 0.0},
        "mainPosition": {"x": 0.0, "y": 0.0},
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
        "mainSize": 20,
        "endMassRadius": 12,
        "trackingConfidence": frame_data.tracking_confidence,
        "timestamp": int(frame_data.timestamp * 1000),
    }


async def stream_webcam(websocket):
    memory = MemoryManager(buffer_size=100)
    kinematics = KinematicsCalculator(memory)

    cap = cv2.VideoCapture(CAMERA_INDEX)
    if not cap.isOpened():
        print("[webcam] ERROR: Could not open webcam", file=sys.stderr)
        return

    fps = cap.get(cv2.CAP_PROP_FPS) or 30
    frame_interval = 1.0 / fps
    aruco_dict = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50)
    aruco_params = cv2.aruco.DetectorParameters()
    detector = cv2.aruco.ArucoDetector(aruco_dict, aruco_params)

    frame_id = 0
    print(f"[webcam] Streaming from webcam at ~{fps:.0f} fps", file=sys.stderr)
    print("[webcam] Show an ArUco 4x4 marker (ID 0-49) to the camera", file=sys.stderr)

    try:
        while True:
            t0 = time.perf_counter()
            ret, frame = cap.read()
            if not ret:
                await asyncio.sleep(0.01)
                continue

            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            corners, ids, _ = detector.detectMarkers(gray)

            if ids is not None and len(ids) > 0:
                # Use the first detected marker
                marker_corners = corners[0][0]  # 4 corner points

                # Compute px -> cm scale from apparent marker size.
                # Average the side lengths for robustness to perspective.
                side_lengths = [
                    np.linalg.norm(marker_corners[i] - marker_corners[(i + 1) % 4])
                    for i in range(4)
                ]
                marker_px = float(np.mean(side_lengths))
                cm_per_px = MARKER_SIZE_CM / marker_px if marker_px > 0 else 1.0

                # Convert pixel centre to cm
                center_x = float(np.mean(marker_corners[:, 0])) * cm_per_px
                center_y = float(np.mean(marker_corners[:, 1])) * cm_per_px

                frame_id += 1
                timestamp = time.time()

                # Satellite at frame center (in cm), end mass = marker
                h, w = frame.shape[:2]
                sat_x = (w / 2.0) * cm_per_px
                sat_y = (h / 2.0) * cm_per_px

                angle = (math.atan2(center_y - sat_y, center_x - sat_x) + 2 * math.pi) % (2 * math.pi)

                position_data = {
                    "timestamp": timestamp,
                    "frame_id": frame_id,
                    "satellite_position": Vector2D(sat_x, sat_y),
                    "end_mass_position": Vector2D(center_x, center_y),
                    "orbital_angular_position": angle,
                    "tracking_confidence": 1.0,
                }
                memory.store_position_data(position_data)
                frame_data = kinematics.process_frame()

                if frame_data is not None:
                    payload = frame_data_to_gui_payload(frame_data)
                    try:
                        await websocket.send(json.dumps(payload))
                    except Exception:
                        break

            elapsed = time.perf_counter() - t0
            sleep_time = max(0, frame_interval - elapsed)
            await asyncio.sleep(sleep_time)
    finally:
        cap.release()


async def main():
    import websockets

    print(f"[webcam] WebSocket server on ws://0.0.0.0:{WS_PORT}", file=sys.stderr)
    async with websockets.serve(stream_webcam, "0.0.0.0", WS_PORT):
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
