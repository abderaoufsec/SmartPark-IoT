# SmartPark-IoT V2 — pin map & electrical design (PROVISIONAL)

**Status: PROVISIONAL DESIGN — NOT PHYSICALLY TESTED, NOT WIRED.**
No measurement or breadboard build backs this document; it is a design
proposal produced from source analysis during milestone M1A and revised in
M1B (second passage points on GPIO 4/16 → new §9, obstruction input
electrical assumptions → new §12). A Wokwi simulation
([`simulation/wokwi/`](../../simulation/wokwi/)) executes the firmware
against a logically-equivalent wiring, but **it validates none of the
electrical assumptions below** — those remain NOT TESTED. **Verify every
item in §10 before connecting power.** Physical validation belongs to M6
([roadmap](../milestones/roadmap.md)).

Companion documents: [v1-audit.md](v1-audit.md) (findings this design answers),
[requirements](../requirements/requirements.md) (FR/NFR IDs), BOM in §11 +
[`bom.csv`](bom.csv).

---

## 1. Board assumptions (must be verified)

| # | Assumption | Why it matters | How to verify |
|---|---|---|---|
| A1 | Classic **ESP32** (WROOM-32 / WROVER) on a 30- or 38-pin "ESP32 Dev Module"/DevKit v1 board, **3.3 V logic** | Pin availability, no 5 V tolerance | Read silkscreen of the physical board; `esptool.py flash_id` |
| A2 | USB-serial bridge (CP2102/CH340) wired to UART0 = **GPIO1/TX0, GPIO3/RX0** | GPIO 1/3 must never be peripherals | Board schematic or continuity check |
| A3 | Flash occupies **GPIO 6–11** (internal, usually not broken out) | Never use | Datasheet/module label |
| A4 | **No PSRAM** (classic WROOM-32 DevKit) — **load-bearing since M1B** | GPIO 16 (now the X2 exit beam, §9) and 17 are free on a WROOM-32 but used by PSRAM on a WROVER: a WROVER board invalidates the M1B pin map | Read the module label / `esptool.py flash_id` (§10) |
| A5 | 5 V available from USB/external rail for HC-SR04 VCC and (externally) the servo | Sensor supply | Measure rail under load |

**If the actual board differs** (ESP32-S2/S3/C3, Pico, S3-DevKitN, …), this pin
map is invalid and must be re-derived: strapping pins and pin counts differ per
variant. That check is a hard gate in §10.

## 2. Design rules applied

Pins are excluded when they are:

1. **UART0:** GPIO 1 (TX0), GPIO 3 (RX0) — never again (V1 failure H-01/F-11).
2. **Flash interface:** GPIO 6–11 (module-internal).
3. **Boot-strapping:** GPIO 0 (boot mode), GPIO 2, GPIO 5, GPIO 12 (MTDI /
   VDD_SPI voltage — a high level at reset can prevent boot), GPIO 15 (MTDO /
   boot logging). ECHO lines in V1 drove 2/5/15 (finding H-03) → eliminated.
4. **PSRAM:** GPIO 16/17 (WROVER modules). **M1B exception:** GPIO 16 is
   now used for the X2 exit beam — legitimate only on a PSRAM-less WROOM
   board (assumption A4, checked in §10). GPIO 17 stays untouched.
5. **Not exposed / not universal:** GPIO 37/38 (absent on most DevKit headers).
6. **Input-only pins** (GPIO 34–39 have no output driver and no internal
   pull-ups/pull-downs) → used **only** as ECHO/inputs, never as TRIG/LED/servo.
7. ADC2 consideration for M2: none of our pins are read as analog; digital use
   is unaffected by Wi-Fi. ECHO inputs use ADC1 pins 34/35/36/39, so future
   analog additions stay Wi-Fi-compatible.

## 3. V2 pin assignment

