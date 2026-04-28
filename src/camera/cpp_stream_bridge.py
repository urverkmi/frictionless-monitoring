"""
Bridge C++ detector output to Python kinematics and GUI stream.

Flow:
1) Receive Detector Contract JSON over UDP (C++ -> Python)
2) Map to internal position_data format for MemoryManager
3) Run KinematicsCalculator
4) Broadcast GUI payload over WebSocket (port 8080 by default)

Run from repo root:
  python3 src/camera/cpp_stream_bridge.py
"""

import argparse
import asyncio
import json
import math
import sys
from pathlib import Path
from typing import Any, Dict, Optional, Set

# Ensure camera package imports resolve when run as a script.
_src_dir = Path(__file__).resolve().parent
if str(_src_dir) not in sys.path:
    sys.path.insert(0, str(_src_dir))

from data_structures import Vector2D
from kinematics import KinematicsCalculator
from memory import MemoryManager


class UdpReceiverProtocol(asyncio.DatagramProtocol):
    """Collect UDP datagrams into an asyncio queue."""

    def __init__(self, queue: asyncio.Queue):
        self.queue = queue

    def datagram_received(self, data: bytes, addr):
        try:
            message = data.decode("utf-8").strip()
            if message:
                self.queue.put_nowait(message)
        except Exception:
            # Ignore malformed datagrams; parser handles JSON errors separately.
            pass


class CppStreamBridge:
    def __init__(
        self,
        ws_port: int,
        udp_port: int,
        buffer_size: int = 100,
        main_size: float = 20.0,
        end_mass_radius: float = 12.0,
    ):
        self.ws_port = ws_port
        self.udp_port = udp_port
        self.udp_queue: Optional[asyncio.Queue[str]] = None
        self.ws_clients: Set[Any] = set()

        self.memory = MemoryManager(buffer_size=buffer_size)
        self.kinematics = KinematicsCalculator(self.memory)
        self.main_size = main_size
        self.end_mass_radius = end_mass_radius

    @staticmethod
    def _validate_detector_payload(payload: Dict[str, Any]) -> bool:
        required = [
            "timestamp",
            "frame_id",
            "camera_id",
            "satellite_position",
            "end_mass_position",
            "orbital_angular_position",
            "tracking_confidence",
        ]
        return all(k in payload for k in required)

    def _to_position_data(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        sat = payload["satellite_position"]
        end = payload["end_mass_position"]

        sat_vec = Vector2D(float(sat["x"]), float(sat["y"]))
        end_vec = Vector2D(float(end["x"]), float(end["y"]))
        return {
            "timestamp": float(payload["timestamp"]),
            "frame_id": int(payload["frame_id"]),
            "satellite_position": sat_vec,
            "end_mass_position": end_vec,
            # Keep legacy "position" key as relative vector for compatibility.
            "position": Vector2D(x=end_vec.x - sat_vec.x, y=end_vec.y - sat_vec.y),
            "orbital_angular_position": float(payload["orbital_angular_position"]),
            "angular_position": float(payload["orbital_angular_position"]),
            "tracking_confidence": float(payload["tracking_confidence"]),
            "camera_id": payload["camera_id"],
        }

    def _frame_to_gui_payload(self, frame_data) -> Dict[str, Any]:
        angle = frame_data.angular_position
        omega = frame_data.angular_velocity
        return {
            "timestamp": int(frame_data.timestamp * 1000),
            "satellitePosition": frame_data.satellite_position.to_dict(),
            # Keep compatibility with components still reading mainPosition.
            "mainPosition": frame_data.satellite_position.to_dict(),
            "endMassPosition": frame_data.end_mass_position.to_dict(),
            "linearSpeed": {
                "x": frame_data.velocity.x,
                "y": frame_data.velocity.y,
            },
            "angularSpeed": {
                "x": omega * math.cos(angle),
                "y": omega * math.sin(angle),
            },
            "tetherLength": frame_data.tether_length,
            "mainSize": self.main_size,
            "endMassRadius": self.end_mass_radius,
            "trackingConfidence": frame_data.tracking_confidence,
        }

    async def _broadcast(self, payload: Dict[str, Any]):
        if not self.ws_clients:
            return
        raw = json.dumps(payload)
        dead = []
        for ws in self.ws_clients:
            try:
                await ws.send(raw)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.ws_clients.discard(ws)

    async def process_udp_loop(self):
        if self.udp_queue is None:
            raise RuntimeError("UDP queue is not initialized")
        while True:
            message = await self.udp_queue.get()
            try:
                payload = json.loads(message)
            except json.JSONDecodeError:
                continue

            if not self._validate_detector_payload(payload):
                continue

            position_data = self._to_position_data(payload)
            self.memory.store_position_data(position_data)
            frame_data = self.kinematics.process_frame()
            if frame_data is None:
                continue

            gui_payload = self._frame_to_gui_payload(frame_data)
            await self._broadcast(gui_payload)

    async def ws_handler(self, websocket):
        self.ws_clients.add(websocket)
        try:
            await websocket.wait_closed()
        finally:
            self.ws_clients.discard(websocket)

    async def run(self):
        import websockets

        loop = asyncio.get_running_loop()
        self.udp_queue = asyncio.Queue()
        await loop.create_datagram_endpoint(
            lambda: UdpReceiverProtocol(self.udp_queue),
            local_addr=("0.0.0.0", self.udp_port),
        )

        print(f"[bridge] UDP listening on 0.0.0.0:{self.udp_port}")
        print(f"[bridge] WebSocket serving on ws://0.0.0.0:{self.ws_port}")

        asyncio.create_task(self.process_udp_loop())
        async with websockets.serve(self.ws_handler, "0.0.0.0", self.ws_port):
            await asyncio.Future()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="C++ detector stream bridge")
    parser.add_argument("--udp-port", type=int, default=9001)
    parser.add_argument("--ws-port", type=int, default=8080)
    parser.add_argument("--buffer-size", type=int, default=100)
    return parser.parse_args()


def main():
    args = parse_args()
    bridge = CppStreamBridge(
        ws_port=args.ws_port,
        udp_port=args.udp_port,
        buffer_size=args.buffer_size,
    )
    asyncio.run(bridge.run())


if __name__ == "__main__":
    main()
