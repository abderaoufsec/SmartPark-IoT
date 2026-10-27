# SmartPark-IoT firmware (ESP32)

PlatformIO project for the ESP32 controller. **Status: M0 baseline** — this
directory compiles the original V1 sketch logic; it is not yet the V2 firmware.

## Build

```bash
cd firmware/esp32
pio run                 # compile (espressif32@6.13.0, Arduino framework)
pio run -t upload       # flash over USB (board: esp32dev / "ESP32 Dev Module")
pio device monitor      # serial monitor, 115200 baud
```

## Contents

| File | Purpose |
|---|---|
| `platformio.ini` | Pinned platform, board and library versions |
| `src/main.cpp` | V1 firmware logic, verbatim, as the active baseline |

## Relationship to `legacy/v1/`

- `legacy/v1/parking_intelligent.ino` is the **immutable archive** of the
  original academic sketch (SHA-256 `d5cedf92…0c71ebd`). Never edit it.
- `src/main.cpp` is a **copy for the V2 baseline** so PlatformIO can build it.
  Its only differences from the archived file are:
  1. `#include <Arduino.h>` added as the first line (required because PlatformIO
     compiles `.cpp` without the Arduino IDE's implicit include and prototype
     generation);
  2. one trailing newline at end of file.

  The logic itself is unchanged; no behavior was altered for M0.

## Compile status

The claim "this project compiles" is only valid when backed by a real, recent
build log (local `pio run` or the GitHub Actions
`firmware-ci` workflow). See `docs/milestones/m0.md` for the verification
record of this milestone.

## Known hardware warnings

The V1 pin assignment and electrical design carry documented safety risks
(UART0 conflict on GPIO 3, 5 V echo levels into 3.3 V-only pins, shared
ultrasonic trigger, unsafe timed barrier close, …). They are deliberately
**not** fixed in M0. Read `docs/hardware/v1-audit.md` before powering
anything.
