// Backend / device hostname for HTTP (control) and default WebSocket host.
// Priority:
// 1) REACT_APP_DEVICE_HOST from gui/.env (explicit override)
// 2) Browser host (keeps old behavior for remote Pi access)
// 3) localhost fallback
export const DEVICE_HOST =
  process.env.REACT_APP_DEVICE_HOST ||
  window.location.hostname ||
  'localhost';
