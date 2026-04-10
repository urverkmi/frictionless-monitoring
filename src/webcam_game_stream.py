"""
Webcam-based ArUco marker tracker for testing the velocity game locally.

Uses your laptop webcam to detect an ArUco marker (ID 0 by default),
compute its velocity from frame-to-frame position changes, and stream
the data over WebSocket on port 8080 — the same format the React GUI expects.

Opens an OpenCV preview window so you can see what the camera sees and
whether the marker is being detected.

Requirements:
    pip install opencv-python opencv-contrib-python websockets numpy

Usage:
    python3 src/webcam_game_stream.py          # with preview window
    python3 src/webcam_game_stream.py --no-gui  # headless

Print any ArUco marker from the DICT_4X4_50 dictionary (ID 0-49).
Generate one:
    python3 -c "import cv2; d=cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50); img=cv2.aruco.generateImageMarker(d,0,400); cv2.imwrite('marker.png',img)"
"""

import argparse
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


async def stream_webcam(websocket, show_gui=True):
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
    last_speed = 0.0
    frames_since_detection = 0

    print(f"[webcam] Camera opened: {int(cap.get(3))}x{int(cap.get(4))} @ {fps:.0f}fps", file=sys.stderr)
    print(f"[webcam] Marker calibration: {MARKER_SIZE_CM} cm physical size", file=sys.stderr)
    print("[webcam] Show an ArUco 4x4 marker (ID 0-49) to the camera", file=sys.stderr)
    if show_gui:
        print("[webcam] Preview window open — press 'q' to quit", file=sys.stderr)

    try:
        while True:
            t0 = time.perf_counter()
            ret, frame = cap.read()
            if not ret:
                await asyncio.sleep(0.01)
                continue

            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            corners, ids, rejected = detector.detectMarkers(gray)
            # Flip for display only (after detection, so marker pattern isn't mirrored)
            display_frame = cv2.flip(frame, 1) if show_gui else frame

            detected = ids is not None and len(ids) > 0

            if detected:
                frames_since_detection = 0
                marker_corners = corners[0][0]  # 4 corner points

                # Compute px -> cm scale from apparent marker size.
                side_lengths = [
                    np.linalg.norm(marker_corners[i] - marker_corners[(i + 1) % 4])
                    for i in range(4)
                ]
                marker_px = float(np.mean(side_lengths))
                cm_per_px = MARKER_SIZE_CM / marker_px if marker_px > 0 else 1.0

                # Pixel center (for drawing)
                px_center_x = float(np.mean(marker_corners[:, 0]))
                px_center_y = float(np.mean(marker_corners[:, 1]))

                # Convert to cm
                center_x = px_center_x * cm_per_px
                center_y = px_center_y * cm_per_px

                frame_id += 1
                timestamp = time.time()

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
                    last_speed = math.sqrt(payload["linearSpeed"]["x"]**2 + payload["linearSpeed"]["y"]**2)
                    try:
                        await websocket.send(json.dumps(payload))
                    except Exception:
                        break

                # Draw on preview (mirrored display frame)
                if show_gui:
                    disp_w = display_frame.shape[1]
                    # Mirror the marker corners for drawing on flipped frame
                    mirrored_corners = [c.copy() for c in corners]
                    for mc in mirrored_corners:
                        mc[:, :, 0] = disp_w - mc[:, :, 0]
                    cv2.aruco.drawDetectedMarkers(display_frame, mirrored_corners, ids)
                    # Draw center dot (mirrored x)
                    cv2.circle(display_frame, (int(disp_w - px_center_x), int(px_center_y)), 6, (0, 255, 0), -1)
                    # Show marker ID and scale
                    cv2.putText(display_frame, f"ID:{ids[0][0]}  {marker_px:.0f}px  scale:{cm_per_px:.3f} cm/px",
                                (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                    # Show speed
                    cv2.putText(display_frame, f"Speed: {last_speed:.2f} cm/s",
                                (10, 65), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                    # Show position
                    cv2.putText(display_frame, f"Pos: ({center_x:.1f}, {center_y:.1f}) cm",
                                (10, 100), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            else:
                frames_since_detection += 1
                if show_gui:
                    color = (0, 0, 255)
                    cv2.putText(display_frame, "No marker detected", (10, 30),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)
                    if len(rejected) > 0:
                        cv2.putText(display_frame, f"({len(rejected)} candidates rejected)",
                                    (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 140, 255), 1)

                # Terminal warning every 2 seconds of no detection
                if frames_since_detection == int(fps * 2):
                    print("[webcam] No marker detected for 2s — check lighting and marker visibility", file=sys.stderr)

            if show_gui:
                cv2.putText(display_frame, f"WS clients: 1  |  Frames sent: {frame_id}",
                            (10, display_frame.shape[0] - 15), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (180, 180, 180), 1)

                cv2.imshow("Webcam - ArUco Tracker", display_frame)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break

            elapsed = time.perf_counter() - t0
            sleep_time = max(0, frame_interval - elapsed)
            await asyncio.sleep(sleep_time)
    finally:
        cap.release()
        if show_gui:
            cv2.destroyAllWindows()


# Store show_gui flag globally so the handler closure can access it
_show_gui = True


async def main():
    import websockets

    parser = argparse.ArgumentParser(description="Webcam ArUco tracker for velocity game")
    parser.add_argument("--no-gui", action="store_true", help="Disable preview window")
    parser.add_argument("--port", type=int, default=WS_PORT)
    args = parser.parse_args()

    global _show_gui
    _show_gui = not args.no_gui

    print(f"[webcam] WebSocket server on ws://0.0.0.0:{args.port}", file=sys.stderr)
    print(f"[webcam] Preview window: {'ON' if _show_gui else 'OFF'}", file=sys.stderr)
    print(f"[webcam] Waiting for browser to connect...", file=sys.stderr)

    async def handler(websocket):
        print(f"[webcam] Client connected!", file=sys.stderr)
        await stream_webcam(websocket, show_gui=_show_gui)
        print(f"[webcam] Client disconnected", file=sys.stderr)

    async with websockets.serve(handler, "0.0.0.0", args.port):
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
