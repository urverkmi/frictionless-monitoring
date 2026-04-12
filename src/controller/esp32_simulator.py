#!/usr/bin/env python3
"""
ESP32 Spinup Simulator
Mimics the ESP32 UART responses for testing without hardware.
Run this BEFORE starting controller_app.py.
"""

import os
import pty
import tty
import time
import platform
import sys

# Simulated timing (seconds)
SOLENOID_DELAY   = 0.4
RAMP_UP_TIME     = 2.0
SPIN_TIME        = 1.0
DETACH_DELAY     = 0.5
RAMP_DOWN_TIME   = 2.0

COMMANDS = {"STOP": "OK", "PING": "PONG"}


def create_virtual_ports():
    """
    Create a linked virtual serial port pair:
      /tmp/ttyVIRT_ESP0  ←→  /tmp/ttyVIRT_ESP1
    controller_app.py connects to ttyVIRT_ESP0, simulator uses the other end.

    Uses socat on Linux, pty on macOS.
    Returns (cleanup_callable, read_fn, write_fn).
    """
    if platform.system() == "Linux":
        import subprocess, serial
        print("[ESP32-SIM] Creating virtual serial port pair via socat...")
        proc = subprocess.Popen([
            'socat',
            'PTY,link=/tmp/ttyVIRT_ESP0,raw,echo=0',
            'PTY,link=/tmp/ttyVIRT_ESP1,raw,echo=0'
        ])
        time.sleep(1)
        ser = serial.Serial('/tmp/ttyVIRT_ESP1', 115200, timeout=0.5)
        print("[ESP32-SIM] Virtual ports ready:")
        print("       controller_app.py  → /tmp/ttyVIRT_ESP0")
        print("       simulator          → /tmp/ttyVIRT_ESP1\n")

        def cleanup():
            ser.close()
            proc.terminate()

        return cleanup, ser.readline, lambda data: ser.write(data)
    else:
        # macOS: use pty pair
        print("[ESP32-SIM] Creating virtual serial port pair via pty...")
        master_fd, slave_fd = pty.openpty()
        tty.setraw(master_fd)
        tty.setraw(slave_fd)
        slave_name = os.ttyname(slave_fd)

        for path in ['/tmp/ttyVIRT_ESP0']:
            if os.path.islink(path) or os.path.exists(path):
                os.remove(path)
        os.symlink(slave_name, '/tmp/ttyVIRT_ESP0')

        print(f"[ESP32-SIM] Virtual ports ready:")
        print(f"       controller_app.py  → /tmp/ttyVIRT_ESP0 ({slave_name})")
        print(f"       simulator          → master fd\n")

        buf = b''

        def read_line():
            nonlocal buf
            while True:
                try:
                    chunk = os.read(master_fd, 256)
                except OSError:
                    return b''
                if not chunk:
                    return b''
                buf += chunk
                if b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    return line + b'\n'

        def write_data(data):
            os.write(master_fd, data)

        def cleanup():
            os.close(master_fd)
            os.close(slave_fd)
            if os.path.islink('/tmp/ttyVIRT_ESP0'):
                os.remove('/tmp/ttyVIRT_ESP0')

        return cleanup, read_line, write_data


def run_simulator(read_line, write_fn):
    """Main simulator loop."""
    print("[ESP32-SIM] Ready. Waiting for commands...\n")
    sys.stdout.flush()

    while True:
        raw = read_line()
        if not raw:
            continue

        line = raw.decode(errors='replace').strip()
        if not line:
            continue

        print(f"  [ESP32-SIM] << {line}")
        sys.stdout.flush()

        if line.startswith("PWM:"):
            try:
                pwm_val = int(line.split(":")[1])
            except (ValueError, IndexError):
                pwm_val = 200
            simulate_spinup_cycle(write_fn, pwm_val)

        elif line in COMMANDS:
            response = COMMANDS[line]
            print(f"  [ESP32-SIM] >> {response}")
            sys.stdout.flush()
            write_fn((response + "\n").encode())

        else:
            print(f"  [ESP32-SIM] ?? Unknown command: {line}")
            sys.stdout.flush()
            write_fn(b"ERR\n")


def simulate_spinup_cycle(write_fn, pwm_val):
    """Simulate the ESP32 spinup cycle with realistic timing."""
    print(f"  [ESP32-SIM] Spinup cycle started (targetPWM={pwm_val})")

    print(f"  [ESP32-SIM] Solenoid ON...")
    sys.stdout.flush()
    time.sleep(SOLENOID_DELAY)

    print(f"  [ESP32-SIM] Motor ramping up to {pwm_val}...")
    sys.stdout.flush()
    time.sleep(RAMP_UP_TIME)

    print(f"  [ESP32-SIM] Spinning at target speed...")
    sys.stdout.flush()
    time.sleep(SPIN_TIME)

    # Solenoid releases — notify controller
    print(f"  [ESP32-SIM] Solenoid OFF")
    print(f"  [ESP32-SIM] >> DETACHED")
    sys.stdout.flush()
    write_fn(b"DETACHED\n")

    time.sleep(DETACH_DELAY)

    print(f"  [ESP32-SIM] Motor ramping down...")
    sys.stdout.flush()
    time.sleep(RAMP_DOWN_TIME)

    print(f"  [ESP32-SIM] Motor stopped")
    print(f"  [ESP32-SIM] >> OK")
    print(f"  [ESP32-SIM] Cycle complete.\n")
    sys.stdout.flush()
    write_fn(b"OK\n")


if __name__ == '__main__':
    cleanup, read_fn, write_fn = create_virtual_ports()
    try:
        run_simulator(read_fn, write_fn)
    except KeyboardInterrupt:
        print("\n[ESP32-SIM] Shutting down simulator.")
    finally:
        cleanup()
