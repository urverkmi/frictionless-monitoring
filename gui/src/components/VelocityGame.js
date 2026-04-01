import React from 'react';
import useConstantVelocityGame from '../hooks/useConstantVelocityGame';
import SpeedSparkline from './SpeedSparkline';

const COLORS = {
  bg: '#0f172a',
  card: '#1e293b',
  border: '#334155',
  blue: '#3b82f6',
  green: '#10b981',
  yellow: '#facc15',
  red: '#ef4444',
  textPrimary: '#f1f5f9',
  textSecondary: '#94a3b8',
};

function scoreColor(score) {
  if (score >= 70) return COLORS.green;
  if (score >= 50) return COLORS.yellow;
  return COLORS.red;
}

function stabilityLabel(currentSpeed, meanSpeed) {
  if (meanSpeed === 0) return { text: 'Move!', color: COLORS.textSecondary };
  const deviation = Math.abs(currentSpeed - meanSpeed) / meanSpeed;
  if (deviation < 0.15) return { text: 'Steady', color: COLORS.green };
  if (deviation < 0.35) return { text: 'Drifting', color: COLORS.yellow };
  return { text: 'Unstable', color: COLORS.red };
}

// ── Idle Screen ──
function IdleView({ onStart }) {
  return (
    <div style={{ textAlign: 'center', padding: '60px 40px' }}>
      <h1 style={{ fontSize: 36, fontWeight: 700, marginBottom: 16, color: COLORS.textPrimary }}>
        Constant Velocity Challenge
      </h1>
      <p style={{ fontSize: 18, color: COLORS.textSecondary, maxWidth: 480, margin: '0 auto 40px' }}>
        Move the marker at a steady speed. Keep it as constant as you can for 5 seconds!
      </p>
      <button
        onClick={onStart}
        style={{
          padding: '18px 64px',
          fontSize: 24,
          fontWeight: 700,
          background: COLORS.green,
          color: '#fff',
          border: 'none',
          borderRadius: 12,
          cursor: 'pointer',
          transition: 'transform 0.1s',
        }}
        onMouseDown={e => e.currentTarget.style.transform = 'scale(0.96)'}
        onMouseUp={e => e.currentTarget.style.transform = 'scale(1)'}
      >
        START
      </button>
    </div>
  );
}

// ── Countdown Screen ──
function CountdownView({ countdown }) {
  const label = countdown > 0 ? countdown : 'GO!';
  return (
    <div style={{ textAlign: 'center', padding: '80px 40px' }}>
      <div style={{
        fontSize: 120,
        fontWeight: 800,
        color: countdown > 0 ? COLORS.textPrimary : COLORS.green,
        lineHeight: 1,
      }}>
        {label}
      </div>
      <p style={{ fontSize: 18, color: COLORS.textSecondary, marginTop: 24 }}>
        Get ready to move the marker...
      </p>
    </div>
  );
}

// ── Playing Screen ──
function PlayingView({ timeRemaining, gameDuration, currentSpeed, speedHistory }) {
  const progress = timeRemaining / gameDuration;
  const samples = speedHistory.slice();
  const runningMean = samples.length > 0
    ? samples.reduce((a, b) => a + b, 0) / samples.length
    : 0;
  const stability = stabilityLabel(currentSpeed, runningMean);

  return (
    <div style={{ padding: '30px 40px' }}>
      {/* Timer bar */}
      <div style={{ marginBottom: 24 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 6 }}>
          <span style={{ color: COLORS.textSecondary, fontSize: 14 }}>Time remaining</span>
          <span style={{ color: COLORS.textPrimary, fontSize: 14, fontWeight: 600 }}>
            {timeRemaining.toFixed(1)}s
          </span>
        </div>
        <div style={{
          height: 8,
          background: COLORS.card,
          borderRadius: 4,
          overflow: 'hidden',
        }}>
          <div style={{
            height: '100%',
            width: `${progress * 100}%`,
            background: progress > 0.3 ? COLORS.blue : COLORS.red,
            borderRadius: 4,
            transition: 'width 0.1s linear',
          }} />
        </div>
      </div>

      {/* Speed readout */}
      <div style={{
        textAlign: 'center',
        background: COLORS.card,
        borderRadius: 12,
        padding: '24px 16px',
        marginBottom: 24,
      }}>
        <div style={{ fontSize: 14, color: COLORS.textSecondary, marginBottom: 4 }}>
          Current Speed
        </div>
        <div style={{ fontSize: 56, fontWeight: 800, color: COLORS.textPrimary, lineHeight: 1 }}>
          {currentSpeed.toFixed(1)}
        </div>
        <div style={{ fontSize: 14, color: COLORS.textSecondary, marginTop: 4 }}>
          units/s
        </div>
        <div style={{
          marginTop: 12,
          fontSize: 18,
          fontWeight: 600,
          color: stability.color,
        }}>
          {stability.text}
        </div>
      </div>

      {/* Sparkline */}
      <div style={{ display: 'flex', justifyContent: 'center' }}>
        <SpeedSparkline samples={samples} width={460} height={120} />
      </div>
      <div style={{
        textAlign: 'center',
        marginTop: 6,
        fontSize: 12,
        color: COLORS.textSecondary,
      }}>
        Speed over time &mdash; yellow dashed = average
      </div>
    </div>
  );
}

