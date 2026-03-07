import React from 'react';

function ControlPanel({ onStart, onStop, status }) {
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
