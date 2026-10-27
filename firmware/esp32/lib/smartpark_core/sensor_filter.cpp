#include "sensor_filter.hpp"

namespace smartpark {

SensorFilter::SensorFilter()
    : started_(false), hasClassified_(false), fault_(false),
      classified_(SensorState::INIT), lastCm_(-1), validStable_(0),
      invalidCount_(0), pendingCount_(0), lastMeasMs_(0),
      hasMeasurement_(false) {}

SensorState SensorFilter::classify(long d) const {
  if (hasClassified_ && classified_ == SensorState::VALID_OCCUPIED) {
    // Leave OCCUPIED only beyond the free threshold (hysteresis).
    return d > FREE_ENTER_CM ? SensorState::VALID_FREE
                             : SensorState::VALID_OCCUPIED;
  }
  if (hasClassified_ && classified_ == SensorState::VALID_FREE) {
    // Leave FREE only below the occupied threshold.
    return d < OCCUPIED_ENTER_CM ? SensorState::VALID_OCCUPIED
                                 : SensorState::VALID_FREE;
  }
  // Fresh classification (or recovering from UNKNOWN/FAULT):
  // inside the ambiguous band [OCCUPIED_ENTER_CM, FREE_ENTER_CM] prefer
  // OCCUPIED — never advertise a space as free on an ambiguous sample.
  if (d < OCCUPIED_ENTER_CM) return SensorState::VALID_OCCUPIED;
  if (d > FREE_ENTER_CM) return SensorState::VALID_FREE;
  return SensorState::VALID_OCCUPIED;
}

bool SensorFilter::update(long distanceCm, uint32_t nowMs) {
  SensorState before = publicState();
  started_ = true;
  hasMeasurement_ = true;
  lastMeasMs_ = nowMs;

  if (distanceCm < 0) {
    // Timeout / invalid: UNKNOWN immediately, never FREE (V1 F-03).
    validStable_ = 0;
    if (invalidCount_ < 255) ++invalidCount_;
    if (invalidCount_ >= FAULT_AFTER_TIMEOUTS) fault_ = true;
  } else {
    lastCm_ = distanceCm;
    invalidCount_ = 0;
    if (validStable_ < 255) ++validStable_;

    if (validStable_ >= STABLE_READINGS) {
      // We now hold a run of trusted samples.
      fault_ = false; // clears only after a trusted run (not on first sample)
      SensorState want = classify(distanceCm);
      if (!hasClassified_ || want == classified_) {
        classified_ = want;
        hasClassified_ = true;
        pendingCount_ = 0;
      } else {
        // A *change* of trusted classification needs STABLE_READINGS
        // consecutive agreeing samples (noise cannot flip the state).
        if (pendingCount_ < 255) ++pendingCount_;
        if (pendingCount_ >= STABLE_READINGS) {
          classified_ = want;
          hasClassified_ = true;
          pendingCount_ = 0;
        }
      }
    } else {
      pendingCount_ = 0;
    }
  }
  return before != publicState();
}

SensorState SensorFilter::publicState() const {
  if (!started_) return SensorState::INIT;
  if (fault_) return SensorState::FAULT;
  if (!hasClassified_ || validStable_ < STABLE_READINGS)
    return SensorState::UNKNOWN;
  return classified_;
}

SensorState SensorFilter::stateAt(uint32_t nowMs) const {
  if (!started_) return SensorState::INIT;
  // Wrap-safe: unsigned subtraction works across millis() rollover.
  if (hasMeasurement_ && (nowMs - lastMeasMs_ > SENSOR_STALE_MS))
    return SensorState::UNKNOWN;
  return publicState();
}

} // namespace smartpark
