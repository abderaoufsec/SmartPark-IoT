#pragma once
/**
 * SmartPark-IoT V2 pin assignment — hardware mirror of
 * docs/hardware/pinmap-v2.md (PROVISIONAL: verify against the physical
 * board before wiring; §10 of that document is the pre-power gate).
 *
 * Rules honored (V1 findings H-01/H-03): no UART0 (1/3), no flash (6-11),
 * no boot-strapping pins driven by peripherals (0/2/5/12/15), no PSRAM
 * (16/17), inputs-only pins (34-39) used exclusively as ECHO inputs.
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

// Status LEDs — series 220-330 Ω mandatory (H-06).
constexpr uint8_t LED_GREEN = 26; // known free spaces (V1 unchanged)
constexpr uint8_t LED_RED = 27;   // known full (V1 unchanged)
constexpr uint8_t LED_ZONE_A = 32; // V1 zone A was GPIO 25 (now a TRIG)
constexpr uint8_t LED_ZONE_B = 33; // V1 unchanged

// 16x2 I2C LCD (PCF8574 backpack, address 0x27; pull-up rail check §8).
constexpr uint8_t LCD_SDA = 21; // default I2C0 — documented, not remapped
constexpr uint8_t LCD_SCL = 22;
constexpr uint8_t LCD_ADDR = 0x27;

// Reserved / spare (pinmap §3): obstruction input is NOT populated in M1A.
constexpr uint8_t RESERVED_OBSTRUCTION = 14;
constexpr uint8_t SPARE = 4;

} // namespace pins
} // namespace smartpark
