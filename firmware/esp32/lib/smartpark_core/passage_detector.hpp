#pragma once
/**
 * SmartPark-IoT V2 — lane passage state machine (FR-003).
 *
 * Blueprint FSM: IDLE → APPROACH → CONFIRMED | CANCELLED.
 *
 * CONFIRMED requires an ORDERED second sensing point (primary goes occupied
 * first, then secondary). The current hardware has only one beam per
 * direction, so the controller feeds SensorState::UNKNOWN as `secondary` and
 * CONFIRMED is structurally unreachable: approach alone NEVER counts.
 * That is the safe provisional behavior while the second beam is absent
 * (docs/hardware/pinmap-v2.md §11) — not something this milestone invents.
 *
 * A lane parked occupied at boot enters APPROACH once (advisory) and stays
 * there; re-triggering is guarded by APPROACH_DEBOUNCE_MS (V1's 2.5 s).
 */
#include <stdint.h>
#include "config.hpp"
#include "sensor_filter.hpp"

namespace smartpark {

enum class PassageState : uint8_t { IDLE = 0, APPROACH, CONFIRMED, CANCELLED };

inline const char* toString(PassageState s) {
  switch (s) {
    case PassageState::IDLE:      return "IDLE";
    case PassageState::APPROACH:  return "APPROACH";
    case PassageState::CONFIRMED: return "CONFIRMED";
    case PassageState::CANCELLED: return "CANCELLED";
  }
  return "INVALID_STATE";
}

/** Edge flags for one update; the controller turns them into queue events. */
struct PassageUpdate {
  bool approachStarted;
  bool cancelled;
  bool confirmed;
};

class PassageDetector {
public:
  PassageDetector()
      : state_(PassageState::IDLE), everApproached_(false),
        lastApproachMs_(0) {}

  PassageState update(SensorState primary, SensorState secondary,
                      uint32_t nowMs) {
    switch (state_) {
    case PassageState::IDLE:
    case PassageState::CANCELLED:
      if (primary == SensorState::VALID_OCCUPIED) {
        const bool guardOk =
            !everApproached_ ||
            (nowMs - lastApproachMs_ >= APPROACH_DEBOUNCE_MS);
        if (guardOk) {
          state_ = PassageState::APPROACH;
          everApproached_ = true;
          lastApproachMs_ = nowMs;
        } else if (state_ == PassageState::CANCELLED) {
          state_ = PassageState::IDLE; // consumed the cancellation
        }
      } else if (state_ == PassageState::CANCELLED) {
        state_ = PassageState::IDLE; // lane quiet again
      }
      break;

    case PassageState::APPROACH:
      if (secondary == SensorState::VALID_OCCUPIED) {
        // Ordered second point seen while approaching → direction confirmed.
        state_ = PassageState::CONFIRMED;
      } else if (primary == SensorState::VALID_FREE) {
        // Beam cleared without a second point → not a passage (approach only).
        state_ = PassageState::CANCELLED;
      }
      break;

    case PassageState::CONFIRMED:
      if (primary == SensorState::VALID_FREE &&
          secondary != SensorState::VALID_OCCUPIED) {
        state_ = PassageState::IDLE; // both points clear again
      }
      break;
    }
    return state_;
  }

  /**
   * Variant returning edge flags (used by the controller for events).
   */
  PassageState update(SensorState primary, SensorState secondary,
                      uint32_t nowMs, PassageUpdate& flags) {
    flags.approachStarted = false;
    flags.cancelled = false;
    flags.confirmed = false;
    PassageState before = state_;
    PassageState after = update(primary, secondary, nowMs);
    if (after != before) {
      if (after == PassageState::APPROACH) flags.approachStarted = true;
      if (after == PassageState::CANCELLED) flags.cancelled = true;
      if (after == PassageState::CONFIRMED) flags.confirmed = true;
    }
    return after;
  }

  PassageState state() const { return state_; }
  bool inApproach() const { return state_ == PassageState::APPROACH; }

private:
  PassageState state_;
  bool everApproached_;
  uint32_t lastApproachMs_;
};

} // namespace smartpark
