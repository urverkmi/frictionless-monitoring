#!/bin/bash
# ─────────────────────────────────────────────
# Plasma Brake Monitor — One-click launcher
# Run from repo root: ./start.sh
# Run with the flag:  ./start.sh --sim for simulator mode
# ─────────────────────────────────────────────

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$REPO_DIR/logs"
mkdir -p "$LOG_DIR"

# On Raspberry Pi over SSH, GUI/camera preview often needs explicit X11 env.
# Keep this Linux-only so local macOS runs are unaffected.
if [[ "$(uname -s)" == "Linux" ]]; then
  : "${DISPLAY:=:0}"
  if [[ -z "${XAUTHORITY:-}" && -f "/home/auroracv/.Xauthority" ]]; then
    export XAUTHORITY="/home/auroracv/.Xauthority"
  fi
  export DISPLAY
fi

# Prefer project virtualenv when available.
if [[ -x "$REPO_DIR/venv/bin/python3" ]]; then
  PYTHON_BIN="$REPO_DIR/venv/bin/python3"
  PIP_CMD=( "$PYTHON_BIN" -m pip )
else
  PYTHON_BIN="python3"
  PIP_CMD=( pip3 )
fi

# ── Kill any stale processes left from a previous run ──
pkill -f "pcb_simulator.py"  2>/dev/null || true
pkill -f "esp32_simulator.py" 2>/dev/null || true
pkill -f "controller_app.py" 2>/dev/null || true
pkill -f "cpp_stream_bridge.py" 2>/dev/null || true
pkill -f "tail.*$LOG_DIR"    2>/dev/null || true
# Kill processes on ports (cross-platform: fuser on Linux, lsof on macOS)
kill_port() {
  if command -v fuser &>/dev/null; then
    fuser -k "$1/tcp" 2>/dev/null || true
  else
    lsof -ti :"$1" 2>/dev/null | xargs kill -9 2>/dev/null || true
  fi
}
kill_port 5001
kill_port 3000
kill_port 8080
sleep 1

# Clear old logs
> "$LOG_DIR/flask.log"
> "$LOG_DIR/react.log"
> "$LOG_DIR/simulator.log"
> "$LOG_DIR/esp32_sim.log"
> "$LOG_DIR/bridge.log"
> "$LOG_DIR/vision.log"

echo "════════════════════════════════════════"
echo "  Aurora Plasma Brake Monitor"
echo "════════════════════════════════════════"

SIM_MODE=false
if [[ "$1" == "--sim" ]]; then
  SIM_MODE=true
  echo "  ⚠  SIMULATOR MODE"
fi

# ── Cleanup ───────────────────────────────────
cleanup() {
  echo ""
  echo "Shutting down..."
  kill "${SIM_PID:-}" "${ESP32_SIM_PID:-}" "$FLASK_PID" "${BRIDGE_PID:-}" "${VISION_PID:-}" "$REACT_PID" "$TAIL_PID" 2>/dev/null
  wait 2>/dev/null
  exit 0
}
trap cleanup SIGINT SIGTERM

# ── 1. Python dependencies ───────────────────
echo "[1/3] Installing Python dependencies..."
if "${PIP_CMD[@]}" install --help 2>/dev/null | grep -q -- "--break-system-packages"; then
  "${PIP_CMD[@]}" install -r "$REPO_DIR/requirements.txt" --quiet --break-system-packages
else
  "${PIP_CMD[@]}" install -r "$REPO_DIR/requirements.txt" --quiet
fi

# ── 2. Start simulator first so virtual ports exist ──
if [ "$SIM_MODE" = true ]; then
  echo "[2/3] Starting PCB simulator..."
  (cd "$REPO_DIR/src/controller" && "$PYTHON_BIN" -u pcb_simulator.py >> "$LOG_DIR/simulator.log" 2>&1) &
  SIM_PID=$!
  sleep 3

  echo "      Starting ESP32 simulator..."
  (cd "$REPO_DIR/src/controller" && "$PYTHON_BIN" -u esp32_simulator.py >> "$LOG_DIR/esp32_sim.log" 2>&1) &
  ESP32_SIM_PID=$!
  sleep 2
fi

# ── 3. Start Flask backend ───────────────────
echo "[3/3] Starting Flask backend (port 5001)..."
(cd "$REPO_DIR/src/controller" && "$PYTHON_BIN" -u controller_app.py >> "$LOG_DIR/flask.log" 2>&1) &
FLASK_PID=$!
sleep 2

# ── 4. Detector bridge: UDP (C++ vision) -> WebSocket (GUI) ─
echo "      Starting cpp_stream_bridge (UDP 9001 -> ws 8080)..."
("$PYTHON_BIN" -u "$REPO_DIR/src/camera/cpp_stream_bridge.py" >> "$LOG_DIR/bridge.log" 2>&1) &
BRIDGE_PID=$!
sleep 1

# ── 5. AprilTag vision (optional; Pi / built binary) ─
# Game mode: set GAME_MODE=1 (and optionally PUCK_TAG_ID, default 1) to launch
# the detector with --single-tag <id>, which makes it emit whenever the puck is
# visible (no second tag required). Default invocation is unchanged.
VISION_BIN="$REPO_DIR/vision/build/apriltag_demo"
VISION_ARGS=( --no-vis )
if [[ "${GAME_MODE:-}" == "1" ]]; then
  PUCK_TAG_ID="${PUCK_TAG_ID:-1}"
  VISION_ARGS+=( --single-tag "$PUCK_TAG_ID" )
fi
if [[ -x "$VISION_BIN" ]]; then
  echo "      Starting AprilTag vision (${VISION_ARGS[*]})..."
  ("$VISION_BIN" "${VISION_ARGS[@]}" >> "$LOG_DIR/vision.log" 2>&1) &
  VISION_PID=$!
  sleep 1
else
  echo "      (Vision binary not found: $VISION_BIN — build vision/ or run it on the Pi)"
fi

# ── 6. Start React frontend ──────────────────
echo "      Starting React frontend (port 3000)..."
if [ ! -d "$REPO_DIR/gui/node_modules" ]; then
  echo "      node_modules not found — running npm install..."
  (cd "$REPO_DIR/gui" && npm install)
fi
(cd "$REPO_DIR/gui" && BROWSER=none npm start >> "$LOG_DIR/react.log" 2>&1) &
REACT_PID=$!

# Brief pause then wipe the startup noise
sleep 3
clear

echo "════════════════════════════════════════"
echo "  Aurora Plasma Brake Monitor — RUNNING"
if [ "$SIM_MODE" = true ]; then
echo "  ⚠  SIMULATOR MODE"
fi
echo "════════════════════════════════════════"
echo "  ✓ Backend:   http://localhost:5001"
echo "  ✓ Frontend:  http://localhost:3000"
echo "  ✓ Data:      ws://localhost:8080 (from C++ via cpp_stream_bridge)"
echo ""
echo "  Full logs (other terminal):"
echo "    tail -f logs/flask.log"
echo "    tail -f logs/bridge.log"
echo "    tail -f logs/vision.log"
echo "    tail -f logs/react.log"
echo "  Press Ctrl+C to stop everything."
echo "════════════════════════════════════════"
echo ""
echo "── Simulator live output ────────────────"
echo "  (start/stop commands from the GUI appear here)"
echo ""

# -n 0: only show lines written after this point (no startup replay)
tail -n 0 -f "$LOG_DIR/simulator.log" "$LOG_DIR/esp32_sim.log" &
TAIL_PID=$!

wait "$FLASK_PID" "$REACT_PID"
