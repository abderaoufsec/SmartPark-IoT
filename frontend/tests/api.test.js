import { describe, it, expect, vi, beforeEach } from 'vitest';

// Mock fetch globally
global.fetch = vi.fn();

describe('API client', () => {
  beforeEach(() => {
    fetch.mockClear();
  });

  it('login stores token and posts to /token', async () => {
    fetch.mockResolvedValueOnce({
      ok: true,
      json: async () => ({ access_token: 'abc123', token_type: 'bearer' }),
    });

    const { login, getToken } = await import('../src/lib/api.js');
    const data = await login('user', 'pass');

    expect(fetch).toHaveBeenCalledWith(
      'http://localhost:8000/token',
      expect.objectContaining({ method: 'POST' })
    );
    expect(data.access_token).toBe('abc123');
    expect(getToken()).toBe('abc123');
  });

  it('fetchEvents includes Bearer token', async () => {
    fetch.mockResolvedValueOnce({ ok: true, json: async () => [] });
    const { setToken, fetchEvents } = await import('../src/lib/api.js');
    setToken('test-token');
    await fetchEvents('esp32-001');
    expect(fetch).toHaveBeenCalledWith(
      'http://localhost:8000/events/?device_id=esp32-001&limit=100',
      expect.objectContaining({
        headers: expect.objectContaining({ Authorization: 'Bearer test-token' }),
      })
    );
  });
});