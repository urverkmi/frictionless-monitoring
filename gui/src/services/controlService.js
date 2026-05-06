import { DEVICE_HOST } from '../config/network';

const BASE_URL = `http://${DEVICE_HOST}:5001`;

export async function sendStart(targetPWM = 200) {
  const response = await fetch(`${BASE_URL}/cmd/start`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ targetPWM })
  });
  if (!response.ok) {
    const err = await response.json().catch(() => ({}));
    throw new Error(`START failed: ${response.status} — ${err.detail ?? response.statusText}`);
  }
  return response.json();
}

export async function sendStop() {
  const response = await fetch(`${BASE_URL}/cmd/stop`, { method: 'POST' });
  if (!response.ok) {
    const err = await response.json().catch(() => ({}));
    throw new Error(`STOP failed: ${response.status} — ${err.detail ?? response.statusText}`);
  }
  return response.json();
}
