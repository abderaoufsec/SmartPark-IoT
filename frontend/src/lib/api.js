/** SmartPark-IoT dashboard API client (M4).

Talks to the FastAPI backend (M3). All endpoints require a Bearer JWT
obtained from POST /token. Base URL from env or default localhost:8000.
*/
const API_BASE = import.meta.env.VITE_API_BASE_URL || 'http://localhost:8000';

let token = '';

export function setToken(t) {
  token = t;
}

export function getToken() {
  return token;
}

async function request(path, opts = {}) {
  const headers = {
    'Content-Type': 'application/json',
    ...(token ? { Authorization: `Bearer ${token}` } : {}),
    ...opts.headers,
  };
  const res = await fetch(`${API_BASE}${path}`, { ...opts, headers });
  if (!res.ok) {
    const body = await res.text().catch(() => '');
    throw new Error(`API ${res.status} ${res.statusText}: ${body}`);
  }
  if (res.status === 204) return null;
  return res.json();
}

export async function login(username, password) {
  const form = new FormData();
  form.append('username', username);
  form.append('password', password);
  const res = await fetch(`${API_BASE}/token`, { method: 'POST', body: form });
  if (!res.ok) throw new Error('Login failed');
  const data = await res.json();
  setToken(data.access_token);
  return data;
}

export async function logout() {
  token = '';
}

export async function fetchEvents(deviceId, limit = 100) {
  const qs = deviceId ? `?device_id=${encodeURIComponent(deviceId)}&limit=${limit}` : `?limit=${limit}`;
  return request(`/events/${qs}`);
}

export async function fetchDeviceStatus(deviceId) {
  return request(`/status/${deviceId}`);
}

export async function fetchOccupancy(deviceId) {
  return request(`/occupancy/${deviceId}`);
}

export async function fetchHealth() {
  return request('/health');
}