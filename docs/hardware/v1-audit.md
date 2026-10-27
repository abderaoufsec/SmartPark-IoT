# V1 audit — firmware bugs, hardware risks and unknowns

**Method:** static source inspection of the archived V1 sketch and repository
contents at baseline commit `d566ec4`. **No live electrical measurements, no
hardware bench, no reproduced build existed at audit time** — findings below are
source-based and must be confirmed on hardware during M1/M6.

**Rule of this milestone:** every finding here is *documented, not fixed*.
Refactoring starts in M1 (see [roadmap](../milestones/roadmap.md)).

Findings are labeled `F-xx` (firmware logic), `H-xx` (hardware/electrical),
`D-xx` (documentation/repository).

---

## 1. Firmware logic findings

| ID | Sev | Location | Finding | Requirement |
|---|---|---|---|---|
| F-01 | Critical | `lireDistance()`, `loop()` step 1 | **One shared TRIG (GPIO 3) pulses all four HC-SR04 transducers.** They fire simultaneously; the 40 ms gap between echo reads does not prevent acoustic cross-talk/reverberation between modules, so readings can reflect other sensors' bursts. | FR-001 |
| F-02 | Critical | `loop()` steps 2–3 | **Approach is counted as passage.** Entry increments as soon as anything is within 15 cm of the entry sensor — a vehicle that stops or reverses is still counted. Exit decrements the same way. A single proximity sensor cannot establish direction; the count drifts. | FR-003 |
| F-03 | High | `lireDistance()` → `999` sentinel | **Timeout is treated as "free zone".** `999 > SEUIL_ZONE_LIBRE (30)`, so a disconnected/blocked zone sensor reports the zone as *free and empty* (LED off, guidance "GO …"). Invalid measurement has no distinct `UNKNOWN` state. | FR-001 |
| F-04 | High | `ouvrirBarriere()` / `fermerBarriere()` / entry branch | **Blind timed barrier close.** After `delay(2000)` the gate closes with no obstruction detection, no position feedback, no manual/FAULT state — it will push against anything in the way. Unsafe for anything beyond a desk model. | FR-005, NFR-001 |
| F-05 | High | `loop()` overall | **Fully blocking loop.** `pulseIn` up to 30 ms ×4, `delay(40)` ×4, plus an entry cycle of ≈0.69 s open + 2 s wait + ≈0.69 s close: sensing, LEDs and LCD freeze for ≈3.4 s per entry. Vehicles at the exit during that window are invisible; the LCD timestamp (`maintenant`) is captured before the reads, so debounce math runs against a stale clock. | FR-005 |
| F-06 | High | `setup()` / boot | **No persistence.** `voitures` starts at 0 on every reboot; a power cycle mid-operation silently invents empty capacity. | FR-007, NFR-004 |
| F-07 | Medium | `mettreAJourLCD()` | **16-column overflow.** `"Free:N " (7) + direction (10) = 17` characters for *every* non-full message; the code only pads up (`while (length < 16)`), never truncates. Observable effect: `GO RIGHT B` loses its final `B`, and the 17th byte is written to an invalid DDRAM address. | display correctness |
| F-08 | Medium | zone LED vs guidance logic | **Inconsistent thresholds.** Zone LEDs use `d < 15 cm` (occupied) while guidance uses `d > 30 cm` (free). In the 15–30 cm band the LED shows *empty* while the LCD reports the zone *not free* — two contradictory states for one sensor. No hysteresis anywhere, so single noisy samples flip both. | FR-002 |
| F-09 | Medium | entry branch, "Parking Plein" | **Refusal is invisible to the driver.** When full, only `Serial` logs "Entree refusee"; the LCD keeps showing `PARK FULL` (already visible) but no distinct refusal feedback, and repeated triggers re-log every loop pass (debounce timestamp is not updated in the `else` branch). | FR-004 |
| F-10 | Low | `loop()` step `constrain` | **Silent clamping masks logic errors.** `voitures = constrain(...)` hides double-count defects instead of raising an inconsistency alert. | FR-011 (audit trail) |
| F-11 | Low | `setup()` comment | **Serial RX unusable.** Code itself notes RX0 is taken by the TRIG assignment (see H-01): serial input/debug commands are impossible, and any traffic a terminal sends to RX will wiggle the sensor trigger line. | — |
| F-12 | Low | architecture | **No Wi-Fi/MQTT/backend/tests exist** — expected at V1, listed for completeness: no automated tests, no config store, no health monitoring, no event IDs. | FR-008…FR-012 |

## 2. Hardware / electrical risks

