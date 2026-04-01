"""
Adapter: pipe C++ AprilTag detector stdout into UDP for cpp_stream_bridge.

The C++ detector (vision/main.cpp) writes JSON lines to stdout:
    {"ts":<ns>,"frame":<N>,"tag0":{"x":..,"y":..,"yaw":..,"conf":..,"visible":..},"tag1":{...}}

cpp_stream_bridge.py expects UDP datagrams on port 9001 with:
    {"timestamp":..,"frame_id":..,"camera_id":"cam0",
     "satellite_position":{"x":..,"y":..},"end_mass_position":{"x":..,"y":..},
     "orbital_angular_position":..,"tracking_confidence":..}

Usage:
    ./apriltag_demo | python3 src/stdout_to_udp.py
    ./apriltag_demo | python3 src/stdout_to_udp.py --port 9001
"""

import json
import math
import socket
import sys
import argparse


def translate(line: str) -> str | None:
    try:
        raw = json.loads(line)
    except json.JSONDecodeError:
        return None

    tag0 = raw.get("tag0", {})
    tag1 = raw.get("tag1", {})

    if not tag0.get("visible") or not tag1.get("visible"):
        return None

    sat = {"x": tag0["x"], "y": tag0["y"]}
    end = {"x": tag1["x"], "y": tag1["y"]}

    dx = end["x"] - sat["x"]
    dy = end["y"] - sat["y"]
    angle = (math.atan2(dy, dx) + 2 * math.pi) % (2 * math.pi)

    conf = (tag0["conf"] + tag1["conf"]) / 2.0

    payload = {
        "timestamp": raw["ts"] / 1e9,
        "frame_id": raw["frame"],
        "camera_id": "cam0",
        "satellite_position": sat,
        "end_mass_position": end,
        "orbital_angular_position": angle,
        "tracking_confidence": conf,
    }
    return json.dumps(payload)


def main():
    parser = argparse.ArgumentParser(description="C++ stdout -> UDP adapter")
    parser.add_argument("--port", type=int, default=9001)
    parser.add_argument("--host", default="127.0.0.1")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (args.host, args.port)
    count = 0

    print(f"[stdout_to_udp] Forwarding stdin -> UDP {args.host}:{args.port}", file=sys.stderr)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        msg = translate(line)
        if msg is not None:
            sock.sendto(msg.encode("utf-8"), dest)
            count += 1
            if count % 100 == 0:
                print(f"[stdout_to_udp] Forwarded {count} frames", file=sys.stderr)


if __name__ == "__main__":
    main()
