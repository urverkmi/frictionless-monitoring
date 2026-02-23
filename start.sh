#!/bin/bash
# ─────────────────────────────────────────────
# Plasma Brake Monitor — One-click launcher
# Run from repo root: ./start.sh
# Run with the flag:  ./start.sh --sim for simulator mode
# ─────────────────────────────────────────────

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$REPO_DIR/logs"
mkdir -p "$LOG_DIR"

# ── Kill any stale processes left from a previous run ──
pkill -f "pcb_simulator.py"  2>/dev/null || true
pkill -f "controller_app.py" 2>/dev/null || true
pkill -f "FakeDataStream.py" 2>/dev/null || true
pkill -f "tail.*$LOG_DIR"    2>/dev/null || true
fuser -k 5000/tcp 2>/dev/null || true
fuser -k 3000/tcp 2>/dev/null || true
fuser -k 8080/tcp 2>/dev/null || true
sleep 1

# Clear old logs
> "$LOG_DIR/flask.log"
> "$LOG_DIR/react.log"
> "$LOG_DIR/simulator.log"
> "$LOG_DIR/fakedata.log"

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
  kill "${SIM_PID:-}" "$FLASK_PID" "${FAKEDATA_PID:-}" "$REACT_PID" "$TAIL_PID" 2>/dev/null
  wait 2>/dev/null
  exit 0
}
trap cleanup SIGINT SIGTERM

# ── 1. Python dependencies ───────────────────
echo "[1/3] Installing Python dependencies..."
pip3 install -r "$REPO_DIR/requirements.txt" --quiet --break-system-packages

# ── 2. Start simulator first so virtual ports exist ──
if [ "$SIM_MODE" = true ]; then
  echo "[2/3] Starting PCB simulator..."
  (cd "$REPO_DIR/src" && python3 -u pcb_simulator.py >> "$LOG_DIR/simulator.log" 2>&1) &
  SIM_PID=$!
  sleep 3
fi

# ── 3. Start Flask backend ───────────────────
echo "[3/3] Starting Flask backend (port 5000)..."
(cd "$REPO_DIR/src" && python3 controller_app.py >> "$LOG_DIR/flask.log" 2>&1) &
FLASK_PID=$!
sleep 2

# ── 4. Start FakeDataStream WebSocket server ─
echo "      Starting data stream (port 8080)..."
(cd "$REPO_DIR/src" && python3 -u FakeDataStream.py >> "$LOG_DIR/fakedata.log" 2>&1) &
FAKEDATA_PID=$!
sleep 1

# ── 5. Start React frontend ──────────────────
echo "      Starting React frontend (port 3000)..."
if [ ! -d "$REPO_DIR/gui/node_modules" ]; then
  echo "      node_modules not found — running npm install..."
  (cd "$REPO_DIR/gui" && npm install)
fi
(cd "$REPO_DIR/gui" && npm start >> "$LOG_DIR/react.log" 2>&1) &
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
echo "  ✓ Backend:   http://localhost:5000"
echo "  ✓ Frontend:  http://localhost:3000"
echo ""
echo "  Full logs (other terminal):"
echo "    tail -f logs/flask.log"
echo "    tail -f logs/fakedata.log"
echo "    tail -f logs/react.log"
echo "  Press Ctrl+C to stop everything."
echo "════════════════════════════════════════"
echo ""
echo "── Simulator live output ────────────────"
echo "  (start/stop commands from the GUI appear here)"
echo ""

# -n 0: only show lines written after this point (no startup replay)
tail -n 0 -f "$LOG_DIR/simulator.log" &
TAIL_PID=$!

wait "$FLASK_PID" "$REACT_PID"
