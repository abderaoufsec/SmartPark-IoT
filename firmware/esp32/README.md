# SmartPark-IoT firmware (ESP32)

PlatformIO project for the ESP32 edge controller. **Status: M1A** — the V1
monolith has been split into a pure-logic core with host unit tests plus a thin
Arduino hardware layer. The build is green in CI (`firmware-ci`: compile +
native tests + cppcheck).

## Commands

```bash
cd firmware/esp32
pio run                 # compile for the ESP32 (espressif32@6.13.0, Arduino)
pio test -e native      # host unit tests — no ESP32 attached, pure logic
pio check -e native "--src-filters=+<lib/smartpark_core/*>+<src/*>" \
  "--flags=--enable=warning,style,performance,portability --std=c++11 --suppress=missingIncludeSystem --inline-suppr" \
  --fail-on-defect=medium --skip-packages     # cppcheck (same command as CI)
pio run -t upload       # flash over USB (board: esp32dev / "ESP32 Dev Module")
pio device monitor      # serial monitor, 115200 baud
```

Native tests need a host C++ compiler (g++/clang/MSVC). CI uses the
preinstalled `g++` on `ubuntu-latest`.

## Structure

| Path | Purpose | Tested natively? |
|---|---|---|
| `lib/smartpark_core/config.hpp` | every tuning constant (thresholds, timings, capacity) | via tests |
| `lib/smartpark_core/event_types.hpp` | event enum/struct, names (serial now, MQTT in M2) | ✅ |
| `lib/smartpark_core/event_queue.{hpp,cpp}` | fixed 16-slot ring, dedup, seq numbers, overflow counter | ✅ |
| `lib/smartpark_core/sensor_filter.{hpp,cpp}` | per-sensor FSM: hysteresis, N-stable filtering, `UNKNOWN`/`FAULT`, staleness | ✅ |
| `lib/smartpark_core/passage_detector.hpp` | lane FSM `IDLE→APPROACH→CONFIRMED/CANCELLED` (FR-003) | ✅ |
| `lib/smartpark_core/occupancy_manager.hpp` | bounded count, boot `UNKNOWN`, no silent clamping | ✅ |
| `lib/smartpark_core/barrier_controller.{hpp,cpp}` | non-blocking gate FSM, guarded close, abort/reopen | ✅ |
| `lib/smartpark_core/display_manager.{hpp,cpp}` | LCD 16×2 formatting, priority messages, never overflows | ✅ |
| `lib/smartpark_core/parking_controller.{hpp,cpp}` | orchestration: samples → events → admission → gate policy | ✅ |
| `src/pins.hpp` | GPIO map = `docs/hardware/pinmap-v2.md` | no (hardware) |
| `src/sensor_reader.{hpp,cpp}` | independent TRIG pulse + bounded `pulseIn` per slot | no (hardware) |
| `src/main.cpp` | Arduino glue: setup/loop scheduling, servo/LCD/LED/serial | no (hardware) |
| `test/test_core/test_main.cpp` | 41 Unity tests covering all M1A required areas | — |

Rule: **nothing under `lib/` may include Arduino headers.** If it touches a
pin, it belongs in `src/`.

## Behavior changes vs V1 (`legacy/v1/parking_intelligent.ino`)

Safety-relevant (never reproduce V1 behavior at the cost of safety):

1. **Timeouts are `UNKNOWN`, never "free"** (V1 F-03: `999 > 30` reported
   broken sensors as empty zones). A sensor that stops answering escalates
   `UNKNOWN → FAULT` after 10 consecutive timeouts; recovery needs 3 trusted
   samples.
2. **Approach is never counted** (V1 F-02 counted anything within 15 cm).
   The passage FSM needs an ordered second sensing point to confirm; the
   current 4-sensor wiring has none, so **the occupancy count never changes
   automatically** — only via explicit `reconcile()`. This is a documented
   hardware limitation (`pinmap-v2.md` §11), not a missing feature.
3. **Occupancy boots `UNKNOWN`** (V1 F-06 invented `0/6` free spaces on every
   reboot). While unknown, admission is deferred: the gate stays closed and
   the LCD shows `OCC:??? NO ENTRY`. Persistence lands in M1B (NFR-004).
4. **No blind timed close** (V1 F-04: `delay(2000)` then close). Closing
   starts only after the entry lane reads a *valid* FREE for 3 s; any
   non-FREE reading during the sweep aborts it (`BLOCKED →` reopen). This is
   a **provisional guarded close, not a certified obstruction sensor** — the
   entry beam's geometry is unverified (`pinmap-v2.md` §9) and there is no
   dedicated obstruction sensor in the M1A BOM.
5. **Independent per-sensor triggers, staggered 80 ms** (V1 F-01 shared one
   TRIG on GPIO 3 = UART0 RX, finding H-01). GPIO 3 is free again.
6. **No `delay()` in `loop()`** (V1 F-05 froze sensing/display ≈3.4 s per
   entry). Everything is `millis()`-scheduled; the only blocking call is one
   `pulseIn` bounded by 30 ms per 80 ms slot (documented bound).

Correctness / observability:

7. **Unified hysteresis thresholds** (V1 F-08): zone LEDs, guidance and
   classification share `OCCUPIED_ENTER_CM=15` / `FREE_ENTER_CM=30` with a
   required run of 3 stable samples to change state (FR-002). Values are
   provisional until M6 calibration.
8. **LCD lines always exactly 16 chars** (V1 F-07 overflow), priority
   messages: `GATE FAULT` > `ENTRY: FULL STOP` > `CAR AT ENTRY/EXIT` >
   `SMARTPARK V2`.
9. **Refusal is visible** (V1 F-09): one deduplicated `ADMISSION_DENIED`
   event + `ENTRY: FULL STOP` on the LCD when approach happens while full or
   unknown.
10. **No silent clamping** (V1 F-10): out-of-range occupancy attempts are
    rejected and counted with an `OCCUPANCY_CHANGE_REJECTED` event.
11. **Gate states are explicit**: `CLOSED/OPENING/OPEN/CLOSING/BLOCKED/FAULT/
    MANUAL`. `FAULT` latches at boot if servo attach fails; `MANUAL` exists
    in the FSM but is unreachable in M1A (no manual input hardware / no
    remote control in this milestone).
12. **Serial event log** with `seq` numbers (FR-008 groundwork); the event
    queue is the single hand-off point MQTT will consume in M2.

Intentionally *not* changed: capacity 6, 15/30 cm thresholds, servo pins/
pulses (50 Hz, 500–2400 µs, 0–90°), 2.5 s approach debounce, I2C address
0x27, boot splash text semantics (`BOOT - NO COUNT` replaces the 2 s splash).

## Safety posture (M1A)

- Power on with the **gate path clear**; there is no obstruction sensor yet.
- The gate **fails open** when the lane cannot be trusted (stays open rather
  than closing on unknown input) and **fails closed for admission** (stays
  shut while occupancy is unknown/full).
- Provisional behaviors are marked as such in code comments and in
  `docs/hardware/pinmap-v2.md`; nothing here is validated on hardware — see
  the M1A checklist in `docs/milestones/m1a.md`.

## Relationship to `legacy/v1/`

- `legacy/v1/parking_intelligent.ino` is the **immutable archive** of the
  original academic sketch (SHA-256 `d5cedf92…0c71ebd`). Never edit it.
- `src/main.cpp` was its verbatim PlatformIO copy during M0; in M1A it was
  replaced by the modular structure above. The original remains byte-exact
  under `legacy/v1/` — that is the rollback path.