| ID | Sev | Risk | Detail | Mitigation milestone |
|---|---|---|---|---|
| H-01 | **Critical** | **GPIO 3 = UART0 RX used as TRIG.** | The shared TRIG is on the pad wired to the USB-serial adapter's TX on common ESP32 dev boards. The serial line and the sensor trigger share one pin: terminal output can spuriously fire all four sensors, sensor wiring can corrupt UART RX, and flashing/monitoring behavior becomes board-dependent. | M1 pin redesign |
| H-02 | **Critical** | **5 V ECHO into 3.3 V-only GPIOs.** | HC-SR04 ECHO is a 5 V logic output; ESP32 GPIOs are **not 5 V tolerant**. The four echo pins (15, 2, 4, 5) are wired directly per the V1 design. Damage risk — requires resistor dividers or a proper level shifter, verified per pin, before further powering. | M6 electrical |
| H-03 | High | **Boot-strap pins used for ECHO (GPIO 2, 5, 15).** | GPIO 2, 5 and 15 are strapping pins whose level at reset influences boot behavior (boot mode/logging/SDIO timing). Sensors may drive their ECHO lines during the ESP32 reset window → intermittent boot failures or odd boot modes. Must be checked against the exact board datasheet. | M1 pin redesign |
| H-04 | High | **Servo supply unknown / likely inadequate.** | An SG90 stalls around 0.65–0.75 A. If powered from the board's 3.3 V regulator or the laptop USB port (nothing in the repo documents the rail), brownouts and resets follow — the classic ESP32+servo failure. Needs a rated supply, common ground and decoupling. | M6 power budget |
| H-05 | Medium | **LCD I2C backpack pull-ups unknown.** | Many PCF8574 backpacks pull SDA/SCL up to 5 V. Unverified → possible over-voltage on the ESP32's I2C pins (21/22). | M6 electrical |
| H-06 | Medium | **LED series resistors undocumented.** | The original README BOM lists 4 LEDs but no resistors or values; the blueprint mentions resistors exist. If LEDs are direct-driven from GPIO, both LED and pin are at risk. Verify values on the physical board. | M6 electrical |
| H-07 | Medium | **No decoupling / wiring documentation.** | No schematic, no wiring diagram, no ground-star or decoupling notes; only one photo of the assembled model. Sensor glitches and resets under servo load are likely in this configuration. | M6 documentation |
| H-08 | Medium | **No obstruction sensing or mechanical limits on the gate.** | Servo is commanded by angle only. Combined with F-04, the model gate can stall against obstacles continuously (over-current, overheating). | M1 (logic) + M6 (sensing) |
| H-09 | Low | **Unknown total power budget & supply voltage** for 4 sensors + LCD + LEDs + servo on one feed; no measurements exist anywhere in the repository. | M6 |

> ⚠️ **Safety summary:** this is a supervised desk demonstrator. Do not connect
> the firmware to a real vehicle gate. Do not power the breadboard until H-02
> (5 V echo) and H-04 (servo supply) are resolved against the exact board.

## 3. Documentation & repository findings

| ID | Sev | Finding | Resolution |
|---|---|---|---|
| D-01 | High | Original README showed an **MIT license badge without any `LICENSE` file** — misleading. | Removed in M0; license decision explicitly pending (both authors must choose). |
| D-02 | Medium | README referenced a **structure that never existed** (`code/smart_parking.ino`, `report/`, `images/`, `mockups/`) and a placeholder clone URL (`yourusername`). | README rewritten to the real structure in M0. |
| D-03 | Medium | README claimed **Wokwi simulation "validates"** behavior; no Wokwi project/config exists in the repository. | Claim removed; simulation is planned M5. |
| D-04 | Medium | README **status table marked every feature ✅** with no reproducible evidence in the repo. | Replaced with the honest feature-status table. |
| D-05 | Low | LCD examples in the old README (`Free: 4 GO LEFT`) did not match actual firmware output (`Free:4 GO LEFT A `). | README now shows strings derived from the code. |
| D-06 | Low | `pasted_content_19.txt` was an exact duplicate of the firmware source. | Removed in M0 after SHA-256 verification (see `legacy/v1/README.md`). |
| D-07 | Low | Report PDF named `Smart_Parking_System_avec_Guidage_Directionnel (1).pdf` (download artifact). | Archived as `legacy/v1/original-report.pdf`; original name recorded. |

## 4. Unknowns (must be measured/verified in M1–M6)

