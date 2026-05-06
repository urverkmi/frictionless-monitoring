import React from 'react';
import { GAME_CONFIG } from '../gameConfig';

const cardStyle = {
  backgroundColor: '#1e293b',
  padding: '1rem',
  borderRadius: '8px',
  marginBottom: '1rem',
};

const labelStyle = {
  fontSize: '0.75rem',
  color: '#94a3b8',
  textTransform: 'uppercase',
  letterSpacing: '0.05em',
  marginBottom: '0.25rem',
};

const valueStyle = {
  fontFamily: 'monospace',
  fontSize: '1.5rem',
  fontWeight: 'bold',
};

function statusColor(status) {
  switch (status) {
    case 'connected':  return '#10b981';
    case 'connecting': return '#f59e0b';
    default:           return '#ef4444';
  }
}

function GameSidebar({
  connectionStatus,
  roundActive,
  outcome,
  finalScore,
  projectedScore,
  currentSpeed,
  peakSpeed,
  timeRemaining,
  onStartRound,
  onReset,
}) {
  return (
    <div style={{
      width: 400,
      padding: '2rem',
      overflowY: 'auto',
      backgroundColor: '#0f172a',
      color: '#e2e8f0',
    }}>
      {/* Connection status */}
      <div style={{
        display: 'flex',
        alignItems: 'center',
        gap: '0.5rem',
        marginBottom: '1.5rem',
        padding: '1rem',
        backgroundColor: '#1e293b',
        borderRadius: 8,
      }}>
        <div style={{
          width: 12,
          height: 12,
          borderRadius: '50%',
          backgroundColor: statusColor(connectionStatus),
          animation: 'pulse 2s cubic-bezier(0.4, 0, 0.6, 1) infinite',
        }} />
        <span style={{ textTransform: 'capitalize', fontSize: '0.875rem' }}>
          {connectionStatus}
        </span>
      </div>

      {/* Title */}
      <h2 style={{ margin: '0 0 1.5rem 0', fontSize: '1.5rem' }}>
        De-orbiting Snoopy
      </h2>

      {/* Round state */}
      <div style={cardStyle}>
        <div style={labelStyle}>Round</div>
        <div style={{
          ...valueStyle,
          color: roundActive ? '#22c55e' : '#64748b',
        }}>
          {roundActive ? 'IN PROGRESS' : 'IDLE'}
        </div>
      </div>

      {/* Live numbers while round is active */}
      {roundActive && (
        <>
          <div style={cardStyle}>
            <div style={labelStyle}>Time remaining</div>
            <div style={{ ...valueStyle, color: '#3b82f6' }}>
              {Math.max(0, Math.ceil(timeRemaining))}s
            </div>
          </div>
          <div style={cardStyle}>
            <div style={labelStyle}>Current speed</div>
            <div style={valueStyle}>
              {currentSpeed.toFixed(3)}
              <span style={{ fontSize: '0.875rem', color: '#64748b', marginLeft: '0.25rem' }}>
                m/s
              </span>
            </div>
          </div>
          <div style={cardStyle}>
            <div style={labelStyle}>Peak speed (this round)</div>
            <div style={{ ...valueStyle, color: '#f59e0b' }}>
              {peakSpeed.toFixed(3)}
              <span style={{ fontSize: '0.875rem', color: '#64748b', marginLeft: '0.25rem' }}>
                m/s
              </span>
            </div>
          </div>
          <div style={cardStyle}>
            <div style={labelStyle}>Projected score (if you hit now)</div>
            <div style={{ ...valueStyle, fontSize: '2rem', color: '#22c55e' }}>
              {projectedScore}
            </div>
            <div style={{ fontSize: '0.75rem', color: '#64748b', marginTop: '0.5rem' }}>
              Slower & more deliberate = higher score.
              Score zeroes at peak ≥ {GAME_CONFIG.maxAllowedSpeed} m/s.
            </div>
          </div>
        </>
      )}

      {/* Outcome (when round just ended) */}
      {!roundActive && outcome && (
        <div style={cardStyle}>
          <div style={labelStyle}>Last round</div>
          <div style={{
            ...valueStyle,
            color: outcome === 'hit' ? '#22c55e' : '#ef4444',
          }}>
            {outcome === 'hit' ? 'HIT' : 'TIMEOUT'}
          </div>
          <div style={{ marginTop: '0.5rem', fontSize: '0.875rem', color: '#94a3b8' }}>
            Final score
          </div>
          <div style={{ ...valueStyle, fontSize: '3rem', color: '#22c55e' }}>
            {finalScore}
          </div>
          <div style={{ fontSize: '0.75rem', color: '#64748b', marginTop: '0.5rem' }}>
            Peak speed: {peakSpeed.toFixed(3)} m/s
          </div>
        </div>
      )}

      {/* Buttons */}
      <button
        onClick={onStartRound}
        disabled={roundActive}
        style={{
          width: '100%',
          padding: '0.875rem',
          marginTop: '1rem',
          backgroundColor: roundActive ? '#334155' : '#22c55e',
          color: '#0f172a',
          border: 'none',
          borderRadius: 8,
          fontWeight: 'bold',
          fontSize: '1rem',
          cursor: roundActive ? 'not-allowed' : 'pointer',
          textTransform: 'uppercase',
          letterSpacing: '0.05em',
        }}
      >
        {roundActive ? 'Round in progress' : 'Start round'}
      </button>

      <button
        onClick={onReset}
        style={{
          width: '100%',
          padding: '0.625rem',
          marginTop: '0.75rem',
          backgroundColor: 'transparent',
          color: '#94a3b8',
          border: '1px solid #334155',
          borderRadius: 8,
          fontSize: '0.875rem',
          cursor: 'pointer',
          textTransform: 'uppercase',
          letterSpacing: '0.05em',
        }}
      >
        Reset
      </button>
    </div>
  );
}

export default GameSidebar;
