# Requirements (MVP / P0)

Stable requirement IDs for use in GitHub issues, tests and PR references
(glossary: *FR* = functional, *NFR* = nonfunctional). Source: project
[blueprint](../SMARTPARK_V2_COMPLETE_PROJECT_BLUEPRINT.md) §4.

**Nothing below is implemented yet unless the Status column says otherwise.**
The M0 V1 baseline does not satisfy these requirements; it exists so M1 can
refactor toward them with a compiling starting point.

## Functional requirements

| ID | Requirement | Status |
|---|---|---|
| FR-001 | Read each sensor without uncontrolled cross-talk; label invalid/timeout measurements `UNKNOWN` | ❌ V1 shares one trigger and returns a "999" sentinel that is later treated as free |
| FR-002 | Filter noise with configurable hysteresis and consecutive stable readings | ❌ V1 compares single raw samples to fixed thresholds |
| FR-003 | Confirm passage direction-aware; never count mere approach as passage | ❌ V1 increments on approach within 15 cm |
| FR-004 | Reject/defer admission when capacity is exhausted or availability unknown | 🟡 V1 rejects when full only; ignores "unknown" |
| FR-005 | Gate finite-state machine: CLOSED, OPENING, OPEN, CLOSING, BLOCKED, FAULT, MANUAL; no unsafe timed close | ❌ V1 uses blocking delays and blind timed close |
| FR-006 | Local sensing and safe gate logic with Wi-Fi/backend unavailable | ✅ trivially true today (no network code); must remain true in M2+ |
| FR-007 | Persist counters and event queue across expected restarts; define crash consistency | ❌ counter resets on reboot |
| FR-008 | Publish validated occupancy events + heartbeat over MQTT with durable IDs and sequence numbers | ⬜ M2 |
| FR-009 | Backend deduplicates repeated events; records ingestion failures | ⬜ M3 |
| FR-010 | Authenticated dashboard: facility, zone, capacity, confidence, device health, history, alerts | ⬜ M4 |
| FR-011 | Operator reconciliation with reason and audit log; no silent corrections | ⬜ M3–M4 |
| FR-012 | Simulated device events exercise the same backend contract as physical hardware | ⬜ M5 |

## Nonfunctional requirements — engineering targets, subject to validation

| ID | Requirement | Status |
|---|---|---|
| NFR-001 | Local gate safety independent of network connectivity (mandatory) | design invariant from M1 onward |
| NFR-002 | Healthy LAN: 95% of confirmed events visible on dashboard within 2 s | ⬜ M4/M6 measurement |
| NFR-003 | No duplicate database rows after at-least-once replay | ⬜ M3 |
| NFR-004 | Reboot/reconnect recovers without inventing free spaces | ⬜ M1/M3 |
| NFR-005 | Local simulation reproducible with documented commands (Windows 11 + WSL2 or Linux) | ⬜ M5 |
| NFR-006 | API and broker reject unauthorized access; secrets never committed | partially: `.gitignore`/`.env.example` in M0; enforcement from M2/M3 |
| NFR-007 | CI validates builds/tests/formatting/migrations before merging | 🟡 firmware compile CI only (M0) |
| NFR-008 | Logs include event/device IDs and are scrubbed of secrets | ⬜ M2 |

## Acceptance evidence matrix (what "done" must show)

| Requirement | Required evidence |
|---|---|
| FR-003 | Approach / reverse / crossing test logs with count invariants |
| FR-005 | Barrier obstruction tests; manual recovery record |
| FR-006 | Broker/router disconnect demo with local operation |
| FR-007 | Power-cycle/reboot recovery report |
| FR-009 | Replay same event 100× → exactly one stored logical event |
| FR-010 | Screen recording + API test of live status |
| NFR-002 | Timestamped 100+ event latency sample with percentiles |
| NFR-006 | Negative authentication/authorization tests |

## Out of scope (prototype)

Unsupervised operation of a full-size road barrier, financial payments,
license-plate collection, and any compliance/safety-standard claim without
professional verification (blueprint §2).
