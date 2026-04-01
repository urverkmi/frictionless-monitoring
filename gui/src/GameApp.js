import React, { useState, useEffect } from 'react';
import VelocityGame from './components/VelocityGame';
import { connectToDataStream, disconnectFromDataStream } from './services/dataService';

function GameApp() {
  const [data, setData] = useState({
    linearSpeed: { x: 0, y: 0 },
    angularSpeed: { x: 0, y: 0 },
    satellitePosition: { x: 0, y: 0 },
    endMassPosition: { x: 0, y: 0 },
    tetherLength: 0,
    timestamp: Date.now(),
  });

  const [connectionStatus, setConnectionStatus] = useState('disconnected');

  useEffect(() => {
    connectToDataStream(
      (newData) => setData(newData),
      (status) => setConnectionStatus(status),
    );
    return () => disconnectFromDataStream();
  }, []);

  return (
    <div style={{ height: '100vh', backgroundColor: '#0f172a', color: '#e2e8f0' }}>
      {connectionStatus !== 'connected' && (
        <div style={{
          position: 'fixed',
          top: 12,
          right: 16,
          padding: '6px 14px',
          borderRadius: 8,
          fontSize: 13,
          fontWeight: 600,
          background: connectionStatus === 'connecting' ? '#854d0e' : '#991b1b',
          color: '#fff',
          zIndex: 10,
        }}>
          {connectionStatus === 'connecting' ? 'Connecting...' : 'Disconnected'}
        </div>
      )}
      <VelocityGame data={data} />
    </div>
  );
}

export default GameApp;
