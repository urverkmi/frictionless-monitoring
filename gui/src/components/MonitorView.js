import React, { useState, useEffect } from 'react';
import MainDisplay from './MainDisplay';
import DataPanel from './DataPanel';
import ControlPanel from './ControlPanel';

import { connectToDataStream, disconnectFromDataStream } from '../services/dataService';
import { sendStart, sendStop } from '../services/controlService';

/**
 * How the full signal chain works end-to-end:
 * User clicks START in React
 *     → onStart() in MonitorView
 *         → sendStart() in controlService.js
 *             → POST http://pi.local:5001/cmd/start  (HTTP over WiFi/Ethernet)
 *                 → Flask sets MODE_STANDBY, then MODE_EARLY_DEPLOYMENT
 *                     → build_packet() constructs RS422 binary frame
 *                         → serial.Serial writes to /dev/ttyUSB0
 *                             → RS422 signal hits the PCB
 **/

const TETHER_LENGTH = 0;


function MonitorView() {
  const [data, setData] = useState({
    linearSpeed: { x: 0, y: 0 },
    angularSpeed: { x: 0, y: 0 },
    satellitePosition: { x: 0, y: 0 },
    endMassPosition: { x: 0, y: 0 },
    tetherLength: TETHER_LENGTH,
    timestamp: Date.now()
  });

  const [connectionStatus, setConnectionStatus] = useState('disconnected');
  const [controlStatus, setControlStatus] = useState('idle');
  const [targetPWM, setTargetPWM] = useState(200);

  useEffect(() => {
    const handleData = (newData) => setData(newData);
    const handleConnectionChange = (status) => setConnectionStatus(status);

    connectToDataStream(handleData, handleConnectionChange);

    return () => disconnectFromDataStream();
  }, []);

  const handleStart = async () => {
    try {
      await sendStart(targetPWM);
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
        targetPWM={targetPWM}
        onTargetPWMChange={setTargetPWM}
      />
    </div>
  );
}

export default MonitorView;
