#pragma once
/**
 * SmartPark-IoT V2 — per-sensor classification filter.
 *
 * Pure logic (native-testable). Implements the blueprint sensor FSM:
 *
 *   INIT ──valid samples──► VALID_OCCUPIED / VALID_FREE   (hysteresis + N-stable)
 *   any  ──timeout────────► UNKNOWN   (immediately — never FREE, V1 F-03)
 *   UNKNOWN ──FAULT_AFTER_TIMEOUTS consecutive──► FAULT
 *   FAULT ──STABLE_READINGS valid samples──► classified again
 *   any classification older than SENSOR_STALE_MS ──► reported UNKNOWN
 *
 * Distances are centimetres; a value < 0 means "timeout / invalid sample".
 */
#include <stdint.h>
#include "config.hpp"

namespace smartpark {

enum class SensorState : uint8_t {
  INIT = 0,        // no measurement yet
  VALID_FREE,      // trusted measurement, space/lane clear
  VALID_OCCUPIED,  // trusted measurement, object present
  UNKNOWN,         // timeout, invalid, not yet trusted, or stale
  FAULT,           // persistent failures (FAULT_AFTER_TIMEOUTS in a row)
};

inline const char* toString(SensorState s) {
  switch (s) {
    case SensorState::INIT:           return "INIT";
    case SensorState::VALID_FREE:     return "FREE";
    case SensorState::VALID_OCCUPIED: return "OCCUPIED";
    case SensorState::UNKNOWN:        return "UNKNOWN";
    case SensorState::FAULT:          return "FAULT";
  }
  return "INVALID_STATE";
}

class SensorFilter {
public:
  SensorFilter();

  /**
   * Feed one measurement for this sensor.
   * @param distanceCm centimetres, or < 0 for timeout/invalid
   * @param nowMs      current millis()
   * @return true if the *reported* state changed (for event generation)
   */
  bool update(long distanceCm, uint32_t nowMs);

  /** Reported state without staleness (callers should prefer stateAt). */
  SensorState state() const { return publicState(); }

  /** Reported state with staleness: silent sensors become UNKNOWN (FR-001). */
  SensorState stateAt(uint32_t nowMs) const;

  /** Last valid distance in cm, or -1 if none was ever received. */
  long lastDistanceCm() const { return hasMeasurement_ ? lastCm_ : -1; }

  /** millis() of the last update() call (0 if never called). */
  uint32_t lastMeasurementMs() const { return lastMeasMs_; }

  /** Consecutive invalid samples since the last valid one (diagnostics). */
  uint8_t invalidStreak() const { return invalidCount_; }

private:
  SensorState publicState() const;
  SensorState classify(long d) const; // hysteresis decision for a valid d

  bool started_;          // at least one update() seen
  bool hasClassified_;    // classified_ holds a trusted VALID_* value
  bool fault_;            // persistent failure latch
  SensorState classified_; // last trusted VALID_FREE / VALID_OCCUPIED
  long lastCm_;           // last valid distance
  uint8_t validStable_;   // consecutive valid samples (saturating)
  uint8_t invalidCount_;  // consecutive invalid samples (saturating)
  uint8_t pendingCount_;  // consecutive samples agreeing with a new state
  uint32_t lastMeasMs_;
  bool hasMeasurement_;
};

} // namespace smartpark