| GPIO | Dir | Function | V1 used it as | Conflict check |
|---|---|---|---|---|
| **13** | OUT | Servo PWM (barrier) | servo (unchanged) | free of strapping; LEDC channel only; JTAG MTCK default mux — unused as JTAG |
| **18** | OUT | TRIG — entry HC-SR04 | — (V1: free) | VSPI default SCK mux, VSPI unused |
| **19** | OUT | TRIG — zone A HC-SR04 | — | VSPI default MISO mux, unused |
| **23** | OUT | TRIG — zone B HC-SR04 | — | VSPI default MOSI mux, unused |
| **25** | OUT | TRIG — exit HC-SR04 | zone A LED | DAC1 output pad (DAC unused) |
| **26** | OUT | LED — green (spaces known free) | green LED (unchanged) | DAC2 (DAC unused) |
| **27** | OUT | LED — red (known full) | red LED (unchanged) | — |
| **32** | OUT | LED — zone A occupied | — (V1: zone A on 25) | ADC2 (digital use only) |
| **33** | OUT | LED — zone B occupied | zone B LED (unchanged) | ADC2 (digital use only) |
| **34** | IN | ECHO — entry (input-only) | entry ECHO (V1: GPIO 15!) | input-only ⇒ no driver conflicts; ADC1_CH6 |
| **35** | IN | ECHO — zone A (input-only) | zone A ECHO (V1: GPIO 2!) | input-only; ADC1_CH7 |
| **36** | IN | ECHO — zone B (input-only) | zone B ECHO (V1: GPIO 4!) | input-only; ADC1_CH0 (VP) |
| **39** | IN | ECHO — exit (input-only) | exit ECHO (V1: GPIO 5!) | input-only; ADC1_CH3 (VN) |
| **21** | I2C | LCD SDA | same | default I2C0; external pull-ups per §8 |
| **22** | I2C | LCD SCL | same | default I2C0 |
| **14** | IN | **Obstruction / close-guard input** (M1B, semantics §12: HIGH = blocked/fault, LOW = clear) — **sensor not populated yet** | — | JTAG MTMS/HSPI default mux — unused as such; strapping-safe for inputs; firmware enables internal pull-up (§12) |
| **4** | IN | **Beam E2** — entry second passage point, active-LOW (M1B, §9) | zone B ECHO (V1) | internal + external pull-up; an open input reads FREE and can never confirm a count (§9) |
| **16** | IN | **Beam X2** — exit second passage point, active-LOW (M1B, §9) | — (V1: unused, PSRAM-excluded) | **assumption A4 gate:** valid only on PSRAM-less WROOM (WROVER conflict); header silk label "RX2" |

**Total used: 18 GPIO** (9 outputs, 7 inputs, 2 I2C) + 1 spare (GPIO 17).
Fits both 30-pin and 38-pin DevKit v1 headers (all listed pins are exposed on
both; GPIO 16 carries the header silk label "RX2"). GPIO 1/3/6–11/17/0/2/5/
12/15 are untouched; GPIO 16 is adopted under assumption A4 (no PSRAM — the
§10 gate checks this before wiring).

### Why this answers the V1 findings

- **H-01 / F-11:** UART0 (1/3) fully free again; serial monitor input becomes
  usable in later milestones.
- **H-03:** no strapping pin is driven by a peripheral at reset (echoes live on
  34/35/36/39; triggers are outputs the ESP32 itself controls after boot).
- **F-01 (cross-talk):** each sensor gets its **own TRIG** (§4).
- **H-02:** every ECHO passes through a level divider (§5).

## 4. Independent ultrasonic triggering (cross-talk mitigation)

V1 pulsed all four transducers with one shared trigger, so each module's echo
could contain another module's reflections (finding F-01). V2:

```
         GPIO18 ──► TRIG(entry)            ┌── ECHO(entry) ── divider ──► GPIO34
         GPIO19 ──► TRIG(zoneA)            ├── ECHO(zoneA)  ── divider ──► GPIO35
         GPIO23 ──► TRIG(zoneB)            ├── ECHO(zoneB)  ── divider ──► GPIO36
         GPIO25 ──► TRIG(exit)             └── ECHO(exit)   ── divider ──► GPIO39
```

Firmware schedule (implemented in `sensor_reader` + `parking_controller`):

1. Only **one** TRIG is pulsed per measurement slot; the other three modules
   stay idle.
2. Slots are staggered in time (`SENSOR_SLOT_MS = 80 ms` ⇒ full four-sensor
   round = 320 ms), longer than the HC-SR04's internal 60 ms re-trigger guard
   and the 30 ms echo timeout, so one sensor's ringing tail cannot overlap the
   next measurement.
3. Measurements run through a `millis()` scheduler — no `delay()` between
   slots (V1 used `delay(40)` ×4).
4. Per-read blocking is bounded by `ECHO_TIMEOUT_US = 30000` (30 ms worst
   case per slot; ~0 of that when a valid echo returns early). Full
   interrupt/RCTIME capture is possible later if the bounded stall matters —
   documented, not implemented (simplicity rule).

**Limitation:** staggered independent triggering reduces but cannot *prove*
elimination of acoustic cross-talk in the physical model — geometry-dependent,
measured in M6 (research Q2 in the blueprint).

