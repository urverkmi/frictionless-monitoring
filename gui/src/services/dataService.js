import { DEVICE_HOST } from '../config/network';

// Must match cpp_stream_bridge.py --ws-port (default 8080)
const DATA_PORT = 8080;
const WS_HOST = process.env.REACT_APP_WS_HOST || DEVICE_HOST;

let dataCallback = null;
let statusCallback = null;
let ws = null;
let reconnectTimer = null;
let shouldReconnect = true;

function pickVec(...candidates) {
  for (const c of candidates) {
    if (c && typeof c.x === 'number' && typeof c.y === 'number') return c;
  }
  return null;
}

function normalizeIncomingPayload(raw) {
  const tag0Position = raw.tag0?.visible === false ? null : pickVec(raw.tag0);
  const tag1Position = raw.tag1?.visible === false ? null : pickVec(raw.tag1);

  const satellitePosition = pickVec(
    raw.satellitePosition,
    raw.mainPosition,
    raw.satellite_position,
    raw.main_position,
    tag0Position,
  );

  const endMassPosition = pickVec(
    raw.endMassPosition,
    raw.position,
    raw.end_mass_position,
    tag1Position,
  );

  const linearSpeed = pickVec(
    raw.linearSpeed,
    raw.velocity,
    raw.linear_speed,
  ) || { x: 0, y: 0 };

  const angularSpeed = pickVec(
    raw.angularSpeed,
    raw.angular_speed,
  ) || { x: 0, y: 0 };

  let tetherLength = typeof raw.tetherLength === 'number'
    ? raw.tetherLength
    : (typeof raw.tether_length === 'number' ? raw.tether_length : 0);

  if (
    tetherLength === 0 &&
    satellitePosition &&
    endMassPosition
  ) {
    tetherLength = Math.hypot(
      endMassPosition.x - satellitePosition.x,
      endMassPosition.y - satellitePosition.y,
    );
  }

  return {
    ...raw,
    // vision/main.cpp logs often expose `ts` as monotonic clock, not wall time.
    timestamp: typeof raw.timestamp === 'number' ? raw.timestamp : Date.now(),
    satellitePosition,
    mainPosition: raw.mainPosition || satellitePosition,
    endMassPosition,
    linearSpeed,
    angularSpeed,
    tetherLength,
  };
}

function connectWebSocket() {
  if (statusCallback) statusCallback('connecting');
  ws = new WebSocket(`ws://${WS_HOST}:${DATA_PORT}`);

  ws.onopen = () => {
    console.log('Connected to data stream');
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    if (statusCallback) statusCallback('connected');
  };

  ws.onmessage = (event) => {
    try {
      const raw = JSON.parse(event.data);
      const data = normalizeIncomingPayload(raw);
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
    if (shouldReconnect && !reconnectTimer) {
      reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connectWebSocket();
      }, 3000);
    }
  };
}

export function connectToDataStream(onData, onStatusChange) {
  dataCallback = onData;
  statusCallback = onStatusChange;
  shouldReconnect = true;
  console.log(`Connecting to ws://${WS_HOST}:${DATA_PORT}`);
  connectWebSocket();
}

export function disconnectFromDataStream() {
  shouldReconnect = false;
  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
  if (ws) {
    ws.close();
    ws = null;
  }
  dataCallback = null;
  statusCallback = null;
}

export function sendCommand(command) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(command));
  } else {
    console.warn('WebSocket not connected, cannot send command');
  }
}
