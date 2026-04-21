// Wrapper per le chiamate al backend. In dev Vite fa proxy verso localhost:8080,
// in produzione cambi BASE_URL con l'indirizzo reale del backend.

const BASE_URL = '';  // vuoto = stesso host/porta (proxy Vite)

async function request(path, options = {}) {
  const url = `${BASE_URL}${path}`;
  const response = await fetch(url, {
    headers: { 'Content-Type': 'application/json', ...options.headers },
    ...options,
  });

  const contentType = response.headers.get('content-type') || '';
  const isJson = contentType.includes('application/json');
  const payload = isJson ? await response.json() : await response.text();

  if (!response.ok) {
    const message =
      (isJson && payload.message) ||
      `HTTP ${response.status} ${response.statusText}`;
    throw new Error(message);
  }

  return payload;
}

export async function getHealth() {
  return request('/api/health');
}

export async function getTopology() {
  return request('/api/topology');
}

export async function runDiscovery(gdsHost, gdsPort) {
  return request('/api/discovery/run', {
    method: 'POST',
    body: JSON.stringify({ gdsHost, gdsPort: Number(gdsPort) }),
  });
}

export async function getDevice(chassisId) {
  return request(`/api/devices/${encodeURIComponent(chassisId)}`);
}
