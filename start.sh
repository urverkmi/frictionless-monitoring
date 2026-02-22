#!/bin/bash
# ─────────────────────────────────────────────
# Plasma Brake Monitor — One-click launcher
# Run from repo root: ./start.sh
# ─────────────────────────────────────────────

set -e  # Exit immediately if anything fails

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "════════════════════════════════════════"
echo "  Aurora Plasma Brake Monitor"
echo "════════════════════════════════════════"

# ── 1. Python dependencies ──────────────────
echo "[1/3] Installing Python dependencies..."
pip3 install -r "$REPO_DIR/requirements.txt" --quiet --break-system-packages

# ── 2. Start Flask backend ──────────────────
echo "[2/3] Starting Flask backend (port 5000)..."
cd "$REPO_DIR/src"
python3 controller_app.py &
FLASK_PID=$!
echo "      Flask PID: $FLASK_PID"

# Give Flask a moment to bind to the port
sleep 2

# ── 3. Start React frontend ─────────────────
echo "[3/3] Starting React frontend (port 3000)..."
cd "$REPO_DIR/gui"

# Install npm packages only if node_modules is missing
if [ ! -d "node_modules" ]; then
  echo "      node_modules not found — running npm install..."
  npm install
fi

npm start &
REACT_PID=$!
echo "      React PID: $REACT_PID"

echo ""
echo "════════════════════════════════════════"
echo "  ✓ Backend:  http://localhost:5000"
echo "  ✓ Frontend: http://localhost:3000"
echo "  Press Ctrl+C to stop everything."
echo "════════════════════════════════════════"

# ── Cleanup: kill both processes on Ctrl+C ──
trap "echo ''; echo 'Shutting down...'; kill $FLASK_PID $REACT_PID 2>/dev/null; exit 0" SIGINT SIGTERM

# Wait for both to exit
wait $FLASK_PID $REACT_PID