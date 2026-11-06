#pragma once
/**
 * SmartPark-IoT V2 — lane passage state machine (FR-003), M1B two-point.
 *
 * Blueprint FSM: IDLE → APPROACH → CONFIRMED | CANCELLED (+ RE-ARM wait
 * implemented as "CONFIRMED persists until the primary point releases").
 *
 * A CONFIRMED passage requires an ORDERED second sensing point:
 *   - the approach must have started on the primary point (E1/X1), and
 *   - the secondary (E2/X2) transition to occupied is accepted only when the
 *     secondary was NOT already occupied when the approach began, or it has
 *     been observably FREE during this approach. This blocks a stuck-high
 *     secondary from fabricating counts (ordered sequence, FR-003).
 *
 * Reversal / incomplete passage: when the primary clears, a gap-crossing
 * vehicle may still reach the secondary, so cancellation waits
 * PASSAGE_GRACE_MS — but only while the secondary is a live sensor
 * (VALID_FREE). With the secondary absent/failed (UNKNOWN/FAULT) there is
 * nothing to wait for: cancel at once (M1A behavior preserved).
 *
 * A primary that fails mid-approach (UNKNOWN/FAULT/INIT) holds the approach
 * for PASSAGE_UNKNOWN_TOLERANCE_MS before cancelling — a dead sensor must
 * not pin a lane, and a legitimate car waiting on the point is never
 * "timed out" while it still reads OCCUPIED.
 *
 * After CONFIRMED the detector waits for the primary to read FREE again
 * before returning to IDLE: one physical crossing cannot re-arm a second
 * count (anti-double-count).
 *
 * Pure logic, wrap-safe uint32 millis() arithmetic, native-testable.
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
        lastApproachMs_(0), secondaryStartOccupied_(false),
        secondarySeenFree_(false), graceActive_(false), graceStartMs_(0),
        unknownActive_(false), unknownStartMs_(0) {}

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
          // Fresh per-approach ordering context (stuck-secondary guard).
          secondaryStartOccupied_ = (secondary == SensorState::VALID_OCCUPIED);
          secondarySeenFree_ = false;
          graceActive_ = false;
          unknownActive_ = false;
        } else if (state_ == PassageState::CANCELLED) {
          state_ = PassageState::IDLE; // consumed the cancellation
        }
      } else if (state_ == PassageState::CANCELLED) {
        state_ = PassageState::IDLE; // lane quiet again
      }
      break;

    case PassageState::APPROACH: {
      // 1. Ordered confirmation (highest priority): the secondary must show
      //    a FREE->OCCUPIED transition this approach, or must not have been
      //    occupied when the approach started.
      if (secondary == SensorState::VALID_OCCUPIED &&
          (!secondaryStartOccupied_ || secondarySeenFree_)) {
        state_ = PassageState::CONFIRMED;
        break;
      }
      // 2. Track the ordering observation.
      if (secondary == SensorState::VALID_FREE) secondarySeenFree_ = true;

      // 3. Primary still on the point: the vehicle is legitimately waiting
      //    (e.g. for the gate) — no timeout while it is observably there.
      if (primary == SensorState::VALID_OCCUPIED) {
        graceActive_ = false;
        unknownActive_ = false;
        break;
      }

      if (primary == SensorState::VALID_FREE) {
        // 4. Primary released. A live secondary may still be reached by a
        //    short vehicle (gap crossing) => grace window. No live secondary
        //    (UNKNOWN/FAULT, or already occupied-but-unconfirmable) =>
        //    nothing to wait for => cancel now (reversal / incomplete).
        unknownActive_ = false;
        if (secondary != SensorState::VALID_FREE) {
          state_ = PassageState::CANCELLED;
        } else if (!graceActive_) {
          graceActive_ = true;
          graceStartMs_ = nowMs;
        } else if (nowMs - graceStartMs_ >= PASSAGE_GRACE_MS) {
          state_ = PassageState::CANCELLED; // incomplete passage: no E2 hit
        }
        break;
      }

      // 5. Primary not classifiable (UNKNOWN/FAULT/INIT): bounded hold.
      if (!unknownActive_) {
        unknownActive_ = true;
        unknownStartMs_ = nowMs;
      } else if (nowMs - unknownStartMs_ >= PASSAGE_UNKNOWN_TOLERANCE_MS) {
        state_ = PassageState::CANCELLED;
      }
      break;
    }

    case PassageState::CONFIRMED:
      // Re-arm only when the primary point observably releases: a vehicle
      // still straddling the primary cannot start a second approach.
      if (primary != SensorState::VALID_OCCUPIED) {
        state_ = PassageState::IDLE;
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
  bool secondaryStartOccupied_; // secondary state captured at approach start
  bool secondarySeenFree_;      // secondary observably FREE this approach
  bool graceActive_;            // primary released; waiting on the secondary
  uint32_t graceStartMs_;
  bool unknownActive_;          // primary unclassifiable; bounded hold
  uint32_t unknownStartMs_;
};

} // namespace smartpark