## 5. 5 V → 3.3 V ECHO conversion (required, finding H-02)

HC-SR04 VCC runs at 5 V and **ECHO outputs 5 V logic. ESP32 GPIOs are not
5 V tolerant.** V1 wired ECHO straight into GPIO — damage risk (H-02).

**Design: resistive divider per ECHO line**

```
 ECHO ──[R1 20 kΩ]──┬──► ESP32 GPIO (34/35/36/39)
                    │
                 [R2 10 kΩ]
                    │
                   GND
```

`V_GPIO = 5 V × R2/(R1+R2) = 5 × 10/30 ≈ 3.33 V` — within the 3.3 V domain
(below the ≈3.6 V absolute-max input) and above `VIH ≈ 2.475 V` (0.75 × VDD)
so a HIGH reads reliably.

- Values may be 10 k/20 k exact or nearest E12 (e.g., 18 k/10 k ≈ 3.21 V) —
  **measure the tap voltage with a multimeter before connecting the GPIO**.
- Alternative: a BSS138-based bidirectional I2C-style level-shifter module
  (one channel per ECHO) if resistor stock is unavailable.
- Do **not** use a single series resistor into the ESP32 clamp diodes as a
  "solution" — it is a current-limiting trick outside spec, not a design.
- Divider output impedance ≈ 6.7 kΩ — negligible for the ESP32 input, and
  HC-SR04 ECHO edges are slow enough for this RC (echo pulses are ≥ tens of µs).

TRIG lines need **no** shifting: ESP32 3.3 V HIGH exceeds HC-SR04's VIH
(0.3 × VCC ≈ 1.5 V? datasheet-dependent — verify per module batch).

## 6. Servo power (finding H-04)

| Requirement | Value |
|---|---|
| Servo | SG90: 4.8–6 V supply, **stall ≈ 0.65–0.75 A** (datasheet figure — verify yours) |
| Supply | **Separate regulated 5 V, ≥ 1 A**, not the ESP32 3.3 V rail and not shared with the logic supply if the USB port budget is unknown |
| Ground | **Common ground** between servo supply, ESP32 and all HC-SR04 modules (single star point at the breadboard ground rail) |
| Decoupling | ≥ 470 µF electrolytic + 100 nF ceramic **at the servo connector** |
| Control | PWM only on GPIO 13 (LEDC), 50 Hz, 500–2400 µs as V1 |
| Mechanical | Supervised desk model only; no exposed moving parts; never a real gate ([SECURITY.md](../../SECURITY.md)) |

Power budget (**estimates from component datasheets — not measurements**):
ESP32 peak ≈ 0.5 A @ 5 V (Wi-Fi later), 4 × HC-SR04 ≈ 0.06 A, LCD+backlight
≈ 0.1 A, 4 × LEDs ≈ 0.04 A, SG90 stall ≈ 0.75 A ⇒ **plan ≈ 1.5 A total peak**;
design the supply for ≥ 2 A or split logic/servo supplies. Measuring this is an
M6 task (H-09).

## 7. LEDs (finding H-06)

Four LEDs (green, red, zone A, zone B), each with a **series resistor
220 Ω–330 Ω** (3.3 V − Vf≈2.0 V ⇒ ≈ 4–6 mA with 330 Ω; well under the 12 mA
recommended / 40 mA absolute per GPIO). **No LED may connect directly to a
GPIO.** Polarity and fitted values must be photographed/recorded at build time.

## 8. LCD I2C compatibility (finding H-05)

- Module: 16×2 HD44780 + PCF8574 backpack, address **0x27** (as V1), logic
  supply must be verified: many backpacks pull SDA/SCL up to **5 V**.
- **Check before wiring:** measure the backpack's pull-up rail (continuity from
  SDA/SCL through pull-up to VCC pin). If it is 5 V:
  - cut/replace the pull-ups with 4.7 kΩ → **3.3 V**, or
  - insert a proper level-shifter section for SDA/SCL.
- If the backpack is a 3.3 V-logic variant, wire directly (GPIO 21/22).
- Keep I2C bus short (≤ 30 cm on a breadboard) to limit bus capacitance.

## 9. Second passage points — IR break-beams (M1B, FR-003)

An ordered confirmation needs a *second* sensing point per direction so a mere
approach can never be counted (FR-003). The M1B arrangement:

```
  ENTRY lane:   E1 (entry HC-SR04, GPIO 18/34)  →  E2 beam (GPIO 4, past the barrier)
  EXIT lane:    X1 (exit HC-SR04, GPIO 25/39)   →  X2 beam (GPIO 16, outer exit)
```

