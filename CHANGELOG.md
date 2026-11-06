# Changelog

All notable changes to SmartPark-IoT are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [Semantic Versioning](https://semver.org/) once releases start.

## [Unreleased]

### Added — Milestone M1B (passage v2, persistence, obstruction guard, simulation)

- **Two-point passage FSM v2** (`passage_detector`): confirms only an ordered
  crossing (E1→E2 entry, X1→X2 exit); reversal cancels, gap-crossing and
  primary-loss tolerance are explicit, entry/exit lanes are independent, and a
  missing second point can never fabricate a count (FR-003).
- **Second passage points (IR beams, selected not procured):** E2 → GPIO 4,
  X2 → GPIO 16 in `pins.hpp` / `pinmap-v2.md` §9 (A4 no-PSRAM gate now
  load-bearing); unwired inputs read FREE ⇒ counting stays reconciliation-only
  by design.
- **Obstruction input (GPIO 14) into the barrier FSM** (FR-005, `pinmap-v2.md`
  §12): close needs a **fresh CLEAR** at sweep start *and* on every tick;
  `BLOCKED`/`UNKNOWN` never authorize closing; a mid-sweep violation aborts to
  reopen; fail-safe pull-up bias (open wire = BLOCKED = never closes).
- **NVS occupancy persistence** (FR-007/NFR-004): `persistence.{hpp,cpp}` —
  16-byte record (magic `SPRK`, version, embedded capacity, reserved, CRC-32)
  with strict validation order; `src/nvs_store.hpp` wraps `Preferences`
  (namespace `smartpark`, key `occ`), saved on count change. Boot restore →
  `KNOWN` + `isRestored()` + `OCCUPANCY_RESTORED`; corrupt/out-of-range →
  stays `UNKNOWN`, never clamped.
- **FR-011 serial console** in the glue: `R <0..255>` reconcile, `?` one-line
  `STATUS` (bounded parser, every outcome a visible event).
- **`simulation/wokwi/`** — headless Wokwi project: `wokwi.toml` points at the
  *same* `firmware.bin` the hardware build produces, `diagram.json` (ESP32
  DevKit v1 + 4× HC-SR04 + pushbutton substitutes for the two beams and the
  safety loop, I2C LCD 0x27, servo, 4 LEDs), 3 automation scenarios and a
  README documenting every substitution. `wokwi-cli lint` is **green
  (0 warnings)**; **3/3 scenario runs green** (2026-11-06, real
  `WOKWI_CLI_TOKEN` — results and run-time findings in `m1b.md` §3/§3.2).
- **18 new native tests** (41 → **59**): entry/exit confirmation,
  reversal-cancel, gap crossing, primary-loss tolerance (`UNKNOWN`+`FAULT`),
  faulted secondary, beam debounce/chatter/staleness, simultaneous entry+exit,
  occupancy bounds, obstruction `UNKNOWN`-never-closes / BLOCKED-aborts-close,
  restore + corruption + full persistence validation battery (incl.
  `crc32("123456789")==0xCBF43926`), `millis()` rollover grace, `uint32` seq
  monotonicity over 70k events.

### Changed — Milestone M1B

- `pins.hpp`: GPIO 14 `RESERVED` → obstruction input; GPIO 4 spare → beam E2;
  GPIO 16 adopted for beam X2 (requires PSRAM-less board, assumption A4).
- Obstruction input bias is **pull-up** (`INPUT_PULLUP`): an earlier
  `INPUT_PULLDOWN` in this milestone contradicted the documented fail-safe
  rule ("broken wire reads HIGH = BLOCKED" — a pull-down made an open input
  read CLEAR = unsafe) and was corrected before commit; the board adds an
  external 10 kΩ pull-up on the same rail (no contention).
- Event `seq` widened `uint16` → `uint32` (M2 queue growth); new sensor IDs
  `SENSOR_BEAM_ENTRY=4`, `SENSOR_BEAM_EXIT=5`, `SENSOR_OBSTRUCTION=6`; new
  event `OCCUPANCY_RESTORED`.
- Docs: `pinmap-v2.md` restructured (§9 = IR beams, new §12 = obstruction
  electrical assumptions; §10 checklist extended); `bom.csv` +2 rows (beams,
  pull-ups); requirements FR-003/005/007 + NFR-004 status; architecture
  overview, roadmap, root/firmware READMEs updated.
- CI: **unchanged** — no Wokwi job is added until the repository secret
  `WOKWI_CLI_TOKEN` exists. *(The three existing jobs are green on GitHub for
  this milestone: [run #3](https://github.com/abderaoufsec/SmartPark-IoT/actions/runs/42205158700),
  commit `9fab481`.)*

### Added — Milestone M1A (firmware architecture, safe hardware design, native tests)

- `docs/hardware/pinmap-v2.md` — **provisional** V2 GPIO map and electrical
  design: board assumptions A1–A5, strapping/flash/UART exclusions,
  per-pin conflict check, independent-trigger schedule, 5 V→3.3 V ECHO
  divider (20 k/10 k), servo supply spec, LED/LCD checks, obstruction-
  sensing requirement, pre-power verification gate §10 — **not physically
  tested** — plus the revised bill of materials (`bom.csv`, §11).
- `firmware/esp32/lib/smartpark_core/` — pure-logic core (no Arduino deps,
  no dynamic allocation, C++11): `config`, `event_types`, `event_queue`
  (dedup + sequence numbers), `sensor_filter` (hysteresis, N-stable,
  `UNKNOWN`/`FAULT`, staleness), `passage_detector` (approach FSM),
  `occupancy_manager` (bounded count, boot `UNKNOWN`), `barrier_controller`
  (non-blocking FSM, guarded close, abort/reopen), `display_manager`
  (exactly-16-char LCD lines), `parking_controller` (orchestration).
- `firmware/esp32/src/` — ESP32-only HAL: `pins.hpp` (pinmap mirror),
  `sensor_reader` (independent per-sensor TRIG + bounded `pulseIn`),
  rewritten `main.cpp` (millis()-scheduled glue, no `delay()` in `loop()`).
- `firmware/esp32/test/test_core/` — 41 Unity unit tests covering all M1A
  required areas: thresholds, timeouts, filtering/hysteresis, debounce,
  occupancy bounds, duplicate-event prevention, unknown/fault handling,
  barrier transitions (incl. manual-takeover release), `millis()` rollover,
  entry/exit events and incomplete passages. Runs host-side:
  `pio test -e native`.
- `platformio.ini`: `[env:native]` (`platform = native`, Unity via
  PlatformIO 6.2.0, `-Wall -Wextra -Werror`); `-Wall -Wextra` on `esp32dev`.
- CI (`.github/workflows/firmware-ci.yml`) extended to three jobs: ESP32
  compile, native unit tests, cppcheck (`pio check`, medium gate).
  *(All three green on GitHub: [run #2](https://github.com/abderaoufsec/SmartPark-IoT/actions/runs/40524020932).)*
- `docs/milestones/m1a.md` — M1A status, verification record, 47/0/3
  acceptance checklist, risks, M1B recommendation.
- Architecture doc: firmware module diagram and design rules
  (`docs/architecture/overview.md`).

### Changed — Milestone M1A (behavior changes vs V1, safety-driven)

- **Timeouts are `UNKNOWN`, never free** (V1 F-03); `FAULT` after 10
  consecutive timeouts, recovery needs 3 trusted samples.
- **Approach is never counted** (V1 F-02): the two-point passage FSM cannot
  confirm with the current single-beam wiring → occupancy changes only via
  explicit reconciliation until a second beam exists (documented hardware
  limit, not silent feature loss).
- **Occupancy boots `UNKNOWN` and defers admission** (V1 F-06 invented free
  spaces): gate stays closed, LCD shows `OCC:??? NO ENTRY` (NFR-004;
  persistence = M1B).
- **No blind timed close** (V1 F-04): close requires 3 s of observed valid
  FREE lane; any non-FREE reading during the sweep aborts and reopens
  (`BLOCKED`). Provisional — no dedicated obstruction sensor yet (H-08).
- **Independent staggered triggers** (V1 F-01/H-01): GPIO 3/UART0 freed;
  echoes moved to input-only 34/35/36/39 (H-03).
- **No `delay()` in `loop()`** (V1 F-05); LCD/LEDs refresh on 500 ms
  schedule, 2 s boot splash removed.
- Unified hysteresis thresholds 15/30 cm + 3-stable filtering (F-08);
  LCD lines always exactly 16 chars (F-07); visible `ENTRY: FULL STOP`
  refusal + single deduplicated `ADMISSION_DENIED` event (F-09); occupancy
  bounds reject with `OCCUPANCY_CHANGE_REJECTED` instead of clamping (F-10).
- `docs/hardware/v1-audit.md` gained §6 resolution status (findings
  updated, never deleted); requirements/roadmap status columns updated
  (requirement text unchanged).

### Fixed — Milestone M1A (defects found by the final line-by-line error hunt)

- **Barrier MANUAL release could report the wrong physical position**
  (`barrier_controller`): `exitManual()` set `CLOSED`/`OPEN` purely from the
  target angle, so a release mid-sweep left the FSM claiming `CLOSED` while
  the servo still held a mid-position (and `tick()` in `CLOSED` never
  corrected it). `exitManual(nowMs)` now resolves by *actual* angle — settled
  positions map directly, a mid-sweep release resumes the **guarded** close
  (unknown/occupied lane ⇒ `BLOCKED` → reopen, fail open).
- **Stale automated intents could survive manual takeover / fault latch**:
  `setManual()`, `exitManual()` and `setActuatorFault()` now clear
  `pendingClose_`/`abortReopen_`; previously a stale abort flag could make a
  later, legitimate `BLOCKED` self-reopen on the next tick without an abort.
- **`abortCount()` wrapped at 256** — now saturates at 255 (diagnostics).
- **Rollover test loop defect** (`test_lane_clear_timer_survives_millis_rollover`):
  the loop bound `t != t0` on a 15 ms grid never lands on `t0` — it only
  terminated after orbiting the whole `uint32` space (1 431 655 811 ticks,
  ~6.5 s of CPU per run) and passing "by accident". Bounded by value now, with
  an explicit OPEN assertion; suite time 8.5 s → 1.9 s.
- **`ControllerView::lastEventSeq` comment** contradicted the value returned
  (total events enqueued / next seq, not "newest seq") — comment corrected.

### Added — Milestone M0 (repository modernization)

- `legacy/v1/` — immutable archive of the original academic project:
  - `parking_intelligent.ino` (byte-identical to upstream commit `d566ec4`,
    SHA-256 `d5cedf92…0c71ebd`)
  - `original-report.pdf` (original name:
    `Smart_Parking_System_avec_Guidage_Directionnel (1).pdf`,
    SHA-256 `ab191341…49e29`)
  - `README.md` with provenance, baseline commit and checksums
- `firmware/esp32/` — PlatformIO project compiling the V1 logic unchanged:
  - `platformio.ini` with pinned `espressif32@6.13.0`,
    `ESP32Servo@1.2.1`, `LiquidCrystal_I2C@1.1.4`
  - `src/main.cpp` — V1 source plus `#include <Arduino.h>` (see firmware README)
- `docs/`: architecture overview, requirements (FR/NFR IDs), hardware/V1 audit
  with firmware bugs and electrical risks, M0–M10 roadmap, M0 status & acceptance
  checklist, baseline decision record (ADR-001), preserved project blueprint
- Root hygiene: `.gitignore`, `.editorconfig`, `.gitattributes`,
  `.env.example` (future-service placeholders, M2+)
- Community files: `CONTRIBUTING.md`, `SECURITY.md`, this `CHANGELOG.md`
- `LICENSE` — MIT, selected by maintainer decision during M0 review
  (2026-10-27); copyright credits Mehdi Koceir, abderaoufsec and contributors
- CI: `.github/workflows/firmware-ci.yml` — PlatformIO compile on push/PR
- PR template: `.github/PULL_REQUEST_TEMPLATE.md`

### Changed

- `README.md` rewritten to reflect reality: honest feature-status table
  (implemented / baseline / planned), real repository structure, accurate
  pin table with warnings, PlatformIO quick start, documentation index.

### Removed

- `pasted_content_19.txt` — verified **byte-identical duplicate** of
  `parking_intelligent.ino` (same SHA-256 `d5cedf92…0c71ebd`); recoverable via
  `git show 388ead1:pasted_content_19.txt`.

### Open items (explicitly pending)

- ~~**M1B: Wokwi scenario execution**~~ — **resolved 2026-11-06**: the token
  was provided, all three scenarios ran green, and results plus the two
  run-time findings (S-1 YAML escape, S-2 button transient) are recorded in
  `m1b.md` §3, §3.2 and §5 row 6.4. Remaining: the *CI* Wokwi job still needs
  the `WOKWI_CLI_TOKEN` repository secret (row 6.5, intentionally absent).
- Branch strategy beyond M0 (`develop` + feature branches) not yet activated —
  M0 works directly on `main` as agreed.
- Confirm the MIT license choice with original author Mehdi Koceir before the
  first tagged release (he is credited in `LICENSE`).

## [Historical] — V1 academic prototype (before SmartPark-IoT)

The original project by Mehdi Koceir (upstream:
[MehdiKoceir/Parking-intelligent-](https://github.com/MehdiKoceir/Parking-intelligent-))
is preserved at commit `d566ec4e7529208aba63a7ea4ec09bb5d546878c`
(`Initial commit` → `Update README with integration and author section`).
It predates this changelog; its content is archived verbatim under `legacy/v1/`.
