<script>
  import { onMount } from 'svelte';
  import { fetchEvents, fetchOccupancy } from '$lib/api.js';

  let events = [];
  let occupancy = null;
  let error = '';
  const deviceId = 'esp32-001';
  let newCount = '';
  let reason = '';

  onMount(async () => {
    try {
      [events, occupancy] = await Promise.all([
        fetchEvents(deviceId, 50),
        fetchOccupancy(deviceId),
      ]);
    } catch (err) {
      error = err.message;
    }
  });

  async function handleReconcile(e) {
    e.preventDefault();
    // Placeholder: the backend reconciliation endpoint is a future enhancement
    alert('Reconciliation endpoint is a future backend enhancement (FR-011).');
  }
</script>

{#if error}
  <div class="alert alert-error">Error: {error}</div>
{/if}

<h1>Operator Reconciliation</h1>
<p class="note">
  Set the occupancy count explicitly with a reason. Every change is audited —
  no silent corrections (FR-011).
</p>

<section class="card">
  <h2>Current Occupancy</h2>
  {#if occupancy}
    <p>Count: <strong>{occupancy.count}</strong> · Free: <strong>{occupancy.free_spaces}</strong></p>
    <p>Recorded: {new Date(occupancy.recorded_at).toLocaleString()}</p>
  {:else}
    <p>No data</p>
  {/if}
</section>

<section class="card">
  <h2>Recent Events</h2>
  <ul class="event-list">
    {#each events as ev}
      <li class="event-item">
        <span class="type">{ev.type}</span>
        <span class="device">{ev.device_id}</span>
        <span class="seq">seq={ev.seq}</span>
        <span class="value">value={ev.value}</span>
      </li>
    {:else}
      <li>No events</li>
    {/each}
  </ul>
</section>

<section class="card">
  <h2>Reconcile Count</h2>
  <form onsubmit={handleReconcile}>
    <label>New count</label>
    <input bind:value={newCount} type="number" min="0" max="6" required />
    <label>Reason (required)</label>
    <input bind:value={reason} type="text" required />
    <button type="submit" disabled={!newCount || !reason}>
      Apply Reconciliation
    </button>
    <p class="hint">This sets the occupancy explicitly; the backend records an audit event.</p>
  </form>
</section>

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
  .note {
    color: #6b7280;
    font-size: 0.875rem;
    margin-bottom: 1rem;
  }
  .event-list {
    list-style: none;
    padding: 0;
    margin: 0;
    max-height: 240px;
    overflow-y: auto;
    border: 1px solid #e5e7eb;
    border-radius: 4px;
  }
  .event-item {
    padding: 0.5rem 0.75rem;
    border-bottom: 1px solid #f3f4f6;
    font-family: ui-monospace, SFMono-Regular, monospace;
    font-size: 0.875rem;
    display: flex;
    gap: 1rem;
  }
  .event-item:last-child {
    border-bottom: none;
  }
  .type { color: #2563eb; }
  .device { color: #6b7280; }
  .seq { color: #059669; }
  .value { color: #d97706; }
  form {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
  }
  label {
    font-weight: 500;
    color: #374151;
  }
  input {
    padding: 0.5rem;
    border: 1px solid #d1d5db;
    border-radius: 4px;
  }
  button {
    padding: 0.6rem 1.2rem;
    background: #2563eb;
    color: #fff;
    border: none;
    border-radius: 4px;
    font-weight: 600;
    cursor: pointer;
  }
  button:disabled {
    background: #9ca3af;
    cursor: not-allowed;
  }
  .hint {
    font-size: 0.875rem;
    color: #6b7280;
    margin-top: 0.5rem;
  }
</style>