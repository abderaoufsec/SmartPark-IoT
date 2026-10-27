# M1A — Firmware architecture, safe hardware design, native unit testing

**Status: complete (local verification), pending review/commit** · date:
2026-10-27 · branch: `main` (no branches created, nothing committed or pushed
per milestone instructions) · supersedes nothing; follows [m0.md](m0.md).

Scope per the M1A task brief and [roadmap](roadmap.md) M1 steps 1–6 (step 7
persistence = M1B). Nothing under `legacy/v1/` was modified (hashes re-verified
below).

---

## 1. What was delivered

| Task | Deliverable |
|---|---|
| 1 — safe hardware design | [`docs/hardware/pinmap-v2.md`](../hardware/pinmap-v2.md) (provisional GPIO map, cross-talk schedule, 5 V→3.3 V dividers, servo power, LED/LCD checks, obstruction-sensing requirement, pre-power gate §10) + revised BOM (`bom.csv` + §11) |
| 2 — firmware refactor | monolith → `lib/smartpark_core/` (9 pure modules) + `src/` HAL (`pins.hpp`, `sensor_reader`, `main.cpp`); see [firmware README](../../firmware/esp32/README.md) |
| 3 — native unit tests | `[env:native]` + Unity suite `test/test_core/test_main.cpp` — **41 tests** |
| 4 — CI | `firmware-ci.yml` now 3 jobs: ESP32 compile, native tests, cppcheck |
| 5 — documentation | firmware README rewrite, `pinmap-v2.md` + audit §6, architecture firmware section, this checklist, changelog, requirements/roadmap status |

Module layout (task 2): `config`, `event_types`, `event_queue`,
`sensor_filter`, `passage_detector`, `occupancy_manager`,
`barrier_controller`, `display_manager`, `parking_controller` +
ESP32-only `sensor_reader`/`pins`/`main`. The suggested module list was
followed; **`passage_detector` was added** because FR-003's two-point contract
is a distinct state machine from occupancy accounting — merging it into
`parking_controller` would have buried the safety-critical transition logic.

## 2. Behavior changes vs V1 (why things are "broken")

Full list with V1 finding references in
[firmware README §"Behavior changes"](../../firmware/esp32/README.md).
The changes a V1 user will *notice* on the desk:

1. **The counter never moves by itself.** No second sensing point exists, so
   approach can never confirm passage (FR-003); occupancy changes only via
   explicit reconciliation. V1 counted every approach — that was the bug.
2. **The gate mostly stays closed.** Boot leaves occupancy `UNKNOWN`
   (persistence is M1B, NFR-004 forbids inventing free spaces), and
   `UNKNOWN` defers admission (FR-004). LCD shows `OCC:??? NO ENTRY`.
   This is the intended safe state, not a regression.
3. **The gate will not blind-close.** Closing needs 3 s of *observed* valid
   FREE lane and aborts/reopens on anything else — provisional until the
   obstruction sensor exists (pinmap §9).
4. **Zone LEDs blink** when a sensor is `UNKNOWN`/`FAULT` instead of reading
   as "empty".
5. Boot splash no longer blocks 2 s; LCD/LEDs refresh on a 500 ms scheduler.

## 3. Verification record (commands actually run)

| Command | Result |
|---|---|
| `pio run -d firmware/esp32 -e esp32dev` (clean rebuild) | **SUCCESS**, exit 0, **0 compiler warnings** with `-Wall -Wextra`, RAM 6.9% (22 460 B), Flash 23.2% (304 261 B) |
| `pio test -d firmware/esp32 -e native` | **PASSED — 41/41 tests**, exit 0, suite 1.9 s |
| `pio check -d firmware/esp32 -e native --src-filters=+<lib/…>+<src/…> … --fail-on-defect=medium` | **PASSED**, 0 high / 0 medium / 1 low (documented false positive, disproven by `test_filter_reports_state_changes`) |
| YAML parse of `.github/workflows/firmware-ci.yml` | OK |
| grep: Arduino API inside `lib/smartpark_core/` | none (comments only) |
| grep: `delay(` calls in `src/`+`lib/` | none (protocol `delayMicroseconds` for the 10 µs TRIG pulse remains — hardware requirement, not app scheduling) |
| grep: `new`/`malloc`/`String`/`std::string` in `lib/` | none — fixed-size arrays only |
| `Get-FileHash` of `legacy/v1/*` | unchanged: ino `D5CEDF92…0c71ebd`, pdf `ab191341…49e29` |
| `git branch -a` | only `main` (+ origin/main) |