**Selected technology: 2× digital IR break-beam modules** (active-LOW output:
OUT is pulled LOW while the beam is broken). Wiring rules:

- E2 → GPIO 4, X2 → GPIO 16 (A4 no-PSRAM gate above; silk label "RX2").
- Firmware uses `INPUT_PULLUP`; the board adds an **external 10 kΩ pull-up
  per line to 3.3 V** (same rail, no contention) — BOM §11.
- Semantics: **LOW = beam broken = occupied; HIGH = free.** An open input
  (disconnected beam, dead module, missing wire) reads HIGH = free, so a
  missing second point can **never fabricate a count** — it can only make
  `PASSAGE_CONFIRMED_*` unreachable, degrading counting back to
  reconciliation-only (the M1A safe default). The fail direction is
  deliberate: *toward "do not count"* — the mirror image of the obstruction
  input's fail direction (§12, toward "do not close").
- Geometry (mount height, beam width, alignment) is **unverified** — §10/M6.
  The firmware contract (FREE→OCCUPIED ordering, 50 ms debounce, stale ⇒
  `UNKNOWN` ⇒ never confirms) is host-tested; see `passage_detector` and
  `test/test_core`.

**Physical hardware:** the modules are **selected but not procured** (BOM
§11). Until they are wired, GPIO 4/16 float HIGH (pull-ups) = free forever ⇒
automatic counting stays disabled by design, exactly as in M1A.

**Wokwi substitute:** Wokwi has no IR-beam part, so the simulation drives the
same GPIOs with pushbuttons (`simulation/wokwi/README.md`) — identical
firmware semantics, logical equivalence only.

## 10. Pre-wiring verification checklist (M1A gate — all open)

- [ ] Exact ESP32 board model/revision photographed and recorded (assumption A1)
- [ ] Confirm GPIO 6–11 not exposed; confirm which pins exist on the header
- [ ] Confirm UART0 is GPIO 1/3 on this board (A2)
- [ ] Confirm **no PSRAM** — hard requirement since M1B (GPIO 16 = X2 beam, §9),
      assumption A4: a WROVER board makes this pin map invalid
- [ ] Measure ECHO divider tap voltage per channel **before** connecting GPIO
- [ ] Measure LCD backpack pull-up rail voltage
- [ ] Measure LED resistor values fitted
- [ ] Choose and document the servo supply (rail, rating, measured voltage
      under servo stall)
- [ ] Verify HC-SR04 TRIG accepts 3.3 V HIGH (module batch datasheet)
- [ ] Photograph final wiring; add schematic + pin-table photo to `docs/hardware/`
- [ ] Bench-test each sensor alone (M6) before enabling any actuator
- [ ] Beam modules (§9): confirm **active-LOW** output polarity with a meter;
      confirm the external 10 kΩ pull-ups are fitted on GPIO 4/16
- [ ] Obstruction input (§12): confirm external pull-up fitted; with the input
      **open** the pin must read HIGH (BLOCKED); CLEAR may only be an actively
      driven LOW

**None of the above has been performed. This design must not be described as
tested, validated or safe for deployment.**

## 11. Bill of materials (revised for V2)

Machine-readable copy: [`bom.csv`](bom.csv) (supplier/price columns left
**TBD** — no prices will be invented; fill with real quotes, blueprint §7).

### Present in the V1 prototype (verify physical stock)

| Item | Qty | Notes |
|---|---:|---|
| ESP32 DevKit (WROOM-32), exact model TBD | 1 | assumption A1 |
| HC-SR04 ultrasonic module | 4 | entry, zone A, zone B, exit |
| SG90 servo | 1 | barrier actuator |
| 16×2 LCD + PCF8574 I2C backpack (0x27) | 1 | §8 voltage check |
| LED, green / red / yellow ×2 | 4 | with series resistors |
| Breadboard, jumper wires, USB cable | — | — |

### Added / required by the V2 design (not yet procured)

| Item | Qty | Why |
|---|---:|---|
| Resistors 20 kΩ + 10 kΩ (E12 nearest OK) | 8 (4 pairs) | ECHO level dividers (§5) — **replaces the unsafe direct wiring** |
| Resistors 220–330 Ω | 4 | LED series resistors (§7) — V1 values unknown |
| IR break-beam module (digital, active-LOW) | 2 | **selected in M1B** as second passage points E2/X2 → GPIO 4/16 (§9) — not yet procured |
| Resistors 10 kΩ (external pull-ups) | 3 | beam lines ×2 (§9) + obstruction input (§12), pull to 3.3 V |
| Electrolytic capacitor ≥ 470 µF / 10 V + 100 nF ceramic | 1 + 2 | servo supply decoupling (§6) |
| Regulated 5 V / ≥ 1 A (prefer 2 A) power supply | 1 | servo + sensors (§6); measured budget TBD |
| Multimeter (verification of §10) | 1 | gate before first power-on |

