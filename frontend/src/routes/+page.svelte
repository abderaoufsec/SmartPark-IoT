<script>
  import { onMount } from 'svelte';
  import { fetchDeviceStatus, fetchOccupancy, fetchHealth } from '$lib/api.js';

  let deviceStatus = null;
  let occupancy = null;
  let health = null;
  let error = '';
  const deviceId = 'esp32-001';

  onMount(async () => {
    try {
      [deviceStatus, occupancy, health] = await Promise.all([
        fetchDeviceStatus(deviceId),
        fetchOccupancy(deviceId),
        fetchHealth(),
      ]);
    } catch (err) {
      error = err.message;
    }
  });

  function fmtMs(ms) {
    if (!ms) return '—';
    return new Date(ms).toLocaleString();
  }
</script>

{#if error}
  <div class="alert alert-error">Error loading dashboard: {error}</div>
{:else}
  <h1>SmartPark-IoT Dashboard</h1>

  <section class="card">
    <h2>Device Health</h2>
    {#if health}
      <p>Status: <strong class={health.status === 'ok' ? 'ok' : 'bad'}>{health.status}</strong></p>
    {:else}
      <p>Unknown</p>
    {/if}
  </section>

  <section class="card">
    <h2>Device: {deviceId}</h2>
    {#if deviceStatus}
      <p>Online: <strong class={deviceStatus.is_online ? 'ok' : 'bad'}>{deviceStatus.is_online ? 'Yes' : 'No'}</strong></p>
      <p>First seen: {fmtMs(deviceStatus.first_seen)}</p>
      <p>Last seen: {fmtMs(deviceStatus.last_seen)}</p>
    {:else}
      <p>No data</p>
    {/if}
  </section>

  <section class="card">
    <h2>Occupancy</h2>
    {#if occupancy}
      <div class="metrics">
        <div class="metric">
          <div class="label">Count</div>
          <div class="value">{occupancy.count}</div>
        </div>
        <div class="metric">
          <div class="label">Free spaces</div>
          <div class="value">{occupancy.free_spaces}</div>
        </div>
      </div>
      <p>Recorded: {fmtMs(occupancy.recorded_at)}</p>
    {:else}
      <p>No data</p>
    {/if}
  </section>
{/if}

<style>
  .card {
    border: 1px solid #e5e7eb;
    border-radius: 8px;
    padding: 1.5rem;
    margin-bottom: 1rem;
    background: #fff;
  }
  .card h2 {
    margin-top: 0;
    font-size: 1.125rem;
    color: #111827;
  }
  .alert-error {
    background: #fef2f2;
    border: 1px solid #fecaca;
    color: #991b1b;
    padding: 1rem;
    border-radius: 4px;
    margin-bottom: 1rem;
  }
  .ok { color: #15803d; font-weight: 600; }
  .bad { color: #b91c1c; font-weight: 600; }
  .metrics {
    display: flex;
    gap: 2rem;
    margin-top: 1rem;
  }
  .metric {
    text-align: center;
  }
  .label {
    font-size: 0.875rem;
    color: #6b7280;
  }
  .value {
    font-size: 2rem;
    font-weight: 700;
    color: #111827;
  }
</style>