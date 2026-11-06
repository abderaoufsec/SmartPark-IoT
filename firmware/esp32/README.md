# SmartPark-IoT firmware (ESP32)

PlatformIO project for the ESP32 edge controller. **Status: M1B** — the V1
monolith was split into a pure-logic core with host unit tests plus a thin
Arduino hardware layer (M1A); M1B adds the two-point passage FSM v2, the
obstruction-guarded barrier close, CRC-valid NVS occupancy persistence and the
FR-011 serial console, and wires this exact firmware binary into a Wokwi
simulation (`simulation/wokwi/`). The build is green in CI (`firmware-ci`:
compile + native tests + cppcheck); **59/59 native tests pass**.

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
# Wokwi simulation (after `pio run`; details in simulation/wokwi/README.md)
wokwi-cli lint ../../simulation/wokwi --offline
wokwi-cli ../../simulation/wokwi --scenario scenarios/01-boot-console.yaml
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
| `lib/smartpark_core/passage_detector.hpp` | two-point passage FSM v2: ordered confirm, grace/tolerance/re-arm, entry+exit independent (FR-003) | ✅ |
| `lib/smartpark_core/occupancy_manager.hpp` | bounded count, boot `UNKNOWN`, no silent clamping, `restore()`/`isRestored()` (FR-007) | ✅ |
| `lib/smartpark_core/barrier_controller.{hpp,cpp}` | non-blocking gate FSM, obstruction-guarded close (fresh CLEAR every tick), abort/reopen | ✅ |
| `lib/smartpark_core/persistence.{hpp,cpp}` | 16-byte occupancy record: magic/version/CAPACITY/reserved + CRC-32, strict validation order | ✅ |
| `lib/smartpark_core/display_manager.{hpp,cpp}` | LCD 16×2 formatting, priority messages, never overflows | ✅ |
| `lib/smartpark_core/parking_controller.{hpp,cpp}` | orchestration: samples → events → admission → gate policy (+ beam/obstruction feeds) | ✅ |
| `src/pins.hpp` | GPIO map = `docs/hardware/pinmap-v2.md` (M1B: beams 4/16, obstruction 14) | no (hardware) |
| `src/sensor_reader.{hpp,cpp}` | independent TRIG pulse + bounded `pulseIn` per slot | no (hardware) |
| `src/nvs_store.hpp` | `Preferences` wrapper (namespace `smartpark`, key `occ`) | no (hardware) |
| `src/main.cpp` | Arduino glue: setup/loop scheduling, servo/LCD/LED/serial, NVS boot restore, FR-011 console | no (hardware) |
| `test/test_core/test_main.cpp` | 59 Unity tests (41 M1A + 18 M1B) covering all required areas | — |

Rule: **nothing under `lib/` may include Arduino headers.** If it touches a
pin, it belongs in `src/`.

## Behavior changes vs V1 (`legacy/v1/parking_intelligent.ino`)

Safety-relevant (never reproduce V1 behavior at the cost of safety):

1. **Timeouts are `UNKNOWN`, never "free"** (V1 F-03: `999 > 30` reported
   broken sensors as empty zones). A sensor that stops answering escalates
   `UNKNOWN → FAULT` after 10 consecutive timeouts; recovery needs 3 trusted
   samples.
2. **Approach is never counted** (V1 F-02 counted anything within 15 cm).
   The passage FSM v2 confirms only an *ordered* two-point crossing (entry
   E1→E2, exit X1→X2): reversal cancels, a missing/stuck second point can
   never fabricate a count, and with the beams absent the input reads FREE
   forever, so **the occupancy count never changes automatically** — only via
   explicit `reconcile()`. The second points (IR beams on GPIO 4/16) are
   designed and firmware-ready but **not procured** (`pinmap-v2.md` §9/§11);
   the count stays reconciliation-only until they are wired.
3. **Occupancy boots `UNKNOWN`** (V1 F-06 invented `0/6` free spaces on every
   reboot). While unknown, admission is deferred: the gate stays closed and
   the LCD shows `OCC:??? NO ENTRY`. M1B persists the count in NVS: a
   **CRC-valid** 16-byte record restores it (flagged *recovered*, never
   re-verified; `OCCUPANCY_RESTORED` event); a missing/corrupt/out-of-range
   record leaves the state `UNKNOWN` — never clamped, never invented
   (FR-007/NFR-004).
4. **No blind timed close** (V1 F-04: `delay(2000)` then close). Closing
   requires the entry lane to read a *valid* FREE for 3 s **and** a fresh
   `CLEAR` on the obstruction input (GPIO 14) at sweep start *and* on every
   tick of the sweep; any non-FREE lane or non-CLEAR obstruction aborts to
   `BLOCKED →` reopen. The input fails safe (pull-up bias): an open wire
   reads BLOCKED and can never authorize a close (`pinmap-v2.md` §12). The
   lane-guard half is still **not a certified obstruction sensor** (geometry
   unverified) and the obstruction *sensor* itself is planned for M6.
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
13. **FR-011 serial console** (M1B): `R <0..255>` reconcile, `?` one-line
    `STATUS`. The parser is bounded, every outcome is a visible event, and
    the console doubles as the assertion surface for the Wokwi scenarios.

Intentionally *not* changed: capacity 6, 15/30 cm thresholds, servo pins/
pulses (50 Hz, 500–2400 µs, 0–90°), 2.5 s approach debounce, I2C address
0x27, boot splash text semantics (`BOOT - NO COUNT` replaces the 2 s splash).

## Safety posture (M1B)

- Power on with the **gate path clear**: with the obstruction sensor absent,
  GPIO 14 floats HIGH via the pull-up = BLOCKED, so **no auto-close can ever
  happen on the real desk build** until the M6 sensor lands (intended;
  `pinmap-v2.md` §12).
- The gate **fails open** when the lane or the obstruction input cannot be
  trusted (stays open rather than closing on unknown input) and **fails
  closed for admission** (stays shut while occupancy is unknown/full).
- Input fail directions are deliberate and asymmetric: beams open → FREE
  (never count), obstruction open → BLOCKED (never close).
- Simulation honesty: `simulation/wokwi/` executes this firmware in Wokwi —
  that is **simulation evidence only** and backs no electrical or physical
  claim.
- Provisional behaviors are marked as such in code comments and in
  `docs/hardware/pinmap-v2.md`; nothing here is validated on hardware — see
  the checklists in `docs/milestones/m1a.md` and `docs/milestones/m1b.md`.

## Relationship to `legacy/v1/`

- `legacy/v1/parking_intelligent.ino` is the **immutable archive** of the
  original academic sketch (SHA-256 `d5cedf92…0c71ebd`). Never edit it.
- `src/main.cpp` was its verbatim PlatformIO copy during M0; in M1A it was
  replaced by the modular structure above. The original remains byte-exact
  under `legacy/v1/` — that is the rollback path.
