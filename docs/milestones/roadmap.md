# Roadmap — milestones M0 to M10

Source: project [blueprint](../SMARTPARK_V2_COMPLETE_PROJECT_BLUEPRINT.md) §15.
**A milestone is complete when its exit gate has evidence — not when the date
passes.** Effort figures are planning estimates for two part-time contributors.

| # | Focus | Suggested effort | Exit gate | Status |
|---|---|---|---|---|
| **M0** | Fork, preserve V1, repository structure, docs & CI foundation | 2–4 days | Reproducible baseline + reviewed PR | ✅ see [m0.md](m0.md) |
| **M1** | Safe & testable firmware: pin redesign, PlatformIO modules, filtering, passage confirmation, gate FSM, persistence | 2–3 weeks | Passage, sensor and barrier tests pass | ⬜ next |
| **M2** | MQTT + offline queue, Wi-Fi lifecycle, event IDs, heartbeat | 1–2 weeks | Reconnect/replay without duplicate logical events | ⬜ |
| **M3** | FastAPI backend + PostgreSQL, idempotent ingest, auth, API | 2 weeks | Migrations, ingest, auth, API tests green | ⬜ |
| **M4** | SvelteKit operator dashboard | 2 weeks | Live, responsive, authenticated UI with tests | ⬜ |
| **M5** | Simulator + end-to-end integration from a clean clone | 1–2 weeks | Full-stack demo without hardware | ⬜ |
| **M6** | Hardware integration, electrical verification, calibration, measured metrics | 2–3 weeks | Recorded physical tests and measured metrics | ⬜ |
| **M7** | Security hardening, monitoring, resilience test matrix | 1–2 weeks | Failure/security acceptance suite | ⬜ |
| **M8** | Pilot packaging, deployment & operations documentation | 1–2 weeks | Clean-install and rollback demonstration | ⬜ |
| **M9** | *(optional)* Multi-facility, per-space sensing, RFID/QR | 2–4 weeks | Scoped feature tests + safety review | ⬜ |
| **M10** | *(optional)* Flutter app, analytics, research extensions | variable | Separate product/research acceptance criteria | ⬜ |

## M1 (next milestone) — step plan

1. Record the exact ESP32 board, wiring, and revise the pin map (fixes F-01,
   H-01, H-03 design constraints from the [V1 audit](../hardware/v1-audit.md)).
2. Electrical safety: level-shift 5 V ECHO (H-02), servo supply design (H-04),
   verify LED resistors (H-06) — coordinate with M6 where physical work is needed.
3. Restructure the PlatformIO firmware into modules; extract **pure logic**
   (sensor classification, hysteresis, state transitions, count bounds) so it
   can be unit-tested on the host (`native` env).
4. Independent per-sensor triggering + filtering (FR-001, FR-002).
5. Direction-aware passage confirmation hardware/logic (FR-003).
6. Non-blocking barrier FSM with obstruction response (FR-005).
7. Persistent occupancy + startup reconciliation (FR-007).
8. Unit, simulation and hardware tests with documented results.

## M2 — step plan (summary)

Versioned JSON schema + topic ACL plan, Wi-Fi/broker lifecycle, durable bounded
event queue with acks, unique event IDs/heartbeat, outage & replay test
scenarios (offline 30 s / 10 min, reboot during publish, duplicates, queue full).

## Working agreements

- Milestone scope is fixed by its exit gate; new ideas go to the backlog or a
  later milestone, not into an in-flight PR.
- Evidence required to close: build logs, test output, recordings or
  measurements — see [requirements](../requirements/requirements.md) acceptance
  matrix.
- Sequence matters: **M1 (local firmware correctness) before M2 (MQTT) before
  M3 (backend).** The blueprint's firmware development sequence
  (§6) is authoritative for M1.
