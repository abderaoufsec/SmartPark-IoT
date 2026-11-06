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

/**
 * Second passage points (M1B): digital IR beams, NOT ultrasonic slots — they
 * carry no distance and are never part of the round-robin schedule. Array
 * indices for the controller's beam table; the SENSOR_BEAM_* values are the
 * event sensorIds (outside the SENSOR_COUNT ultrasonic range on purpose).
 */
constexpr uint8_t BEAM_COUNT = 2;
constexpr uint8_t BEAM_ENTRY = 0; // secondary point of the entry lane (GPIO 4)
constexpr uint8_t BEAM_EXIT = 1;  // secondary point of the exit lane (GPIO 16)
constexpr uint8_t SENSOR_BEAM_ENTRY = 4; // event sensorId (not a slot index!)
constexpr uint8_t SENSOR_BEAM_EXIT = 5;
constexpr uint8_t SENSOR_OBSTRUCTION = 6; // event sensorId for the close guard

/** Raw beam/obstruction level must be stable this long before it is trusted
 *  (contact chatter, threshold edge chatter). PROVISIONAL. */
constexpr uint32_t BEAM_DEBOUNCE_MS = 50;

// ---------------- Passage detection ----------------
/**
 * Minimum time between two APPROACH starts on the same lane (V1's 2.5 s
 * debounce, kept). Protects against a flapping beam re-triggering instantly.
 */
constexpr uint32_t APPROACH_DEBOUNCE_MS = 2500;

/**
 * After the primary point clears, the SECOND point may still be reached by a
 * vehicle whose length is shorter than the point spacing (gap crossing). The
 * detector waits this long for the ordered second-point transition before
 * declaring CANCELLED (reversal / incomplete passage, FR-003). Only applies
 * while the second point is a live sensor (VALID_FREE): with the second point
 * absent (UNKNOWN) there is nothing to wait for and the M1A behavior —
 * cancel at once — is kept. PROVISIONAL (desk-model scale, M6 calibration).
 */
constexpr uint32_t PASSAGE_GRACE_MS = 3000;

/**
 * An APPROACH may hold through a transient primary failure (stale/timeout),
 * but if the primary stays non-classifiable for this long the approach is
 * abandoned (CANCELLED) so a dead sensor cannot pin a lane forever.
 * PROVISIONAL. Wrap-safe unsigned arithmetic.
 */
constexpr uint32_t PASSAGE_UNKNOWN_TOLERANCE_MS = 5000;

/**
 * A confirmed passage requires an ordered second sensing point
 * (requirements FR-003). Since M1B the second points exist (IR beams on
 * GPIO 4/16); until they are wired the controller feeds UNKNOWN and
 * CONFIRMED stays unreachable — approach alone NEVER counts (see
 * docs/hardware/pinmap-v2.md §9/§11).
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
 * (docs/hardware/pinmap-v2.md §12.2).
 */
constexpr uint32_t LANE_CLEAR_CONFIRM_MS = 3000;

/**
 * The obstruction input (M1B, GPIO 14) must have been sampled within this
 * window to be trusted; otherwise the barrier sees ObstructionState::UNKNOWN
 * which can never authorize a closing sweep. The glue feeds it every loop(),
 * so >1 s without a sample means the loop itself stalled. Reuses the sensor
 * staleness scale deliberately (same "silent == unknown" rule, FR-001).
 */
constexpr uint32_t OBSTRUCTION_STALE_MS = SENSOR_STALE_MS;

// ---------------- Display / indicators ----------------
constexpr uint32_t LCD_INTERVAL_MS = 500;
constexpr uint32_t LED_BLINK_MS = 500; // UNKNOWN/FAULT zone-LED blink period

// ---------------- Events ----------------
/** Fixed-capacity event ring — no dynamic allocation anywhere. */
constexpr size_t EVENT_QUEUE_CAP = 16;

} // namespace smartpark
