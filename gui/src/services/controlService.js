const BASE_URL = 'http://localhost:5000';

export async function sendStart() {
  const response = await fetch(`${BASE_URL}/cmd/start`, { method: 'POST' });
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