**Local toolchain note (honest record):** the pre-existing MSYS2 `g++` on this
machine is broken (`cc1plus.exe` fails with `0xC0000139`, orphaned runtime DLLs
in the pacman DB) and could not compile anything before this milestone's tests.
Local native runs therefore used WinLibs GCC 16.2.0 (installed for M1A); CI
uses the preinstalled `g++` on `ubuntu-latest`. The host compiler is an
environment dependency — see risks.

### 3.1 Final line-by-line error-hunt sweep (before sign-off)

Every `lib/`, `src/`, test, workflow and doc file was re-reviewed against the
requirements; the suite was re-run clean. Defects found and fixed (details in
`CHANGELOG.md` → "Fixed — Milestone M1A"):

| # | Defect | Severity | Fix + regression test |
|---|---|---|---|
| E-1 | `exitManual()` could report `CLOSED` while the servo held a mid-sweep angle (state/angle mismatch; `tick()` never corrected it) | latent safety | angle-based resolution + guarded close; `test_barrier_manual_release_mid_sweep_*` |
| E-2 | `pendingClose_`/`abortReopen_` survived `setManual()`/`setActuatorFault()` — a stale abort flag made a later legitimate `BLOCKED` self-reopen instantly | latent safety | intents cleared on takeover/fault; `test_barrier_takeover_clears_stale_abort_reopen` |
| E-3 | `abortCount()` wrapped 255→0 (diagnostic counter) | low | saturating increment |
| E-4 | Rollover test loop bounded by `t != t0` on a 15 ms grid: needed 1 431 655 811 ticks (full `uint32` orbit) to exit — passed only after ~6.5 s of CPU | test defect | value-bounded loop + explicit OPEN assert; suite 8.5 s → 1.9 s |
| E-5 | `lastEventSeq` doc comment said "newest seq" but the field is "total enqueued" | doc | comment corrected |

**Verified non-defects** (reviewed, intentionally unchanged): the ESP32
`pulseIn()` core returns `0` on timeout for *all* three wait phases under one
cumulative budget — so the documented 30 ms bound is exact and no fabricated
distance is possible; the `CLOSING` sweep's `int32` math cannot overflow for
any `uint32` elapsed time; queue dedup-vs-tail-only and single-shot admission
decisions are documented policies, not bugs.

## 4. Safety limitations that remain (do not skip)

- **No physical wiring exists for this design.** `pinmap-v2.md` is
  PROVISIONAL; §10's checklist gates first power-up. Nothing was measured,
  simulated or bench-tested this milestone.
- **No obstruction sensor** → guarded close is provisional, supervised desk
  model only; entry-beam geometry unverified (H-08 open).
- **No second passage beam** → automatic counting is disabled by design
  (FR-003 incomplete until hardware exists).
- **No persistence** → every boot is `UNKNOWN` until reconciliation (FR-007
  open, M1B).
- **Thresholds (15/30 cm) are inherited guesses** — calibration is M6.
- `MANUAL` barrier state exists in the FSM but no M1A code path can reach it
  (no manual input hardware; remote control explicitly out of scope).

## 5. M1A acceptance checklist

Legend: PASS = implemented **and** verified by the evidence named ·
FAIL = broken · NOT TESTED = cannot be verified without hardware/GitHub/push.

