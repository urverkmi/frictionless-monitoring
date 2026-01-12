import React from 'react';

function DataPanel({ data, connectionStatus }) {
  const VectorDisplay = ({ title, vector, unit }) => (
    <div style={{
      backgroundColor: '#1e293b',
      padding: '1.5rem',
      borderRadius: '8px',
      marginBottom: '1rem',
      boxShadow: '0 2px 4px rgba(0, 0, 0, 0.3)'
    }}>
      <h3 style={{
        margin: '0 0 1rem 0',
        fontSize: '0.75rem',
        color: '#94a3b8',
        textTransform: 'uppercase',
        letterSpacing: '0.05em'
      }}>
        {title}
      </h3>
      <div style={{ display: 'flex', flexDirection: 'column', gap: '0.75rem' }}>
        {['x', 'y', 'z'].map(axis => (
          <div key={axis} style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <span style={{ 
              color: '#64748b', 
              fontSize: '0.875rem',
              textTransform: 'uppercase',
              width: '30px'
            }}>
              {axis}:
            </span>
            <div style={{ 
              flex: 1, 
              marginLeft: '0.75rem',
              marginRight: '0.75rem',
              height: '8px',
              backgroundColor: '#0f172a',
              borderRadius: '9999px',
              overflow: 'hidden'
            }}>
              <div 
                style={{ 
                  height: '100%',
                  width: `${Math.min(Math.abs(vector[axis]) / 3 * 100, 100)}%`,
                  backgroundColor: '#3b82f6',
                  transition: 'all 0.1s',
                  marginLeft: vector[axis] < 0 ? 'auto' : '0'
                }}
              />
            </div>
            <span style={{ 
              color: '#e2e8f0', 
              fontSize: '1.25rem',
              fontWeight: '600',
              width: '112px',
              textAlign: 'right',
              fontFamily: 'monospace'
            }}>
              {vector[axis].toFixed(3)}
              <span style={{ fontSize: '0.875rem', color: '#64748b', marginLeft: '0.25rem' }}>
                {unit}
              </span>
            </span>
          </div>
        ))}
      </div>
    </div>
  );

  const getStatusColor = () => {
    switch(connectionStatus) {
      case 'connected': return '#10b981';
      case 'connecting': return '#f59e0b';
      default: return '#ef4444';
    }
  };

  const linearMag = Math.sqrt(
    data.linearSpeed.x ** 2 + 
    data.linearSpeed.y ** 2 + 
    data.linearSpeed.z ** 2
  );
  
  const angularMag = Math.sqrt(
    data.angularSpeed.x ** 2 + 
    data.angularSpeed.y ** 2 + 
    data.angularSpeed.z ** 2
  );

  // Calculate distance from main unit to end mass
  const distance = Math.sqrt(
    data.endMassPosition.x ** 2 + 
    data.endMassPosition.y ** 2
  );

  return (
    <div style={{
      width: '400px',
      padding: '2rem',
      overflowY: 'auto',
      backgroundColor: '#0f172a'
    }}>
      {/* Connection Status */}
      <div style={{
        display: 'flex',
        alignItems: 'center',
        gap: '0.5rem',
        marginBottom: '2rem',
        padding: '1rem',
        backgroundColor: '#1e293b',
        borderRadius: '8px'
      }}>
        <div style={{
          width: '12px',
          height: '12px',
          borderRadius: '50%',
          backgroundColor: getStatusColor(),
          animation: 'pulse 2s cubic-bezier(0.4, 0, 0.6, 1) infinite'
        }} />
        <span style={{ textTransform: 'capitalize', fontSize: '0.875rem' }}>
          {connectionStatus}
        </span>
        <span style={{ 
          marginLeft: 'auto', 
          fontSize: '0.75rem', 
          color: '#64748b' 
        }}>
          {new Date(data.timestamp).toLocaleTimeString()}
        </span>
      </div>

      {/* Magnitude Cards */}
      <div style={{ 
        display: 'grid', 
        gridTemplateColumns: '1fr 1fr', 
        gap: '0.75rem',
        marginBottom: '1.5rem'
      }}>
        <div style={{
          backgroundColor: '#1e293b',
          padding: '1rem',
          borderRadius: '8px'
        }}>
          <div style={{ fontSize: '0.75rem', color: '#64748b', marginBottom: '0.25rem' }}>
            Linear Magnitude
          </div>
          <div style={{ fontSize: '1.5rem', fontWeight: 'bold', color: '#3b82f6' }}>
            {linearMag.toFixed(2)}
          </div>
        </div>
        <div style={{
          backgroundColor: '#1e293b',
          padding: '1rem',
          borderRadius: '8px'
        }}>
          <div style={{ fontSize: '0.75rem', color: '#64748b', marginBottom: '0.25rem' }}>
            Angular Magnitude
          </div>
          <div style={{ fontSize: '1.5rem', fontWeight: 'bold', color: '#a855f7' }}>
            {angularMag.toFixed(2)}
          </div>
        </div>
      </div>

      {/* Linear Speed */}
      <VectorDisplay 
        title="Linear Speed" 
        vector={data.linearSpeed} 
        unit="m/s"
      />

      {/* Angular Speed */}
      <VectorDisplay 
        title="Angular Speed" 
        vector={data.angularSpeed} 
        unit="rad/s"
      />

    </div>
  );
}

export default DataPanel;