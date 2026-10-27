#pragma once
/**
 * SmartPark-IoT V2 — central configuration constants.
 *
 * Pure header, no Arduino dependencies (native-testable).
 * Threshold values inherited from V1 are PROVISIONAL: they were never
 * calibrated against the physical model (see docs/hardware/v1-audit.md #6).
 * Calibration is an M6 task; changing a value here changes behavior for both
 * the ESP32 build and the unit tests.
 */
#include <stddef.h>
#include <stdint.h>

namespace smartpark {

// ---------------- Occupancy ----------------
/** Nominal capacity of the demonstrator (unchanged from V1). */
constexpr int CAPACITY = 6;

// ---------------- Ultrasonic classification (cm) ----------------
/**
 * Hysteresis model (fixes V1 findings F-03, F-08):
 *  - state becomes OCCUPIED when distance <  OCCUPIED_ENTER_CM
 *  - an OCCUPIED state only returns to FREE when distance > FREE_ENTER_CM
 *  - a fresh/unknown classification inside the band [15, 30] is treated as
 *    OCCUPIED (conservative: never call a space free on an ambiguous sample)
 *  - a timeout (distance < 0) is UNKNOWN immediately, never FREE (F-03)
 */
constexpr long OCCUPIED_ENTER_CM = 15; // was V1 SEUIL_VOITURE
constexpr long FREE_ENTER_CM = 30;     // was V1 SEUIL_ZONE_LIBRE

/** Consecutive agreeing samples required to trust/commit a classification. */
constexpr uint8_t STABLE_READINGS = 3;

/** Timeout/invalid samples before a sensor escalates UNKNOWN -> FAULT. */
constexpr uint8_t FAULT_AFTER_TIMEOUTS = 10;

/**
 * A classification older than this (no successful measurement) is reported as
 * UNKNOWN even if the last sample said FREE/OCCUPIED (stale = unknown).
 */
constexpr uint32_t SENSOR_STALE_MS = 1000;

/** pulseIn() per-sample blocking bound (µs) — 30 ms worst case per slot. */
constexpr uint32_t ECHO_TIMEOUT_US = 30000;

/**
 * One sensor is measured per slot; slots are staggered so no two transducers
 * fire together (V1 F-01 cross-talk fix). 80 ms > HC-SR04 60 ms re-trigger
 * guard and > ECHO timeout. Full round = 4 × 80 ms = 320 ms.
 */
constexpr uint32_t SENSOR_SLOT_MS = 80;

// ---------------- Sensors layout ----------------
constexpr uint8_t SENSOR_COUNT = 4;
constexpr uint8_t SENSOR_ENTRY = 0;
constexpr uint8_t SENSOR_ZONE_A = 1;
constexpr uint8_t SENSOR_ZONE_B = 2;
constexpr uint8_t SENSOR_EXIT = 3;

// ---------------- Passage detection ----------------
/**
 * Minimum time between two APPROACH starts on the same lane (V1's 2.5 s
 * debounce, kept). Protects against a flapping beam re-triggering instantly.
 */
constexpr uint32_t APPROACH_DEBOUNCE_MS = 2500;

/**
 * A confirmed passage requires an ordered second sensing point
 * (requirements FR-003). With the current 4-sensor hardware the second point
 * does not exist: the controller feeds UNKNOWN, so CONFIRMED is unreachable
 * and occupancy never changes automatically. This is a documented hardware
 * limitation, not a missing feature (docs/hardware/pinmap-v2.md §9/§11).
 */

// ---------------- Barrier ----------------
/** Servo sweep speed: +2° every 15 ms (same feel as V1, but non-blocking). */
constexpr uint32_t BARRIER_STEP_MS = 15;
constexpr uint8_t BARRIER_STEP_DEG = 2;
constexpr uint8_t BARRIER_OPEN_ANGLE = 90;
constexpr uint8_t BARRIER_CLOSED_ANGLE = 0;

/**
 * The lane must read a *valid FREE* for this long before a closing sweep may
 * start, and any non-FREE reading during the sweep aborts it (BLOCKED ->
 * reopen). This replaces V1's blind delay(2000) close (finding F-04).
 * PROVISIONAL: the entry beam is not a certified obstruction sensor
 * (docs/hardware/pinmap-v2.md §9).
 */
constexpr uint32_t LANE_CLEAR_CONFIRM_MS = 3000;

// ---------------- Display / indicators ----------------
constexpr uint32_t LCD_INTERVAL_MS = 500;
constexpr uint32_t LED_BLINK_MS = 500; // UNKNOWN/FAULT zone-LED blink period

// ---------------- Events ----------------
/** Fixed-capacity event ring — no dynamic allocation anywhere. */
constexpr size_t EVENT_QUEUE_CAP = 16;

} // namespace smartpark
