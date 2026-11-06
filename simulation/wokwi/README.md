# SmartPark-IoT — Wokwi simulation (M1B)

Headless Wokwi project that runs the **exact `esp32dev` firmware binary**
(`firmware/esp32/.pio/build/esp32dev/firmware.bin`) against simulated sensors,
so passage/gate/persistence logic gets executed end-to-end without hardware.

> **Scope honesty:** everything here is **SIMULATED**. It proves the firmware
> *runs and behaves* under Wokwi's parts models — it does **not** validate the
> physical wiring, electrical assumptions (pinmap §5 divider, §9/§12 pull-ups,
> §10 pre-power gate) or timing on a real board. Those stay **NOT TESTED**
> until bench validation (M1C/M6).

## Layout

| Part id   | Wokwi type              | Firmware symbol            | Notes |
| --------- | ----------------------- | -------------------------- | ----- |
| `esp`     | `wokwi-esp32-devkit-v1` | —                          | classic 30-pin WROOM DevKit (assumption A3, no PSRAM ⇒ GPIO 16 usable) |
| `u1`      | `wokwi-hc-sr04`         | `SENSOR_ENTRY` (18/34)     | entry point 1, TRIG GPIO 18, ECHO GPIO 34 |
| `u2`      | `wokwi-hc-sr04`         | `SENSOR_ZONE_A` (19/35)    | zone A |
| `u3`      | `wokwi-hc-sr04`         | `SENSOR_ZONE_B` (23/36)    | zone B, ECHO on `VP` = GPIO 36 |
| `u4`      | `wokwi-hc-sr04`         | `SENSOR_EXIT` (25/39)      | exit point 1, ECHO on `VN` = GPIO 39 |
| `btn_e2`  | `wokwi-pushbutton`      | `BEAM_ENTRY_PIN` (GPIO 4)  | **substitute** for the entry IR beam (point 2) |
| `btn_x2`  | `wokwi-pushbutton`      | `BEAM_EXIT_PIN` (GPIO 16)  | **substitute** for the exit IR beam (point 2) |
| `btn_obs` | `wokwi-pushbutton`      | `OBSTRUCTION_PIN` (GPIO 14)| **substitute** for the safety loop / obstruction input |
| `lcd`     | `wokwi-lcd1602` (I2C)   | `LCD_ADDR 0x27` (21/22)    | PCF8574 backpack |
| `servo`   | `wokwi-servo`           | `SERVO` (GPIO 13)          | barrier arm |
| `led_*`   | `wokwi-led` + 220 Ω     | 26/27/32/33                | admission + zone lamps |
| `pu_*`    | 10 kΩ pull-ups          | §9/§12 external pull-ups   | same rails as the BOM parts |

### Documented substitutions (simulation vs physical)

- **IR beams → pushbuttons.** Wokwi has no IR-beam part. A pushbutton driving
  the same GPIO with `INPUT_PULLUP` has *identical firmware semantics*:
  pressed = input pulled LOW = **beam broken = occupied**; released (or
  disconnected) = pulled HIGH = free, which can never confirm a count.
  The native unit tests are the deterministic harness for the beam logic.
- **Obstruction → safety-loop button.** Firmware reads HIGH = BLOCKED,
  LOW = CLEAR (fail-safe pull-up bias, pinmap §12). `btn_obs` connects GPIO 14
  to GND through the button, with the external 10 kΩ pull-up modeled:
  **held pressed = loop closed = CLEAR; released = loop open = BLOCKED**
  (same readout the firmware would produce for a broken wire).
- **ECHO divider (pinmap §5) is not modeled.** Wokwi's HC-SR04 drives logic
  levels directly; the physical 5 V→3.3 V divider remains NOT TESTED.
- **Ultrasonic distance** defaults to 400 cm (free). Scenarios drive it with
  `set-control: {part-id: u1, name: distance, value: …}` (< 15 cm = occupied).

## Prerequisites

1. Build the firmware (the sim runs this binary):
   `pio run -d firmware/esp32 -e esp32dev`
2. Install the CLI (Windows): `iwr https://wokwi.com/ci/install.ps1 -useb | iex`
3. Token: export `WOKWI_CLI_TOKEN` (from <https://wokwi.com/dashboard/ci>).
   Without it the CLI cannot start a simulation.

## Commands

```powershell
# static check of the diagram (part ids, pin names, connections)
wokwi-cli simulation/wokwi lint

# run a scenario (build first; paths are relative to the repo root)
wokwi-cli simulation/wokwi --scenario simulation/wokwi/scenarios/01-boot-console.yaml
wokwi-cli simulation/wokwi --scenario simulation/wokwi/scenarios/02-entry-passage.yaml
wokwi-cli simulation/wokwi --scenario simulation/wokwi/scenarios/03-obstruction-guard.yaml
```

## Scenarios

| File | Asserts |
| ---- | ------- |
| `01-boot-console.yaml` | M1B boot banner + honesty line; `R 2` → `OCCUPANCY_RECONCILED` + `STATUS … count=2`; out-of-range `R 9` → `OCCUPANCY_CHANGE_REJECTED`, count unchanged (F-10); unknown input prints usage |
| `02-entry-passage.yaml` | entry approach → gate `OPEN` while count stays 2 (approach never counts); E2 beam hit → `PASSAGE_CONFIRMED_ENTRY`, count 2→3; lane clear + safety loop CLEAR → `gate=CLOSED` |
| `03-obstruction-guard.yaml` | boot reads `obstruction=BLOCKED` (open loop = fail-safe); opening works while BLOCKED; guarded close needs CLEAR; **mid-sweep BLOCKED aborts and reopens**; BLOCKED never closes over repeated windows; CLEAR recovers to `CLOSED` |

## Status (M1B)

- `wokwi-cli lint` — see m1b.md report for the recorded result.
- Scenario runs require the `WOKWI_CLI_TOKEN` secret/value; **results are
  recorded in `docs/milestones/m1b.md` only after an actual run** — no
  simulated outcome is claimed here without execution.
- CI: **no Wokwi job is added** to `.github/workflows/firmware-ci.yml` until
  the repository secret `WOKWI_CLI_TOKEN` exists (documented in m1b.md).
- Known watch-item: whether the current `wokwi-cli` exposes an automation
  control for the HC-SR04 (`distance`). If it does not, scenario 01 still
  runs (console/buttons only) and the ultrasonic-driven scenarios are marked
  NOT TESTED with the CLI's own control-list error quoted in m1b.md.
