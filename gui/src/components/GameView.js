import React, { useState, useEffect, useRef, useCallback } from 'react';
import GameCanvas from './GameCanvas';
import GameSidebar from './GameSidebar';
import { GAME_CONFIG } from '../gameConfig';
import { connectToDataStream, disconnectFromDataStream } from '../services/dataService';

function speedMag(v) {
  return Math.sqrt((v?.x || 0) ** 2 + (v?.y || 0) ** 2);
}

function dist(a, b) {
  return Math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2);
}

// Map camera-frame coords to displayed world-frame using the knobs in
// gameConfig. Rotate -> mirror -> scale -> offset.
function applyViewTransform(p) {
  if (!p) return p;
  const rad = (GAME_CONFIG.viewRotationDeg * Math.PI) / 180;
  const c = Math.cos(rad);
  const s = Math.sin(rad);
  let x = p.x * c - p.y * s;
  let y = p.x * s + p.y * c;
  if (GAME_CONFIG.viewMirrorX) x = -x;
  if (GAME_CONFIG.viewMirrorY) y = -y;
  x *= GAME_CONFIG.viewScale;
  y *= GAME_CONFIG.viewScale;
  x += GAME_CONFIG.viewOffsetX;
  y += GAME_CONFIG.viewOffsetY;
  return { x, y };
}

// Linear-velocity vector also needs the rotation+mirror+scale (no offset).
function applyViewTransformVelocity(v) {
  if (!v) return v;
  const rad = (GAME_CONFIG.viewRotationDeg * Math.PI) / 180;
  const c = Math.cos(rad);
  const s = Math.sin(rad);
  let x = v.x * c - v.y * s;
  let y = v.x * s + v.y * c;
  if (GAME_CONFIG.viewMirrorX) x = -x;
  if (GAME_CONFIG.viewMirrorY) y = -y;
  x *= GAME_CONFIG.viewScale;
  y *= GAME_CONFIG.viewScale;
  return { x, y };
}

function projectedScoreFromPeak(peakSpeed) {
  const ratio = peakSpeed / GAME_CONFIG.maxAllowedSpeed;
  const raw = 100 * (1 - ratio);
  return Math.round(Math.max(0, Math.min(100, raw)));
}

function spawnTarget(puckPosition) {
  const halfW = GAME_CONFIG.tableHalfWidth - GAME_CONFIG.targetMargin - GAME_CONFIG.targetRadius;
  const halfH = GAME_CONFIG.tableHalfHeight - GAME_CONFIG.targetMargin - GAME_CONFIG.targetRadius;
  let candidate = null;
  for (let i = 0; i < 50; i += 1) {
    const x = (Math.random() * 2 - 1) * halfW;
    const y = (Math.random() * 2 - 1) * halfH;
    candidate = { x, y, radius: GAME_CONFIG.targetRadius };
    if (!puckPosition || dist(candidate, puckPosition) >= GAME_CONFIG.minSpawnDistanceFromPuck) {
      return candidate;
    }
  }
  return candidate;
}

function GameView() {
  const [connectionStatus, setConnectionStatus] = useState('disconnected');
  const [puckPosition, setPuckPosition] = useState(null);
  const [target, setTarget] = useState(null);
  const [roundActive, setRoundActive] = useState(false);
  const [outcome, setOutcome] = useState(null);            // 'hit' | 'timeout' | null
  const [finalScore, setFinalScore] = useState(0);
  const [currentSpeed, setCurrentSpeed] = useState(0);
  const [peakSpeed, setPeakSpeed] = useState(0);
  const [timeRemaining, setTimeRemaining] = useState(GAME_CONFIG.roundDurationSeconds);
  const [hitFlashUntil, setHitFlashUntil] = useState(0);

  // Refs mirror state for fresh reads inside the data callback / interval.
  const roundActiveRef    = useRef(false);
  const targetRef         = useRef(null);
  const peakSpeedRef      = useRef(0);
  const puckPositionRef   = useRef(null);
  const roundStartedAtRef = useRef(0);

  // ---- Round lifecycle ----
  const endRound = useCallback((endOutcome, score) => {
    roundActiveRef.current = false;
    setRoundActive(false);
    setOutcome(endOutcome);
    setFinalScore(score);
    setTarget(null);
    targetRef.current = null;
    if (endOutcome === 'hit') {
      setHitFlashUntil(Date.now() + GAME_CONFIG.hitFlashMs);
    }
  }, []);

  const startRound = useCallback(() => {
    const newTarget = spawnTarget(puckPositionRef.current);
    targetRef.current = newTarget;
    setTarget(newTarget);
    peakSpeedRef.current = 0;
    setPeakSpeed(0);
    setCurrentSpeed(0);
    setOutcome(null);
    setFinalScore(0);
    setTimeRemaining(GAME_CONFIG.roundDurationSeconds);
    roundStartedAtRef.current = Date.now();
    roundActiveRef.current = true;
    setRoundActive(true);
  }, []);

  const reset = useCallback(() => {
    roundActiveRef.current = false;
    targetRef.current = null;
    peakSpeedRef.current = 0;
    setRoundActive(false);
    setTarget(null);
    setOutcome(null);
    setFinalScore(0);
    setPeakSpeed(0);
    setCurrentSpeed(0);
    setTimeRemaining(GAME_CONFIG.roundDurationSeconds);
    setHitFlashUntil(0);
  }, []);

  // ---- WebSocket subscription ----
  useEffect(() => {
    const handleData = (data) => {
      // The C++ detector emits `endMassPosition` in camera-frame metres;
      // in single-tag mode this IS the puck. Apply the view transform so
      // physical pushes match canvas movement.
      const puck = applyViewTransform(data.endMassPosition);
      puckPositionRef.current = puck;
      setPuckPosition(puck);

      const speed = speedMag(applyViewTransformVelocity(data.linearSpeed));
      setCurrentSpeed(speed);

      if (roundActiveRef.current && targetRef.current) {
        if (speed > peakSpeedRef.current) {
          peakSpeedRef.current = speed;
          setPeakSpeed(speed);
        }
        // Hit detection
        if (puck && dist(puck, targetRef.current) < targetRef.current.radius) {
          endRound('hit', projectedScoreFromPeak(peakSpeedRef.current));
        }
      }
    };

    connectToDataStream(handleData, setConnectionStatus);
    return () => disconnectFromDataStream();
  }, [endRound]);

  // ---- Countdown timer ----
  useEffect(() => {
    if (!roundActive) return undefined;
    const tickMs = 100;
    const id = setInterval(() => {
      const elapsed = (Date.now() - roundStartedAtRef.current) / 1000;
      const remaining = GAME_CONFIG.roundDurationSeconds - elapsed;
      setTimeRemaining(remaining);
      if (remaining <= 0) {
        endRound('timeout', 0);
      }
    }, tickMs);
    return () => clearInterval(id);
  }, [roundActive, endRound]);

  const projectedScore = projectedScoreFromPeak(peakSpeed);

  return (
    <div style={{
      display: 'flex',
      height: '100vh',
      backgroundColor: '#0f172a',
      color: '#e2e8f0',
    }}>
      <GameCanvas
        puckPosition={puckPosition}
        target={target}
        hitFlashUntil={hitFlashUntil}
      />
      <GameSidebar
        connectionStatus={connectionStatus}
        roundActive={roundActive}
        outcome={outcome}
        finalScore={finalScore}
        projectedScore={projectedScore}
        currentSpeed={currentSpeed}
        peakSpeed={peakSpeed}
        timeRemaining={timeRemaining}
        onStartRound={startRound}
        onReset={reset}
      />
    </div>
  );
}

export default GameView;
