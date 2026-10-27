# Security Policy

## Reporting a vulnerability

Please **do not** open a public GitHub issue for security problems.

- Preferred: use GitHub's private reporting —
  <https://github.com/abderaoufsec/SmartPark-IoT/security/advisories/new>
- If that is unavailable, contact the maintainer
  ([@abderaoufsec](https://github.com/abderaoufsec)) directly through GitHub
  and ask for a private channel; we will coordinate from there.

Include: affected component (firmware/backend/dashboard), reproduction steps,
impact, and whether you have notified anyone else. We aim to acknowledge
reports within a few days; this is a small student-maintained project, so please
allow patience over enterprise SLAs. Please keep details confidential until a
fix or disclosure decision is agreed.

## Scope

In scope (once implemented): firmware in `firmware/`, and from M3 onward the
backend, MQTT usage, database and dashboard.

Out of scope: the archived `legacy/v1/` code (immutable history), third-party
libraries and services, and social-engineering of contributors.

## Known security posture (M0)

- There is **no Wi-Fi, MQTT, network service or authentication code** in the
  firmware yet — the attack surface today is the USB/serial flashing path only.
- No automated dependency/secret scanning is configured yet; GitHub's built-in
  secret scanning (for public repos) applies once pushed.
- `.env` is git-ignored; `.env.example` contains placeholders only.
  Rotate any credential you ever pasted into a real file before committing.

## ⚠️ Prototype safety limitations (read before powering hardware)

This project controls a **model gate with a small servo on a desk demonstrator**.
It is **not** a certified barrier and must never control real vehicle access:

- The V1 firmware closes the barrier after a fixed delay with **no obstruction
  detection** — it can push against an obstacle.
- The V1 wiring has documented electrical risks (5 V echo signals into
  3.3 V-only GPIOs, boot-strap pin usage, UART conflict) — see
  [`docs/hardware/v1-audit.md`](docs/hardware/v1-audit.md).
- Only operate the prototype under supervision, with no exposed moving parts
  and a safe low-voltage supply.

Professional safety and electrical assessment is mandatory before any real
deployment. Reporting an unsafe-behavior finding in the firmware is a security
report — use the channels above.