### Required for later milestones (explicitly NOT available now)

| Item | Qty | Needed by | Effect if absent |
|---|---:|---|---|
| Obstruction sensor (IR break-beam or HC-SR04) for gate path → GPIO 14 (**assigned M1B**, §12) | 1 | FR-005 full compliance (M6) | input unwired reads HIGH = BLOCKED via pull-up ⇒ **no auto-close ever happens physically** (fails safe); lane-guarded close policy (§12) remains as defence in depth |
| Fuse / power protection for the servo rail | 1 | M6 electrical hardening | desk-model only |

*Prices, suppliers and availability are intentionally empty: quoting without
supplier evidence would violate the project's evidence rules.*

---

## 12. Obstruction input — electrical assumptions (M1B, GPIO 14)

**Requirement (FR-005):** no automated gate closing may endanger a vehicle or
a person; a dedicated obstruction detector across the gate closing path with
detect ⇒ stop/reverse/hold in **< 200 ms** is required in any real deployment.

### 12.1 Firmware contract (implemented, host-tested in M1B)

| # | Rule |
|---|---|
| 1 | GPIO 14 is sampled every `loop()` iteration and fed to the barrier FSM as `ObstructionState` (`CLEAR` / `BLOCKED` / `UNKNOWN`). |
| 2 | Polarity: **HIGH = blocked or faulted, LOW = clear.** |
| 3 | Bias = **fail-safe pull-up**: firmware enables `INPUT_PULLUP`; the board adds an **external 10 kΩ pull-up to 3.3 V** (BOM §11). Same rail ⇒ no contention. |
| 4 | An **open input** — broken wire, absent sensor, released NC loop — reads HIGH = BLOCKED ⇒ **can never authorize a close**. CLEAR must be actively driven LOW; loss of the driving electronics therefore also reads BLOCKED. |
| 5 | CLEAR must be **fresh** (sampled within `OBSTRUCTION_STALE_MS` = 1000 ms) or the barrier sees `UNKNOWN`: close never starts and an in-progress sweep aborts. Unknown never expires into permission. |
| 6 | Closing needs a fresh CLEAR **both at sweep start and on every tick of the sweep**; BLOCKED/UNKNOWN mid-sweep aborts to a reopen. |
| 7 | Deliberate fail-direction asymmetry with the beams (§9): an open input fails toward **BLOCKED (never close)** here and toward **FREE (never count)** there — each subsystem fails toward *its* safe action. |

### 12.2 Physical sensor status (honest state)

- The detector itself is **not procured** (BOM §11, planned M6). Neither was it
  ever populated in V1.
- **Consequence on the real desk build with the pull-up only:** GPIO 14
  permanently reads BLOCKED ⇒ the guarded auto-close **never happens
  physically**. Gate opens on approach and stays open until the M6 sensor
  lands. This is intended fail-safe behaviour, documented, not a bug.
- **Defence in depth (unchanged from M1A):** the closing sweep *also* requires
  the entry ECHO to read a continuously valid FREE for `LANE_CLEAR_CONFIRM_MS`
  (3 s) and aborts mid-sweep on any OCCUPIED/UNKNOWN/FAULT lane reading. That
  is a **lane guard, not a certified obstruction sensor**: beam width, mount
  angle and sample rate are unverified (§10), and a thin object can enter
  between samples — clearly marked unsafe for anything but a supervised desk
  model.
- **Wokwi substitute:** `btn_obs` pulls GPIO 14 to GND through a pushbutton
  (pressed = loop closed = CLEAR; released = loop open = BLOCKED — the same
  readout the firmware produces for a broken wire). Logical equivalence only.

### 12.3 Reaction time

Firmware detection-to-stop = the next `loop()` iteration (`loop()` contains no
`delay()`; the barrier tick aborts the sweep on the same iteration the input is
read). The **< 200 ms** end-to-end figure is a *measured* M6 item — **NOT
TESTED** until a physical sensor, its geometry and a scope/photogate test
exist. The Wokwi simulation exercises the abort *logic* (scenario
`03-obstruction-guard.yaml`) but validates nothing electrical.
