#pragma once
/**
 * SmartPark-IoT V2 pin assignment — hardware mirror of
 * docs/hardware/pinmap-v2.md (PROVISIONAL: verify against the physical
 * board before wiring; §10 of that document is the pre-power gate).
 *
 * Rules honored (V1 findings H-01/H-03): no UART0 TX (1), no flash (6-11),
 * no boot-strapping pins driven by peripherals (0/2/5/12/15), inputs-only
 * pins (34-39) used exclusively as ECHO inputs.
 *
 * M1B changes (see pinmap §3/§9/§12):
 *  - GPIO 4 (was spare) and GPIO 16 (was PSRAM-excluded) now carry the two
 *    second passage points. GPIO 16 is only safe on a classic DevKit with a
 *    WROOM-32 module (no PSRAM): assumption A3 must be verified — a
 *    WROVER/PSRAM board would conflict.
 *  - GPIO 14 (was reserved) is the obstruction/close-guard input.
 */
#include <stdint.h>
#include "config.hpp"

namespace smartpark {
namespace pins {

// Barrier servo — unchanged from V1 (GPIO 13 is conflict-free).
constexpr uint8_t SERVO = 13;

// Independent TRIG per sensor (V1's shared GPIO 3 trigger is gone, F-01).
constexpr uint8_t TRIG[SENSOR_COUNT] = {18, 19, 23, 25}; // entry, A, B, exit
// ECHO on input-only pins, AFTER the 5V->3.3V divider (H-02, pinmap §5).
constexpr uint8_t ECHO[SENSOR_COUNT] = {34, 35, 36, 39};

// Second passage points (M1B): digital IR beams, active-LOW, INPUT_PULLUP
// (occupied = LOW; a disconnected input reads HIGH = free = never confirms,
// which fails toward "do not count" — pinmap §9). Named *_PIN because the
// bare BEAM_ENTRY/BEAM_EXIT identifiers are the controller's *indices*.
constexpr uint8_t BEAM_ENTRY_PIN = 4; // E2, past the barrier (was SPARE)
constexpr uint8_t BEAM_EXIT_PIN = 16; // X2, outer exit (was PSRAM-excluded)

// Obstruction/close-guard input (M1B): HIGH = blocked or faulted (fail-safe
// pull-up bias: broken wire / open NC loop reads HIGH), LOW = clear (must be
// actively driven). Firmware enables the internal pull-up; the physical board
// adds an external 10k pull-up on the same rail — pinmap §12. Deliberate
// asymmetry with the beams (§9): an OPEN input fails toward BLOCKED here
// (never close) but toward FREE there (never count) — both fail toward the
// safe action for their subsystem.
constexpr uint8_t OBSTRUCTION_PIN = 14; // was RESERVED_OBSTRUCTION

// Status LEDs — series 220-330 Ω mandatory (H-06).
constexpr uint8_t LED_GREEN = 26; // known free spaces (V1 unchanged)
constexpr uint8_t LED_RED = 27;   // known full (V1 unchanged)
constexpr uint8_t LED_ZONE_A = 32; // V1 zone A was GPIO 25 (now a TRIG)
constexpr uint8_t LED_ZONE_B = 33; // V1 unchanged

// 16x2 I2C LCD (PCF8574 backpack, address 0x27; pull-up rail check §8).
constexpr uint8_t LCD_SDA = 21; // default I2C0 — documented, not remapped
constexpr uint8_t LCD_SCL = 22;
constexpr uint8_t LCD_ADDR = 0x27;

// Serial console (FR-011 groundwork): UART0 TX stays the log channel;
// RX (GPIO 3) accepts the two-line console ("R <n>" reconcile, "?" status).
// Never reconfigured; no other peripheral may use UART0 (H-01).
constexpr uint8_t CONSOLE_RX = 3;

} // namespace pins
} // namespace smartpark
