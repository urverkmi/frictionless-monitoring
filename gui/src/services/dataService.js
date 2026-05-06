import { DEVICE_HOST } from '../config/network';

// Must match cpp_stream_bridge.py --ws-port (default 8080)
const DATA_PORT = 8080;
const WS_HOST = process.env.REACT_APP_WS_HOST || DEVICE_HOST;

let dataCallback = null;
let statusCallback = null;
let ws = null;
let reconnectTimer = null;
let shouldReconnect = true;

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