| # | Item | Status | Evidence |
|---|---|---|---|
| **Task 1 — safe hardware design** |
| 1.1 | `docs/hardware/pinmap-v2.md` exists; board assumptions stated & design marked provisional | PASS | doc §1, status banner |
| 1.2 | GPIO 1/3, flash 6–11, strapping 0/2/5/12/15, PSRAM 16/17 avoided | PASS | doc §2/§3, pin table with per-pin conflict check |
| 1.3 | 4× HC-SR04 + 1× SG90 + 4× LEDs + I2C LCD all mapped with rationale | PASS | doc §3 (15 GPIO used, fits 30/38-pin DevKit) |
| 1.4 | Independent triggering schedule (no simultaneous fire) | PASS (design) | doc §4 + `sensor_reader`/`slotFor` code |
| 1.5 | 5 V→3.3 V echo conversion specified with values & alternatives | PASS (design) | doc §5 (20 k/10 k divider, ≈3.33 V) |
| 1.6 | Servo power, common ground, decoupling, LED resistors, LCD rail, obstruction requirements documented | PASS (design) | doc §6–§9 + BOM §11/`bom.csv` |
| 1.7 | Pin availability, electrical limits, peripheral conflicts checked | PASS | doc §2/§3 conflict table |
| 1.8 | Pre-wiring verification gate; physical test explicitly **not** claimed | PASS | doc §10 (all items unchecked) + banner |
| 1.9 | Revised bill of materials (present / required / planned-not-available) | PASS | `bom.csv` + doc §11 |
| **Task 2 — firmware architecture** |
| 2.1 | Monolith split into small modules (config/sensor_reader/sensor_filter/occupancy/barrier/parking/display/event_types) | PASS | tree in firmware README |
| 2.2 | Business logic separated from Arduino I/O | PASS | grep: no Arduino API in `lib/`; native build compiles core alone |
| 2.3 | Deterministic, testable state machines | PASS | filter/passage/barrier FSMs take `nowMs` as input; 41 tests |
| 2.4 | No unnecessary dynamic allocation | PASS | grep: no `new`/`malloc`/`String`; fixed ring buffer + arrays |
| 2.5 | Blocking application delays replaced by `millis()` scheduling | PASS | grep: no `delay()` in `src/`/`lib/`; bounded 30 ms `pulseIn` documented (pinmap §4) |
| 2.6 | Explicit sensor states incl. `FAULT` | PASS | `sensor_filter.hpp` + tests 1/2 |
| 2.7 | Timeout ⇒ `UNKNOWN`, never auto-`FREE` | PASS | `test_timeout_is_unknown_never_free` |
| 2.8 | Hysteresis + stable-sample filtering + named config constants | PASS | `config.hpp`, `test_hysteresis_*`, `test_noise_does_not_flip_state` |
| 2.9 | Bounded occupancy counts, no silent clamping | PASS | `test_occupancy_reconcile_bounds`, `test_occupancy_change_bounds` |
| 2.10 | Explicit barrier states (incl. BLOCKED/FAULT/MANUAL) | PASS | `barrier_controller.hpp` + transitions tests |
| 2.11 | Approach never interpreted as passage; no invented confirmation mechanism | PASS | `test_single_beam_never_reaches_confirmed`, `test_entry_approach_opens_gate_but_never_counts` |
| 2.12 | Passage-confirmation limitation documented + safe provisional behavior | PASS | pinmap §9/§11, firmware README, audit §6 F-02/F-04 rows |
| 2.13 | No automatic close on a bare fixed delay | PASS | `test_barrier_close_needs_sustained_valid_free` (observed-valid-FREE window + mid-sweep abort) |
| 2.14 | No remote barrier control introduced | PASS | no network/remote code in firmware; search of `src/`/`lib/` |
| 2.15 | Hardware-dependent behavior disabled/marked when signals unavailable | PASS | admission defers on `UNKNOWN` (`test_admission_gates_on_occupancy`); `MANUAL` unreachable by design; guarded close marked provisional |
| **Task 3 — native unit tests** (run without an ESP32) |
| 3.1 | `[env:native]` configured, pinned via PlatformIO 6.2.0 | PASS | `platformio.ini` |
| 3.2 | Sensor threshold boundaries | PASS | `test_fresh_classification_boundaries` |
| 3.3 | Sensor timeout / invalid readings | PASS | `test_timeout_is_unknown_never_free` |
| 3.4 | Filtering + hysteresis | PASS | `test_noise_does_not_flip_state`, `test_filter_reports_state_changes` |
| 3.5 | Repeated detection + debounce | PASS | `test_approach_debounce_blocks_retrigger` |
| 3.6 | Occupancy lower/upper bounds | PASS | `test_occupancy_reconcile_bounds`, `test_occupancy_change_bounds` |
| 3.7 | Duplicate event prevention | PASS | `test_event_queue_duplicate_rejected`, `test_controller_denial_emitted_once` |
| 3.8 | Unknown/fault handling | PASS | `test_fault_escalation_and_recovery`, `test_unknown_lane_aborts_closing_sweep`, `test_admission_gates_on_occupancy` |
| 3.9 | Barrier state transitions | PASS | 11 barrier tests incl. abort/reopen, `BLOCKED`, `FAULT`, `MANUAL` + mid-sweep release regression tests |
| 3.10 | Nonblocking timers incl. `millis()` rollover | PASS | `test_barrier_sweep_*_rollover`, `test_lane_clear_timer_*_rollover`, `test_sensor_stale_*_rollover`, `test_slot_for_*_rollover` |
| 3.11 | Entry/exit events + incomplete passage sequences | PASS | `test_approach_then_cancel_never_confirms`, `test_exit_approach_never_touches_gate`, `test_full_cycle_*` |
| 3.12 | Whole suite green | PASS | `pio test -e native` → **41/41** |
| **Task 4 — CI** |
| 4.1 | Job 1: ESP32 firmware compilation | PASS (local) / NOT TESTED (on GitHub) | identical command run locally (green); workflow not yet pushed |
| 4.2 | Job 2: native C++ unit tests | PASS (local) / NOT TESTED (on GitHub) | same suite green locally; CI execution pending push |
| 4.3 | Job 3: static checks (cppcheck, medium gate) | PASS (local) / NOT TESTED (on GitHub) | `pio check` green locally; workflow pending push |
| 4.4 | Dependencies pinned; no extra services | PASS | `platformio==6.2.0`, `espressif32@6.13.0`, Unity/cppcheck resolved by that core; YAML parses |
| **Task 5 — documentation** |
| 5.1 | Firmware README rewritten (structure, commands, behavior changes) | PASS | `firmware/esp32/README.md` |
| 5.2 | Hardware safety documentation | PASS | `pinmap-v2.md`, `bom.csv`, audit §6 |
| 5.3 | Architecture documentation updated | PASS | `docs/architecture/overview.md` §Firmware architecture |
| 5.4 | M1A checklist (this file) | PASS | — |
| 5.5 | Changelog updated | PASS | `CHANGELOG.md` → M1A section |
| 5.6 | Requirements/roadmap statuses updated without changing the requirements themselves | PASS | status columns only; FR/NFR text untouched |
| **Milestone hygiene** |
| H.1 | Work only on `main`, no branches | PASS | `git branch -a` |
| H.2 | No commit / no push | PASS | working tree modified, 0 new commits (baseline still `f0830f0`) |
| H.3 | `legacy/v1/` untouched | PASS | SHA-256 re-verified |
| H.4 | No MQTT/backend/frontend/Flutter/AI/payments code | PASS | changed paths: `firmware/**`, `docs/**`, `.github/workflows` only |
| H.5 | No fabricated tests/screenshots/hardware results | PASS | every claim above maps to a command run this session |
| H.6 | Build actually compiles (never claimed without a build) | PASS | clean `pio run` log, exit 0, 0 warnings |
| H.7 | Requirements not silently changed | PASS | requirements.md status column edits only |