// ── Result Screen ──
function ResultView({ score, passed, meanSpeed, variationPct, speedHistory, onPlayAgain }) {
  return (
    <div style={{ textAlign: 'center', padding: '40px 40px' }}>
      {/* Score */}
      <div style={{
        fontSize: 80,
        fontWeight: 800,
        color: scoreColor(score),
        lineHeight: 1,
      }}>
        {score}
      </div>
      <div style={{ fontSize: 16, color: COLORS.textSecondary, marginTop: 4 }}>
        out of 100
      </div>

      {/* Badge */}
      <div style={{
        display: 'inline-block',
        marginTop: 16,
        padding: '8px 28px',
        borderRadius: 20,
        fontSize: 20,
        fontWeight: 700,
        color: '#fff',
        background: passed ? COLORS.green : COLORS.red,
      }}>
        {passed ? 'PASSED!' : 'TRY AGAIN'}
      </div>

      {/* Stats */}
      <div style={{
        display: 'flex',
        justifyContent: 'center',
        gap: 32,
        marginTop: 28,
        marginBottom: 24,
      }}>
        <div>
          <div style={{ fontSize: 24, fontWeight: 700, color: COLORS.textPrimary }}>
            {meanSpeed.toFixed(1)}
          </div>
          <div style={{ fontSize: 13, color: COLORS.textSecondary }}>Avg Speed</div>
        </div>
        <div>
          <div style={{ fontSize: 24, fontWeight: 700, color: COLORS.textPrimary }}>
            {variationPct}%
          </div>
          <div style={{ fontSize: 13, color: COLORS.textSecondary }}>Variation</div>
        </div>
        <div>
          <div style={{ fontSize: 24, fontWeight: 700, color: COLORS.textPrimary }}>
            {speedHistory.length}
          </div>
          <div style={{ fontSize: 13, color: COLORS.textSecondary }}>Samples</div>
        </div>
      </div>

      {/* Final sparkline */}
      <div style={{ display: 'flex', justifyContent: 'center', marginBottom: 32 }}>
        <SpeedSparkline samples={speedHistory} width={460} height={100} />
      </div>

      {/* Play again */}
      <button
        onClick={onPlayAgain}
        style={{
          padding: '16px 56px',
          fontSize: 22,
          fontWeight: 700,
          background: COLORS.blue,
          color: '#fff',
          border: 'none',
          borderRadius: 12,
          cursor: 'pointer',
        }}
        onMouseDown={e => e.currentTarget.style.transform = 'scale(0.96)'}
        onMouseUp={e => e.currentTarget.style.transform = 'scale(1)'}
      >
        Play Again
      </button>
    </div>
  );
}

// ── Main Component ──
export default function VelocityGame({ data }) {
  const game = useConstantVelocityGame(data);

  return (
    <div style={{
      flex: 1,
      display: 'flex',
      flexDirection: 'column',
      justifyContent: 'center',
      alignItems: 'center',
      background: COLORS.bg,
      minHeight: '100vh',
    }}>
      <div style={{
        width: '100%',
        maxWidth: 540,
        background: COLORS.card,
        borderRadius: 16,
        border: `1px solid ${COLORS.border}`,
        overflow: 'hidden',
      }}>
        {game.gamePhase === 'idle' && (
          <IdleView onStart={game.startGame} />
        )}
        {game.gamePhase === 'countdown' && (
          <CountdownView countdown={game.countdown} />
        )}
        {game.gamePhase === 'playing' && (
          <PlayingView
            timeRemaining={game.timeRemaining}
            gameDuration={game.gameDuration}
            currentSpeed={game.currentSpeed}
            speedHistory={game.speedHistory}
          />
        )}
        {game.gamePhase === 'result' && (
          <ResultView
            score={game.score}
            passed={game.passed}
            meanSpeed={game.meanSpeed}
            variationPct={game.variationPct}
            speedHistory={game.speedHistory}
            onPlayAgain={game.resetGame}
          />
        )}
      </div>
    </div>
  );
}
