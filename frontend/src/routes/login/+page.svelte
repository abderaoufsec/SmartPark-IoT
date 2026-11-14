<script>
  import { navigate } from '$app/navigation';
  import { login } from '$lib/api.js';

  let username = '';
  let password = '';
  let error = '';

  async function handleSubmit(e) {
    e.preventDefault();
    try {
      await login(username, password);
      navigate('/');
    } catch (err) {
      error = err.message;
    }
  }
</script>

<div class="login-page">
  <div class="login-card">
    <h1>SmartPark-IoT</h1>
    <form onsubmit={handleSubmit}>
      <label>Username</label>
      <input bind:value={username} type="text" required />
      <label>Password</label>
      <input bind:value={password} type="password" required />
      <button type="submit">Sign in</button>
      {#if error}
        <p class="error">{error}</p>
      {/if}
    </form>
  </div>
</div>

<style>
  .login-page {
    display: flex;
    align-items: center;
    justify-content: center;
    min-height: 100vh;
    background: #f3f4f6;
  }
  .login-card {
    max-width: 320px;
    padding: 2rem;
    border: 1px solid #e5e7eb;
    border-radius: 8px;
    background: #fff;
  }
  .login-card h1 {
    text-align: center;
    margin-bottom: 1.5rem;
    color: #111827;
  }
  .login-card label {
    display: block;
    margin-top: 1rem;
    font-weight: 500;
    color: #374151;
  }
  .login-card input {
    width: 100%;
    padding: 0.5rem;
    margin-top: 0.25rem;
    border: 1px solid #d1d5db;
    border-radius: 4px;
    box-sizing: border-box;
  }
  .login-card button {
    width: 100%;
    margin-top: 1.5rem;
    padding: 0.6rem;
    background: #2563eb;
    color: #fff;
    border: none;
    border-radius: 4px;
    font-weight: 600;
    cursor: pointer;
  }
  .login-card button:hover {
    background: #1d4ed8;
  }
  .error {
    color: #dc2626;
    margin-top: 1rem;
    text-align: center;
    font-size: 0.875rem;
  }
</style>