**Totals: 47 PASS · 0 FAIL · 3 NOT TESTED** (the three CI-execution rows,
untestable until the workflow is pushed to GitHub).

## 6. Known risks carried into M1B

1. Host compiler is environment-dependent (MSYS2 g++ on this machine was
   broken; CI uses ubuntu g++) — a native-test failure on a contributor's
   machine may be toolchain, not code.
2. `pinmap-v2.md` board assumptions (A1–A5) unverified — a non-classic ESP32
   board invalidates the map.
3. Single LOW cppcheck finding is suppressed only by severity policy, not by
   suppression directive — if cppcheck's analysis improves, it may re-flag
   `SensorFilter::update`'s return (disproven by test).
4. Deferred admission makes the desk demo look "inert" until M1B persistence
   lands — expected, but reviewers should not mistake it for a defect.

## 7. M1B recommendation — Wokwi simulation + reliable passage detection

Scope proposal (blueprint M1 continuation; still no MQTT):

1. **Wokwi harness** for the `pinmap-v2` wiring (4× HC-SR04, SG90, 4 LEDs,
   16×2 I2C LCD) driven by the *existing* firmware — first reproducible
   execution evidence beyond compilation; record the config in-repo (fixes
   D-03 honestly this time). Simulation also exercises the approach/cancel
   flow end-to-end where desk hardware cannot yet.
2. **Passage detection hardware plan**: specify + wire a *second* sensing
   point per direction (two-beam sequence or IR break-beam), replay the
   blueprint's stop/reverse/queue scenarios (research Q1), then enable
   `PASSAGE_CONFIRMED_*` → automatic counting behind a config flag. Until
   that hardware exists, keep reconciliation-only counting.
3. **Persistence (FR-007/NFR-004)**: NVS-backed occupancy + boot reconcile,
   freeing the demo from `UNKNOWN`-forever; then a minimal serial console on
   the now-free UART0 RX for local reconciliation (FR-011 groundwork).
4. **Obstruction sensor selection** (H-08): pick the part, reserve GPIO 14
   wiring, define the reaction-time test — enables replacing the provisional
   guarded close with FR-005-grade protection (or keep M6 for the physical
   test).
5. **CI follow-up**: confirm the three jobs actually run green on GitHub once
   pushed, and update §5 rows 4.1–4.3 from NOT TESTED to PASS with run links.
