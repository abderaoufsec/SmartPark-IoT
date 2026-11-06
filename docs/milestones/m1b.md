# M1B — Two-point passage detection, NVS persistence, obstruction safety, simulation

**Status: complete — committed, pushed, CI-green, Wokwi scenarios 3/3 green** ·
date: 2026-11-06 · branch: `main` (no branches created; commit `9fab481`
pushed with maintainer approval, [CI run #3](https://github.com/abderaoufsec/SmartPark-IoT/actions/runs/42205158700)
all 3 jobs green; all 3 Wokwi scenarios executed green locally on 2026-11-06 —
§3/§5 row 6.4) · follows [m1a.md](m1a.md), supersedes nothing.

Scope per the M1B task brief and [roadmap](roadmap.md) M1 steps 5–8: reliable
two-point passage detection, persistence, barrier obstruction safety, extended
native tests, Wokwi simulation and docs/CI. Nothing under `legacy/v1/` was
modified (hashes re-verified in §3). No MQTT / backend / frontend / Flutter /
AI / payments code was added. **CI is unchanged** (rationale in §5, row 6.5).

---

## 1. What was delivered

| Task | Deliverable |
|---|---|
| 1 — passage detection v2 | `passage_detector` two-point FSM **v2**: confirm only on an ordered second-point hit (or second point free at approach start), reversal cancels, gap-crossing within grace, primary-loss tolerance (`UNKNOWN`/`FAULT`), entry/exit lanes independent. Second points = **IR beams on GPIO 4 (E2) / GPIO 16 (X2)** (`pins.hpp`, [`pinmap-v2.md` §9](../hardware/pinmap-v2.md)) |
| 2 — obstruction safety | `ObstructionState` input (**GPIO 14**) into `barrier_controller`: fresh `CLEAR` required at sweep start **and on every tick**, `UNKNOWN`/`BLOCKED` never authorize a close, mid-sweep violation aborts to reopen; fail-safe **pull-up** bias ([`pinmap-v2.md` §12](../hardware/pinmap-v2.md)) |
| 3 — persistence (FR-007/NFR-004) | `persistence.{hpp,cpp}` — 16-byte record (magic `SPRK`, version, embedded capacity, reserved, CRC-32, strict validation order) + `src/nvs_store.hpp` (`Preferences`, ns `smartpark`, key `occ`); save on count change; boot restore → `KNOWN` + `isRestored()` + `OCCUPANCY_RESTORED`; invalid record → stays `UNKNOWN`, never clamped |
| 4 — console (FR-011 groundwork) | bounded serial console in the glue: `R <0..255>` reconcile, `?` one-line `STATUS` — every outcome is a visible event; doubles as the Wokwi assertion surface |
| 5 — native tests | 41 → **59** tests (18 new); all 41 M1A tests preserved byte-for-byte |
| 6 — Wokwi simulation | [`simulation/wokwi/`](../../simulation/wokwi/): `wokwi.toml` (runs the *same* `firmware.bin`), `diagram.json` (ESP32 DevKit v1 + 4× HC-SR04 + pushbutton substitutes for both beams and the safety loop, I2C LCD 0x27, servo, 4 LEDs), **3 automation scenarios**, README documenting every substitution. `wokwi-cli lint` **green**; **3/3 scenario runs green** (2026-11-06, real token, §3) |
| 7 — documentation | pinmap §9 (beams) + new §12 (obstruction electrical) + §10 checklist + BOM rows; requirements FR-003/005/007 + NFR-004; roadmap, architecture overview, root + firmware READMEs, changelog (this file) |

Design decision worth recording: the **fail directions are asymmetric on
purpose** — an open beam input fails toward FREE (never fabricates a count),
an open obstruction input fails toward BLOCKED (never authorizes a close).
Each subsystem fails toward *its* safe action (§12.1 rule 7).

## 2. Behavior changes a V1/M1A user will notice

1. **Reboot no longer forgets.** A CRC-valid NVS record restores the exact
   count (flagged *recovered*, `OCCUPANCY_RESTORED`); a missing/corrupt record
   still boots `UNKNOWN` — never invented (FR-007/NFR-004).
2. **The counter can move again — but only on an ordered crossing.** With the
   second points wired, E1→E2 (or X1→X2) confirms; approach alone, reversal,
   chatter, a stuck beam or a dead sensor never counts. With the beams *absent*
   (today's desk reality) the inputs read FREE and counting stays
   reconciliation-only — the safe M1A default, now by §9 design.
3. **The gate is harder to close.** Closing needs the lane observed FREE for
   3 s **and** a fresh `CLEAR` on GPIO 14 at sweep start and every tick; a
   mid-sweep BLOCKED aborts and reopens. With the obstruction sensor absent,
   the pull-up reads BLOCKED forever → **no auto-close happens physically**
   until the M6 sensor lands (intended fail-safe, §12.2).
4. **Local console:** `?` prints `STATUS occ=… count=… free=… gate=…
   obstruction=…`; `R n` reconciles with a visible event.

## 3. Verification record (commands actually run, this session)

| Command | Result |
|---|---|
| `pio run -d firmware/esp32 -e esp32dev` | **SUCCESS**, exit 0, **0 warnings** (`-Wall -Wextra`), RAM 6.9% (22 628 B), Flash 24.1% (316 025 B) |
| `pio test -d firmware/esp32 -e native` | **PASSED — 59/59**, exit 0, suite 2.49 s |
| `pio check -d firmware/esp32 --fail-on-defect=medium` | **PASSED**, 0 high / 0 medium / **3 low** — identical to the M1A baseline (library `missingOverride` + Arduino `setup`/`loop` `unusedFunction`) |
| GitHub Actions [run #3](https://github.com/abderaoufsec/SmartPark-IoT/actions/runs/42205158700) for `9fab481` | **SUCCESS — all 3 jobs green** (ESP32 compile 52 s, native tests 32 s on ubuntu g++ = 59/59, cppcheck 20 s) |
| `wokwi-cli lint simulation/wokwi --offline --warnings-as-errors` | **exit 0** — 0 warnings, 1 info (`wokwi-esp32-devkit-v1` is an undocumented part type; expected) |
| `wokwi-cli --scenario …` (3 scenarios, `--timeout 60000` for 03) | **3/3 PASSED — `Scenario completed successfully`, exit 0 each** (wokwi-cli 0.29.1, 2026-11-06, real `WOKWI_CLI_TOKEN`): 01 boot/console (reconcile + rejection + usage), 02 entry passage (approach never counts → confirm → guarded close), 03 obstruction guard (BLOCKED at boot → abort mid-sweep → auto-retry close). Key events quoted in §3.2/§5 row 6.4 |
| `git branch -a` | only `main` (+ `origin/main`) |
| `git rev-parse HEAD` / `git ls-remote --heads origin` | both `d35ad1d…` — 0 new commits, work uncommitted |
| `git status --porcelain` | `firmware/**`, `docs/**`, root `README.md`/`CHANGELOG.md`, new `simulation/` — no out-of-scope paths (one stray root file, see §6.6) |
| `Get-FileHash legacy/v1/*` | unchanged: ino `D5CEDF92…0c71ebd`, pdf `AB191341…49e29` |

**Local toolchain note (carried from M1A):** native tests ran with WinLibs GCC
16.2.0 (the machine's MSYS2 `g++` remains broken); CI uses `ubuntu-latest`.

### 3.1 Safety-consistency finding fixed during M1B (do not skip)

| # | Finding | Fix |
|---|---|---|
| B-1 | The obstruction input was first coded `INPUT_PULLDOWN` while its own documented fail-safe rule says *broken wire reads HIGH = BLOCKED*. With a pull-down, an open input reads **CLEAR** → close authorized = **unsafe**, and it would contend with the planned external pull-up (divider ≈ 2.7 V, component-dependent margin). | Changed to `INPUT_PULLUP` (internal + board 10 kΩ external, same rail, no contention). The polarity contract (HIGH = BLOCKED, LOW = CLEAR) is unchanged; §12.1 records the rule and the deliberate asymmetry with the beams. Caught by line review, fixed before this report; regression coverage: `test_obstruction_*` + Wokwi scenario 03 (run green 2026-11-06). |

### 3.2 Findings from the first real scenario execution (2026-11-06)

Running the scenarios for the first time surfaced two issues. Both were
resolved without touching safety logic; neither is claimed away.

| # | Finding | Resolution |
|---|---|---|
| S-1 | `write-serial: 'R 2\n'` used YAML **single quotes**, which do not process escapes: the console received the literal characters `R 2\n` (backslash + `n`, no newline), the line-terminated protocol never fired, and every console wait timed out (exit 42) — first run failed, and so did a delay-corrected retry. | All `write-serial` steps rewritten with **double quotes** (`"R 2\n"`) so `\n` becomes a real newline byte. The firmware console was never at fault: an `--interactive` piped-input run (real newlines) proved reconcile/STATUS/rejection/usage all correct *before* the scenarios were fixed. |
| S-2 | Every `btn_obs` transition shows a **1–12 ms opposite-level transient** in the simulator (`sensor=6`: 0→1→0 on press). In scenario 03 the first recovery sweep therefore hit `BLOCKED` **mid-sweep**, aborted to state 4 and reopened, then auto-retry closed the gate ~3.7 s later (`26887 seq=39 BARRIER_STATE_CHANGED value=0`). | Left in place **on purpose**: abort-on-blocked-mid-sweep is exactly FR-005's fail-safe design, and the run proves it end-to-end in the real simulation. Scenario 03 now samples its final STATUS after the same 6 s close budget as the other steps. The transient itself is *not* asserted as required behavior — a simulator artifact must not become a spec. |

## 4. Safety limitations that remain (do not skip)

- **Nothing physical changed.** `pinmap-v2.md` stays PROVISIONAL; §10's
  checklist gates first power-up. No wiring, measurement or bench test exists.
- **Obstruction sensor absent** (BOM: planned M6) → the real build never
  auto-closes (fail-safe); the lane-guarded close remains defence in depth and
  is *not* a certified obstruction sensor (geometry unverified, H-08 open).
- **Beams not procured** (BOM: selected M1B) → automatic counting stays
  disabled on hardware until E2/X2 exist (FR-003 hardware half open).
- **Wokwi evidence is scoped, not blanket.** All three scenarios ran green
  (2026-11-06, §3) — but that is *simulated* evidence against Wokwi's part
  models with substituted buttons: it does not validate wiring, electricity or
  real timing (the simulation README's scope note stands).
- **GPIO 16 (X2) makes assumption A4 load-bearing** — a PSRAM (WROVER) board
  invalidates the pin map; unverified (§10).
- **Thresholds/timings are inherited guesses** (15/30 cm, 3 s grace, 3 s lane
  window); the FR-005 **< 200 ms** reaction figure is unmeasured (§12.3, M6).

## 5. M1B acceptance checklist

Legend: PASS = implemented **and** verified by the evidence named ·
FAIL = broken · NOT TESTED = cannot be verified without hardware (or, for
row 6.5, until the CI repository secret exists). Rows marked *design*/*code*
are explicitly not execution evidence.

| # | Item | Status | Evidence |
|---|---|---|---|
| **Task 1 — reliable two-point passage detection (FR-003)** |
| 1.1 | Second points assigned: E2 → GPIO 4, X2 → GPIO 16 (non-strapping; A4 documented for 16) | PASS | `pins.hpp`, `pinmap-v2.md` §3/§9 |
| 1.2 | Confirm only on ordered second point (or free-at-approach-start); approach alone never counts | PASS | `test_successful_entry_confirms_and_counts`, `test_successful_exit_confirms_and_counts`, `test_confirmed_requires_ordered_second_point`, `test_single_beam_never_reaches_confirmed` |
| 1.3 | Reversal cancels; gap-crossing confirms within grace; primary-loss tolerance `UNKNOWN`+`FAULT`; faulted secondary never confirms | PASS | `test_reversal_cancels_after_grace_without_confirm`, `test_gap_crossing_confirms_within_grace`, `test_primary_sensor_loss_cancels_after_tolerance`, `test_faulted_secondary_never_confirms` |
| 1.4 | Entry/exit FSMs independent, simultaneous crossings counted once each | PASS | `test_simultaneous_entry_and_exit`, `test_successful_entry/exit_*` |
| 1.5 | Beam 50 ms debounce, chatter never double-confirms, stale beam ⇒ `UNKNOWN` (never free) | PASS | `test_beam_debounce_rejects_short_glitches`, `test_beam_chatter_never_double_confirms`, `test_beam_stale_becomes_unknown_never_free` |
| 1.6 | Grace/tolerance/rollover timing wrap-safe; occupancy bounds still hold on confirm | PASS | `test_passage_grace_survives_millis_rollover`, `test_confirmed_passage_respects_occupancy_bounds` |
| 1.7 | Unwired beam fails toward FREE = never confirms; documented fail direction | PASS (design) | `pinmap-v2.md` §9, firmware README item 2 |
| 1.8 | Physical IR beam modules wired/procured | **NOT TESTED** | BOM: *selected M1B, not procured* — hardware = M6 |
| **Task 2 — obstruction safety (FR-005, pinmap §12)** |
| 2.1 | GPIO 14 input, HIGH = BLOCKED / LOW = CLEAR, fail-safe pull-up bias (internal + external 10 kΩ) | PASS (design) | `main.cpp`, `pins.hpp`, `pinmap-v2.md` §12.1 |
| 2.2 | Close needs fresh `CLEAR` at start **and** every tick; `UNKNOWN` never closes; `BLOCKED` mid-sweep aborts → reopen | PASS | `test_obstruction_unknown_never_authorizes_close`, `test_obstruction_blocks_explicit_close_intent`, barrier suite (48 `tick()` sites feed explicit `ObstructionState`) |
| 2.3 | Fail direction documented and asymmetric with beams (open → BLOCKED here, FREE there) | PASS | `pinmap-v2.md` §12.1 rule 7, `pins.hpp` comment |
| 2.4 | Physical obstruction sensor present | **NOT TESTED** | BOM: planned M6; unwired input = never closes (documented, §12.2) |
| 2.5 | FR-005 reaction < 200 ms measured end-to-end | **NOT TESTED** | `pinmap-v2.md` §12.3 — requires sensor + scope (M6) |
| **Task 3 — persistence (FR-007 / NFR-004)** |
| 3.1 | 16-byte record (magic/version/embedded capacity/reserved/CRC-32) with strict validation order; exact length enforced | PASS | `persistence.*`, `test_persistence_record_validation` (NULLPTR/BAD_LENGTH/BAD_MAGIC/BAD_VERSION/BAD_CAPACITY/BAD_COUNT/BAD_RESERVED/BAD_CRC) |
| 3.2 | CRC-32 implementation proven against known answer `crc32("123456789") == 0xCBF43926` | PASS | `test_persistence_record_validation` |
| 3.3 | Restore → `KNOWN` + `isRestored()` + `OCCUPANCY_RESTORED`; corrupt/out-of-range → rejected, stays `UNKNOWN`, never clamped | PASS | `test_restore_from_valid_record`, `test_corrupt_record_rejected_stays_unknown` |
| 3.4 | Save-on-change + boot restore wired through `Preferences` | PASS (code) | `src/nvs_store.hpp`, `src/main.cpp` (encode/decode host-tested; the NVS call path itself needs flash) |
| 3.5 | Physical power-cycle survival / flash wear | **NOT TESTED** | M6 |
| **Task 4 — serial console (FR-011 groundwork)** |
| 4.1 | `R <0..255>` reconcile always emits a visible event; out-of-range rejected, not clamped | PASS | `parking_controller::reconcile` (event on every call), `test_reconcile_drives_events_and_view`, `test_occupancy_reconcile_bounds` |
| 4.2 | `?` STATUS line; bounded parser (no unbounded echo/overflow) | PASS | `src/main.cpp` `handleConsoleLine` — **execution asserted by Wokwi scenario 01 (run green 2026-11-06)** |
| **Task 5 — extended native tests** |
| 5.1 | Whole suite green | PASS | `pio test -e native` → **59/59** |
| 5.2 | All 41 M1A tests preserved unchanged | PASS | suite output lists every M1A case; 18 additions only |
| 5.3 | CI runs the same suite; workflow unchanged (no job that cannot run) | PASS | [run #3](https://github.com/abderaoufsec/SmartPark-IoT/actions/runs/42205158700) all 3 jobs green for `9fab481` (compile + 59/59 on ubuntu g++ + cppcheck); `.github/workflows/firmware-ci.yml` untouched |
| **Task 6 — Wokwi simulation** |
| 6.1 | `wokwi.toml` points at the real `firmware.bin`/`firmware.elf`; diagram parts/pins correct | PASS | `wokwi-cli lint --offline --warnings-as-errors` → exit 0, 0 warnings |
| 6.2 | Every substitution documented (IR beams → pushbuttons, safety loop → button, ECHO divider not modeled) | PASS | `simulation/wokwi/README.md` |
| 6.3 | Three scenarios written with exact serial assertions (boot/console, entry passage, obstruction guard) | PASS | `simulation/wokwi/scenarios/*.yaml` — all three executed green (6.4) |
| 6.4 | Scenario **execution** evidence | **PASS** | **3/3 `Scenario completed successfully`, exit 0** — wokwi-cli 0.29.1, 2026-11-06, real `WOKWI_CLI_TOKEN` (maintainer-provided). 01: `OCCUPANCY_RECONCILED` + `OCCUPANCY_CHANGE_REJECTED value=9` (no clamping) + both STATUS assertions + usage line. 02: `ENTRY_APPROACH` (count unchanged) → `PASSAGE_CONFIRMED_ENTRY` → count 3 → close sweep `3→0` under `obstruction=CLEAR`. 03: boot `obstruction=BLOCKED` → close denied until CLEAR → mid-sweep transient abort (state 4) → reopen → auto-retry `CLOSED` → final STATUS `gate=CLOSED obstruction=CLEAR`. Findings S-1/S-2 recorded in §3.2 |
| 6.5 | CI Wokwi job | **NOT TESTED** (intentionally absent) | no `WOKWI_CLI_TOKEN` **repository secret** exists — a job that cannot run would be a fake check; local runs are recorded in 6.4 |
| **Task 7 — documentation** |
| 7.1 | `pinmap-v2.md`: §9 beams, new §12 obstruction electrical, §3/§10/§11 updated, A4 gate | PASS | this document's §1 + diff |
| 7.2 | `bom.csv` + §11: beams ×2, pull-ups ×3, obstruction row updated | PASS | `docs/hardware/bom.csv` |
| 7.3 | Requirements status (FR-003/005/007, NFR-004) without changing requirement text | PASS | `docs/requirements/requirements.md` |
| 7.4 | Roadmap / architecture / root README / firmware README / changelog updated | PASS | `roadmap.md`, `overview.md`, both READMEs, `CHANGELOG.md` |
| 7.5 | Simulation README (scope honesty + commands + limitations) | PASS | `simulation/wokwi/README.md` |
| 7.6 | This checklist | PASS | — |
| **Milestone hygiene** |
| H.1 | Work only on `main`, no branches | PASS | `git branch -a` |
| H.2 | No commit / no push **before maintainer approval** | PASS | working phase had 0 new commits; approval given → `9fab481` pushed (see header) |
| H.3 | `legacy/v1/` untouched | PASS | SHA-256 re-verified (§3) |
| H.4 | No MQTT/backend/frontend/Flutter/AI/payments code; CI unchanged | PASS | changed-path list in §3 |
| H.5 | No fabricated tests/simulation/hardware results | PASS | every claim maps to a command output from this session; un-run items are marked NOT TESTED |
| H.6 | Build actually compiles | PASS | clean `pio run`, exit 0, 0 warnings |
| H.7 | Requirements not silently changed | PASS | status-column edits only |
| H.8 | Stray root file noted, not deleted | PASS | see §6.6 |

**Totals: 36 PASS · 0 FAIL · 5 NOT TESTED** (rows 1.8, 2.4, 2.5, 3.5, 6.5 —
all hardware- or CI-secret-gated; none of them is claimed as working).

## 6. Known risks carried forward

1. **~~Wokwi token outstanding~~ — RESOLVED 2026-11-06.** Maintainer provided
   a real `WOKWI_CLI_TOKEN`; all three scenarios ran green and row 6.4 now
   holds execution evidence (not lint, not intent).
2. **`set-control distance` on `wokwi-hc-sr04` — VERIFIED working** in
   wokwi-cli 0.29.1 (the docs' control list is stale): the entry/exit
   approach steps drove real `ENTRY_APPROACH` events in scenarios 02/03.
   Pushbutton `pressed` works too. Residual risk: wokwi-cli is alpha — a
   future CLI/API change can break the runs; they are re-runnable from the
   README commands.
3. **A4 (no PSRAM) is now load-bearing** — GPIO 16 carries the X2 beam.
4. Host compiler remains environment-dependent (WinLibs locally, ubuntu in CI).
5. NVS record survives *logical* corruption (CRC) but physical power-cycle
   behavior is unmeasured (M6).
6. **Stray untracked `t4.c`** (`int main(){return 0;}`) sits at the repo root —
   a one-line compiler smoke test, not part of M1B, **left untouched** (not
   deleted, not committed).

## 7. Recommended next milestone

**Close out M1 first, then M2.**

1. **M1 close-out: DONE.** `WOKWI_CLI_TOKEN` was provided on 2026-11-06 →
   all three scenarios ran green → results recorded in §5 row 6.4 and §3.2
   (findings S-1/S-2). M1's evidence chain is complete for everything that
   can be verified without hardware.
2. **Then M2** (MQTT + offline queue, Wi-Fi lifecycle, event IDs, heartbeat) —
   the roadmap's next milestone; the event queue and `uint32` seq are already
   the M2 hand-off surface.
3. **M6 (later, hardware-gated):** beam/obstruction procurement, §10 gate,
   power-cycle persistence, < 200 ms reaction measurement, threshold
   calibration. No M1 claim depends on M6 succeeding — but nothing physical is
   claimed until it does.
