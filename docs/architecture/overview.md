# Architecture overview

**Status:** target architecture for the SmartPark-IoT MVP; only the ESP32 edge
controller exists today (M0 baseline — see the component status table).
This document follows the project
[blueprint](../SMARTPARK_V2_COMPLETE_PROJECT_BLUEPRINT.md) §5.

## Components and data flow

```mermaid
flowchart TD
    S[Ultrasonic / passage sensors] --> E[ESP32 edge controller]
    E --> L[LCD, LEDs, prototype gate servo]
    E --> Q[Persistent outbound event queue]
    Q -->|Wi-Fi + MQTT| B[Mosquitto broker]
    B --> I[Backend MQTT consumer]
    I --> DB[(PostgreSQL)]
    DB --> API[FastAPI REST + WebSocket API]
    API --> W[SvelteKit operator dashboard]
    API --> M[Flutter app - optional, P2]
    SIM[Device simulator] --> B
```

| Component | Milestone | Status |
|---|---|---|
| ESP32 edge controller (sensing, local safety, display) | M0 baseline → M1 rework | 🟡 M1A: modular firmware + native tests merged into `main` working tree; hardware unverified |
| Persistent outbound event queue | M2 | ⬜ planned (in-memory dedup queue exists, M1A) |
| Wi-Fi + MQTT transport (topics, IDs, heartbeat) | M2 | ⬜ planned |
| Mosquitto broker | M2 | ⬜ planned |
| FastAPI backend + PostgreSQL | M3 | ⬜ planned |
| SvelteKit dashboard | M4 | ⬜ planned |
| Device simulator | M5 | ⬜ planned |
| Flutter app | M10 (optional) | ⬜ planned |

## Firmware architecture (as built in M1A)

The edge controller is split by dependency direction: **pure logic never
includes Arduino headers**, so the entire decision path compiles and runs on
the host (`pio test -e native`).

```mermaid
flowchart LR
    subgraph HAL["src/ (ESP32 only)"]
      RD[sensor_reader<br/>independent TRIG + bounded pulseIn]
      GL[main.cpp<br/>millis scheduler, servo/LCD/LED/serial]
    end
    subgraph CORE["lib/smartpark_core (pure C++11, unit-tested)"]
      PC[parking_controller<br/>orchestration + policy]
      SF[sensor_filter<br/>hysteresis / UNKNOWN / FAULT]
      PD[passage_detector<br/>approach FSM]
      OM[occupancy_manager<br/>bounded count / UNKNOWN]
      BC[barrier_controller<br/>gate FSM + guarded close]
      EQ[event_queue<br/>dedup + seq]
      DM[display_manager<br/>16-char LCD lines]
    end
    RD -->|cm or -1| PC
    GL -->|millis ticks| PC
    PC --> SF
    PC --> PD
    PC --> OM
    PC --> BC
    PC --> EQ
    PC --> DM
    GL -->|drain| EQ
    GL -->|view| DM
```

Design rules (enforced by review + the `-Werror` native build):

- **Time is always a parameter.** No hidden `millis()` reads inside the core —
  every `tick()/update(nowMs)` is deterministic and wrap-safe (tested across
  the `millis()` rollover).
- **`UNKNOWN` is a first-class state.** Timings that cannot be observed yield
  `UNKNOWN`; no path converts missing data to "free"/"closed" (ADR-006).
- **The queue is the single output surface** of the controller: serial logs
  drain it today; the MQTT publisher in M2 will drain the same queue without
  touching policy code.
- **Hardware availability is a policy input, not an assumption.** Missing
  passage/obstruction hardware appears as `UNKNOWN` feed or documented
  provisional behavior (`pinmap-v2.md` §9), never as invented signals.

Per-module responsibilities and their test coverage are listed in
[`firmware/esp32/README.md`](../../firmware/esp32/README.md).
| Wi-Fi + MQTT transport (topics, IDs, heartbeat) | M2 | ⬜ planned |
| Mosquitto broker | M2 | ⬜ planned |
| FastAPI backend + PostgreSQL | M3 | ⬜ planned |
| SvelteKit dashboard | M4 | ⬜ planned |
| Device simulator | M5 | ⬜ planned |
| Flutter app | M10 (optional) | ⬜ planned |

## Ownership and trust boundaries

- **ESP32 owns physical safety.** Sensing, occupancy logic and gate behavior
  must work with Wi-Fi, broker and backend completely unavailable
  (NFR-001). Network loss must never cause unsafe actuation.
- **Backend owns normalized event records.** It validates, deduplicates
  (idempotent by `(device_id, event_id)`) and stores events; it never reaches
  into hardware directly.
- **Database owns persistence**; migrations are the source of truth.
- **Dashboard is a client, not a controller.** It displays state and requests
  authenticated operator actions; it cannot bypass device-local safety checks.

Trust boundaries (mermaid):

```mermaid
flowchart LR
    subgraph Physical["Physical zone (supervised demonstrator)"]
        SENS[Sensors] --> ESP[ESP32]
        ESP --> ACT[Servo / LEDs / LCD]
    end
    subgraph LabLAN["Lab LAN (untrusted-ish)"]
        ESP <-->|MQTT + auth/TLS in deployment| BROKER[(Mosquitto)]
        BROKER --> BACKEND[FastAPI backend]
        BACKEND --> PG[(PostgreSQL)]
        BACKEND --> DASH[Dashboard clients]
    end
```

Everything crossing from the physical zone into the LAN is an *event or
heartbeat*, never raw actuator control from the internet. Operator commands
(future) are scoped, expiring, logged and validated locally by the ESP32.

## Failure-mode policy

| Failure | Expected behavior |
|---|---|
| Sensor timeout / stuck | State becomes `UNKNOWN`, health alert; never reported as "free" |
| Wi-Fi or broker down | Local operation continues; events queue durably; dashboard shows `OFFLINE`/stale, not fabricated freshness |
| Duplicate/reordered MQTT delivery | Idempotent ingest — exactly one stored logical event |
| Backend/DB down | API reports unavailable; device unaffected |
| ESP32 reboot | Safe startup; occupancy recovered from persistence or marked `UNKNOWN` until reconciled |
| Barrier obstruction | Gate FSM stops/reverses or latches `FAULT` (M1 design) — never blind timed close |

## Offline-first rationale

The demonstrator must be useful without internet connectivity and must not
corrupt its own occupancy state when connectivity flaps. That is why local
sensing + safety run entirely on the ESP32 (ADR-002), events carry durable
unique IDs (ADR-005), and "unknown" is a first-class state instead of assuming
free capacity (ADR-006). See
[`docs/decisions/0001-baseline.md`](../decisions/0001-baseline.md).

## Deployment variants

- **Development:** Docker Compose on a laptop (broker + DB + backend + frontend)
  — *does not exist yet; created in M3/M4/M5.*
- **Pilot:** local mini-PC/Raspberry Pi-class gateway running broker + backend,
  ESP32 controllers on the same LAN; optional cloud sync is a later decision.
- Internet connectivity is never a prerequisite for local operation.
