<script>
  import { navigate } from '$app/navigation';
  import { onMount } from 'svelte';
  import { login, setToken, getToken } from '$lib/api.js';

  let loggedIn = false;
  let loginError = '';
  let username = '';
  let password = '';

  onMount(() => {
    const t = getToken();
    loggedIn = !!t;
  });

  async function handleLogin(e) {
    e.preventDefault();
    try {
      await login(username, password);
      loggedIn = true;
      loginError = '';
      navigate('/');
    } catch (err) {
      loginError = err.message;
    }
  }

  function handleLogout() {
    logout();
    loggedIn = false;
    navigate('/login');
  }
</script>

{#if loggedIn}
  <nav class="navbar">
    <a href="/">Dashboard</a>
    <a href="/reconcile">Reconciliation</a>
    <button onclick={handleLogout}>Logout</button>
  </nav>
{:else}
  <div class="login-card">
    <h1>SmartPark-IoT</h1>
    <form onsubmit={handleLogin}>
      <label>Username</label>
      <input bind:value={username} type="text" required />
      <label>Password</label>
      <input bind:value={password} type="password" required />
      <button type="submit">Sign in</button>
      {#if loginError}
        <p class="error">{loginError}</p>
      {/if}
    </form>
  </div>
{/if}

<main>
  <slot />
</main>

<style>
  .navbar {
    display: flex;
    gap: 1rem;
    padding: 1rem;
    background: #1f2937;
    color: #fff;
  }
  .navbar a {
    color: #93c5fd;
    text-decoration: none;
    font-weight: 500;
  }
  .navbar a:hover {
    color: #fff;
  }
  .navbar button {
    margin-left: auto;
    background: #ef4444;
    color: #fff;
    border: none;
    padding: 0.5rem 1rem;
    border-radius: 4px;
    cursor: pointer;
  }
  .login-card {
    max-width: 320px;
    margin: 8rem auto;
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
  main {
    padding: 1rem;
    max-width: 960px;
    margin: 0 auto;
  }
</style>