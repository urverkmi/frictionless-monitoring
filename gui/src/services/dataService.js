// Configuration
// DATA_PORT must match the WebSocket server in Python (e.g. FakeDataStream.py)
const DATA_PORT = 8080;
// When false, connect to real WebSocket stream instead of local mock generator
const USE_MOCK_DATA = false; // Set to false when real data is available

// Physical constants
const TETHER_LENGTH = 150; // pixels (or meters in real data)
const MAIN_SIZE = 20;
const END_MASS_RADIUS = 12;
const ROTATION_SPEED = 0.3; // radians per second

let dataCallback = null;
let statusCallback = null;
let mockInterval = null;
let ws = null;
let startTime = Date.now();
let currentAngle = 0;

// Smooth mock data generator
// Shape of returned object must match what the GUI expects from the Python stream
function generateMockData() {
  const elapsed = (Date.now() - startTime) / 1000;
  const dt = 0.05; // 50ms time step
  
  // Update rotation angle smoothly
  currentAngle += ROTATION_SPEED * dt;
  
  // Calculate end mass position (rotating around origin)
  const endMassX = Math.cos(currentAngle) * TETHER_LENGTH;
  const endMassY = Math.sin(currentAngle) * TETHER_LENGTH;
  
  // Smooth sinusoidal velocities for more realistic motion
  const time = elapsed;
  const linearSpeed = {
    x: Math.sin(time * 0.3) * 1.5,
    y: Math.cos(time * 0.25) * 1.2,
    z: Math.sin(time * 0.35) * 0.8
  };
  
  const angularSpeed = {
    x: Math.sin(time * 0.2) * 0.5,
    y: Math.cos(time * 0.15) * 0.4,
    z: ROTATION_SPEED // constant rotation around Z axis
  };

  return {
    linearSpeed,
    angularSpeed,
    mainPosition: {
      x: 0,
      y: 0
    },
    endMassPosition: {
      x: endMassX,
      y: endMassY
    },
    tetherLength: TETHER_LENGTH,
    mainSize: MAIN_SIZE,
    endMassRadius: END_MASS_RADIUS,
    timestamp: Date.now()
  };
}

// Connect to WebSocket
function connectWebSocket() {
  if (statusCallback) statusCallback('connecting');
  
ws = new WebSocket(`ws://${window.location.hostname}:${DATA_PORT}`);
  
  ws.onopen = () => {
    console.log('Connected to data stream');
    if (statusCallback) statusCallback('connected');
  };
  
  ws.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      if (dataCallback) dataCallback(data);
    } catch (error) {
      console.error('Error parsing data:', error);
    }
  };
  
  ws.onerror = (error) => {
    console.error('WebSocket error:', error);
    if (statusCallback) statusCallback('error');
  };
  
  ws.onclose = () => {
    console.log('Disconnected from data stream');
    if (statusCallback) statusCallback('disconnected');
    // Attempt to reconnect after 3 seconds
    setTimeout(() => {
      if (!USE_MOCK_DATA) connectWebSocket();
    }, 3000);
  };
}

// Start mock data stream
function startMockStream() {
  if (statusCallback) statusCallback('connected');
  startTime = Date.now();
  currentAngle = 0;
  
  mockInterval = setInterval(() => {
    if (dataCallback) {
      dataCallback(generateMockData());
    }
  }, 50); // 20 Hz update rate
}

// Public API
export function connectToDataStream(onData, onStatusChange) {
  dataCallback = onData;
  statusCallback = onStatusChange;
  
  if (USE_MOCK_DATA) {
    console.log('Using mock data stream');
    startMockStream();
  } else {
    console.log(`Connecting to ws://localhost:${DATA_PORT}`);
    connectWebSocket();
  }
}

export function disconnectFromDataStream() {
  if (mockInterval) {
    clearInterval(mockInterval);
    mockInterval = null;
  }
  if (ws) {
    ws.close();
    ws = null;
  }
  dataCallback = null;
  statusCallback = null;
}

// Send commands to data source (for future use)
export function sendCommand(command) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(command));
  } else {
    console.warn('WebSocket not connected, cannot send command');
  }
}