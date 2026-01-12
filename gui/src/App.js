import React, { useState, useEffect, useRef } from 'react';
import MainDisplay from './components/MainDisplay';
import DataPanel from './components/DataPanel';
import { connectToDataStream, disconnectFromDataStream } from './services/dataService';

// Configuration constants
const TETHER_LENGTH = 150;
const MAIN_SIZE = 20;
const END_MASS_RADIUS = 12;
const ROTATION_SPEED = 0.3;

function App() {
  const [data, setData] = useState({
    linearSpeed: { x: 0, y: 0, z: 0 },
    angularSpeed: { x: 0, y: 0, z: 0 },
    mainPosition: { x: 0, y: 0 },
    endMassPosition: { x: TETHER_LENGTH, y: 0 },
    tetherLength: TETHER_LENGTH,
    mainSize: MAIN_SIZE,
    endMassRadius: END_MASS_RADIUS,
    timestamp: Date.now()
  });

  const [connectionStatus, setConnectionStatus] = useState('disconnected');

  useEffect(() => {
    // Connect to data stream
    const handleData = (newData) => {
      setData(newData);
    };

    const handleConnectionChange = (status) => {
      setConnectionStatus(status);
    };

    connectToDataStream(handleData, handleConnectionChange);

    return () => {
      disconnectFromDataStream();
    };
  }, []);

  return (
    <div style={{
      display: 'flex',
      height: '100vh',
      backgroundColor: '#0f172a',
      color: '#e2e8f0'
    }}>
      <MainDisplay data={data} />
      <DataPanel data={data} connectionStatus={connectionStatus} />
    </div>
  );
}

export default App;