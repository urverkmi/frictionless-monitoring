import React from 'react';

function ControlPanel({ onStart, onStop, status, targetPWM, onTargetPWMChange }) {
  return (
    <div style={{
      width: '250px',
      padding: '20px',
      backgroundColor: '#111827',
      borderLeft: '1px solid #1f2937',
      display: 'flex',
      flexDirection: 'column',
      gap: '12px'
    }}>
      <h2>Control Panel</h2>

      <label style={{ fontSize: '14px', color: '#9ca3af' }}>
        Target PWM (50–200)
        <input
          type="number"
          min={50}
          max={200}
          value={targetPWM}
          onChange={(e) => onTargetPWMChange(Number(e.target.value))}
          style={{
            display: 'block',
            width: '100%',
            marginTop: '4px',
            padding: '8px',
            backgroundColor: '#1f2937',
            border: '1px solid #374151',
            borderRadius: '6px',
            color: '#e2e8f0',
            fontSize: '16px',
            boxSizing: 'border-box'
          }}
        />
      </label>

      <button
        onClick={onStart}
        style={{
          padding: '10px',
          backgroundColor: '#16a34a',
          border: 'none',
          borderRadius: '6px',
          color: 'white',
          cursor: 'pointer'
        }}
      >
        START
      </button>

      <button
        onClick={onStop}
        style={{
          padding: '10px',
          backgroundColor: '#dc2626',
          border: 'none',
          borderRadius: '6px',
          color: 'white',
          cursor: 'pointer'
        }}
      >
        STOP
      </button>

      <div>Status: {status}</div>
    </div>
  );
}

export default ControlPanel;
