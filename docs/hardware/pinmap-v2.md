# SmartPark-IoT V2 — pin map & electrical design (PROVISIONAL)

**Status: PROVISIONAL DESIGN — NOT PHYSICALLY TESTED, NOT WIRED.**
No measurement, breadboard build or simulation backs this document yet. It is a
design proposal produced from source analysis during milestone M1A. **Verify
every item in §10 before connecting power.** Physical validation belongs to M6
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
| A4 | No PSRAM assumption (works on WROOM-32) | GPIO 16/17 free on WROOM but used by PSRAM on WROVER → we avoid them anyway | — |
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
4. **PSRAM:** GPIO 16/17 (WROVER modules) — avoided for board universality.
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
| **14** | — | **RESERVED** — future obstruction sensor input (not populated) | — | JTAG MTMS/HSPI default mux — unused as such; strapping-safe for inputs |
| **4** | — | spare (broken out on both header sizes) | zone B ECHO (V1) | — |

**Total used: 15 GPIO** (9 outputs, 4 inputs, 2 I2C) + 2 reserved/spare.
Fits both 30-pin and 38-pin DevKit v1 headers (all listed pins are exposed on
both). GPIO 1/3/6–11/16/17/0/2/5/12/15 are untouched.

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

## 9. Obstruction sensing — requirement & M1A provisional policy

**Required for FR-005 in any real deployment:** a dedicated obstruction
detector across the gate closing path (second ultrasonic aimed at the lane, or
an IR break-beam, or a bumper strip), wired to the **reserved GPIO 14**, with a
specified reaction: detect ⇒ stop + reverse/hold, sampled fast enough that
total reaction < 200 ms.

**This hardware does not exist in the current BOM** (we will not invent it).
Therefore V1A firmware behavior is **provisional and model-only**:

- Auto-close happens **only** as a *guarded close*: the closing sweep runs
  while the **entry ECHO** reads a continuously valid FREE distance; any
  OCCUPIED / UNKNOWN / FAULT reading during the sweep aborts to `BLOCKED` and
  re-opens.
- This is **not** equivalent to an obstruction sensor: beam width, mount angle
  and update rate are unverified (§10), and a thin object can enter between
  samples. Clearly marked unsafe for anything but a supervised desk model.
- If the entry sensor is unhealthy (UNKNOWN), the gate **stays open** (fails
  open toward traffic safety, fails closed toward security — documented trade-
  off, see firmware README).

## 10. Pre-wiring verification checklist (M1A gate — all open)

- [ ] Exact ESP32 board model/revision photographed and recorded (assumption A1)
- [ ] Confirm GPIO 6–11 not exposed; confirm which pins exist on the header
- [ ] Confirm UART0 is GPIO 1/3 on this board (A2)
- [ ] Confirm no PSRAM (or accept 16/17 exclusion anyway)
- [ ] Measure ECHO divider tap voltage per channel **before** connecting GPIO
- [ ] Measure LCD backpack pull-up rail voltage
- [ ] Measure LED resistor values fitted
- [ ] Choose and document the servo supply (rail, rating, measured voltage
      under servo stall)
- [ ] Verify HC-SR04 TRIG accepts 3.3 V HIGH (module batch datasheet)
- [ ] Photograph final wiring; add schematic + pin-table photo to `docs/hardware/`
- [ ] Bench-test each sensor alone (M6) before enabling any actuator

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
| Electrolytic capacitor ≥ 470 µF / 10 V + 100 nF ceramic | 1 + 2 | servo supply decoupling (§6) |
| Regulated 5 V / ≥ 1 A (prefer 2 A) power supply | 1 | servo + sensors (§6); measured budget TBD |
| Multimeter (verification of §10) | 1 | gate before first power-on |

### Required for later milestones (explicitly NOT available now)

| Item | Qty | Needed by | Effect if absent |
|---|---:|---|---|
| Obstruction sensor (IR break-beam or HC-SR04) for gate path → GPIO 14 | 1 | FR-005 full compliance (M6) | firmware keeps provisional guarded-close policy |
| Second sensing point per direction (2 × HC-SR04 or IR beams) | 2 | FR-003 confirmed passage (M1B/M6) | **no automatic counting** — occupancy changes only via explicit reconciliation |
| Fuse / power protection for the servo rail | 1 | M6 electrical hardening | desk-model only |

*Prices, suppliers and availability are intentionally empty: quoting without
supplier evidence would violate the project's evidence rules.*