1. Exact ESP32 board model and revision (pin capabilities, USB-serial chip, regulator).
2. Actual wiring vs README pin table — no schematic exists; photo shows an assembled model only.
3. Which rail powers the SG90 servo and whether it shares the logic supply.
4. LED resistor values; LCD backpack pull-up voltage.
5. Whether the physical prototype still exists and functions; no test logs, serial captures or measurements exist in the repository.
6. Real geometry-based calibration of `SEUIL_VOITURE = 15 cm` / `SEUIL_ZONE_LIBRE = 30 cm` (currently untested guesses for the model's dimensions).
7. Cross-talk magnitude between the four sensors in the physical layout.

## 5. What V1 does correctly (for balance)

- Debounce windows (2.5 s) on entry/exit reduce same-sensor double counts.
- `pulseIn` timeout (30 ms) bounds blocking when no echo returns.
- Occupancy is bounded (`0..6`) rather than unbounded.
- LCD line 1 and the "full" message are exactly 16 characters; lines are
  padded, so partial writes don't leave stale fragments in the normal case.
- Clear separation of read → entry logic → exit logic → indicators → display
  in `loop()` gives M1 a workable structure to refactor from.

---

## 6. Resolution status after M1A

Findings are **never deleted** — this table records what M1A changed in
software (logic proven by `pio test -e native`, compile proven by CI) versus
what still needs hardware work. “Design” = specified in
[pinmap-v2.md](pinmap-v2.md), **not yet built or measured**.

| ID | M1A status | Where / evidence |
|---|---|---|
| F-01 | 🟢 fixed (logic) | per-sensor TRIG array + 80 ms staggered scheduler: `src/pins.hpp`, `src/sensor_reader.cpp`, `ParkingController::slotFor()`; cross-talk elimination still needs M6 measurement |
| F-02 | 🟢 fixed (logic) — **counting deliberately disabled** | `passage_detector.hpp` + `test_single_beam_never_reaches_confirmed`, `test_entry_approach_opens_gate_but_never_counts`; needs second beam for FR-003 completion (pinmap §11) |
| F-03 | 🟢 fixed (logic) | `sensor_filter.cpp` timeout → `UNKNOWN`, `FAULT` escalation; `test_timeout_is_unknown_never_free`, `test_fault_escalation_and_recovery` |
| F-04 | 🟡 partially fixed — **provisional guarded close** | `barrier_controller.cpp` (sustained valid-FREE gate + mid-sweep abort); dedicated obstruction sensor still absent (H-08) → supervised desk model only |
| F-05 | 🟢 fixed (logic) | `src/main.cpp` `millis()` scheduler, no `delay()` in `loop()`; bounded `pulseIn` ≤ 30 ms/slot documented in pinmap §4 |
| F-06 | 🟡 mitigated, root cause open | boots `UNKNOWN` + admission defer (`occupancy_manager.hpp`, `test_occupancy_boots_unknown`); NVS persistence = M1B |
| F-07 | 🟢 fixed (logic) | `display_manager.cpp` pad/truncate to 16; `test_display_lines_are_exactly_16_chars`, `test_display_priority_messages` |
| F-08 | 🟢 fixed (logic), calibration open | unified hysteresis in `config.hpp`/`sensor_filter.cpp`; `test_fresh_classification_boundaries`, `test_hysteresis_*`, `test_noise_does_not_flip_state`; values provisional to M6 |
| F-09 | 🟢 fixed (logic) | `ENTRY: FULL STOP` LCD message + one deduplicated `ADMISSION_DENIED`; `test_controller_denial_emitted_once`, `test_display_priority_messages` |
| F-10 | 🟢 fixed (logic) | bounds rejection + `OCCUPANCY_CHANGE_REJECTED` event; `test_occupancy_reconcile_bounds`, `test_occupancy_change_bounds`, `test_reconcile_drives_events_and_view` |
| F-11 | 🟢 fixed (design + code) | TRIG moved off GPIO 3 (pinmap §3); serial RX free for a future local console (not enabled in M1A) |
| F-12 | 🟡 partially | unit tests + 3-gate CI now exist (NFR-007); MQTT/monitoring/event IDs remain M2+ |
| H-01 | 🟡 design fixed, **not wired** | pinmap §3 (GPIO 3/1/ freed); physical rewiring in M6 |
| H-02 | 🟡 design fixed, **not built** | divider spec + BOM rows (pinmap §5); per-pin voltage measurement is a pre-power gate (§10) |
| H-03 | 🟡 design fixed, **not wired** | echoes moved to input-only 34/35/36/39 (pinmap §3) |
| H-04 | 🟡 design fixed, **not powered** | external 5 V ≥ 1 A, common ground, decoupling (pinmap §6) |
| H-05 | ⬜ open | LCD pull-up rail measurement still required (pinmap §8) |
| H-06 | 🟡 design fixed, **not verified on board** | 220–330 Ω mandated in BOM (pinmap §7) |
| H-07 | 🟡 partially | pin table + design doc exist (`pinmap-v2.md`); schematic/photo still due in M6 |
| H-08 | 🟡 partially | logic-side mitigation merged (guarded close, `BLOCKED`/`FAULT`/`MANUAL`); obstruction sensor documented as required-but-absent (pinmap §9/§11) |
| H-09 | ⬜ open | power budget is a datasheet *estimate* only; measurement is M6 |

D-01…D-07 were resolved in M0 (see their Resolution column in §3).

*Recorded during milestone M0. Update this document (don't silently delete
findings) when M1 fixes them; link the fix commit/PR next to the finding.*
Section 6 added in M1A (2026-10-27); commit links will be added when M1A is
committed.
