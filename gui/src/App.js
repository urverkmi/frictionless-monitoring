import React, { useState, useEffect } from 'react';
import MainDisplay from './components/MainDisplay';
import DataPanel from './components/DataPanel';
import ControlPanel from './components/ControlPanel';

import { connectToDataStream, disconnectFromDataStream } from './services/dataService';
import { sendStart, sendStop } from './services/controlService';

/**
 * How the full signal chain works end-to-end:
 * User clicks START in React
 *     → onStart() in App.js
 *         → sendStart() in controlService.js
 *             → POST http://pi.local:5000/cmd/start  (HTTP over WiFi/Ethernet)
 *                 → Flask sets MODE_STANDBY, then MODE_EARLY_DEPLOYMENT
 *                     → build_packet() constructs RS422 binary frame
 *                         → serial.Serial writes to /dev/ttyUSB0
 *                             → RS422 signal hits the PCB
 **/

const TETHER_LENGTH = 150;
const MAIN_SIZE = 20;
const END_MASS_RADIUS = 12;

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
  const [controlStatus, setControlStatus] = useState('idle');

  useEffect(() => {
    const handleData = (newData) => setData(newData);
    const handleConnectionChange = (status) => setConnectionStatus(status);

    connectToDataStream(handleData, handleConnectionChange);

    return () => disconnectFromDataStream();
  }, []);

  const handleStart = async () => {
    try {
      await sendStart();
      setControlStatus('running');
    } catch (err) {
      console.error(err);
      setControlStatus('error');
    }
  };

  const handleStop = async () => {
    try {
      await sendStop();
      setControlStatus('stopped');
    } catch (err) {
      console.error(err);
      setControlStatus('error');
    }
  };

  return (
    <div style={{
      display: 'flex',
      height: '100vh',
      backgroundColor: '#0f172a',
      color: '#e2e8f0'
    }}>
      <MainDisplay data={data} />
      <DataPanel data={data} connectionStatus={connectionStatus} />

      <ControlPanel
        onStart={handleStart}
        onStop={handleStop}
        status={controlStatus}
      />
    </div>
  );
}

export default App;
