#pragma once
/**
 * SmartPark-IoT V2 — occupancy state (FR-004 bounds, FR-007/NFR-004 honesty).
 *
 * - Boots UNKNOWN with reason NO_PERSISTED_STATE: persistence (NVS) lands in
 *   M1B, and NFR-004 forbids inventing free spaces after a reboot. While
 *   UNKNOWN the controller defers admission (FR-004) — intended, not a bug.
 * - Count is bounded to [0, CAPACITY]. Out-of-range attempts are REJECTED and
 *   counted — never silently clamped (V1 finding F-10).
 * - In M1A the count changes only through reconcile() (explicit operator/
 *   system action) or a CONFIRMED passage (unreachable with current
 *   hardware). Approach never changes it (FR-003).
 */
#include <stdint.h>
#include "config.hpp"

namespace smartpark {

enum class OccupancyStatus : uint8_t { UNKNOWN = 0, KNOWN };

enum class OccUnknownReason : uint8_t {
  NONE = 0,           // status is KNOWN
  NO_PERSISTED_STATE, // boot without persisted state (M1A/M1B-NVS pending)
};

class OccupancyManager {
public:
  OccupancyManager()
      : status_(OccupancyStatus::UNKNOWN),
        reason_(OccUnknownReason::NO_PERSISTED_STATE), count_(0),
        rejected_(0) {}

  /** Explicit reconciliation. Returns false (and counts a rejection) when
   *  newCount > CAPACITY — no silent clamping (F-10). */
  bool reconcile(uint8_t newCount) {
    if (newCount > static_cast<uint8_t>(CAPACITY)) {
      ++rejected_;
      return false;
    }
    count_ = newCount;
    status_ = OccupancyStatus::KNOWN;
    reason_ = OccUnknownReason::NONE;
    return true;
  }

  /**
   * Apply a *confirmed* passage delta (+1 entry / −1 exit).
   * Returns false when the result would leave [0, CAPACITY] (rejected_++).
   * Never invoked without a CONFIRMED passage event.
   */
  bool change(int8_t delta) {
    if (status_ != OccupancyStatus::KNOWN) {
      ++rejected_;
      return false; // cannot drift an unknown count — reconcile first
    }
    const int next = static_cast<int>(count_) + delta;
    if (next < 0 || next > CAPACITY) {
      ++rejected_;
      return false;
    }
    count_ = static_cast<uint8_t>(next);
    return true;
  }

  /** Invalidate the count (e.g. future sensor-failure policy, tests). */
  void markUnknown() {
    status_ = OccupancyStatus::UNKNOWN;
    reason_ = OccUnknownReason::NO_PERSISTED_STATE;
    count_ = 0;
  }

  bool isKnown() const { return status_ == OccupancyStatus::KNOWN; }
  OccupancyStatus status() const { return status_; }
  OccUnknownReason reason() const { return reason_; }

  /** Valid only when isKnown(); returns 0 otherwise (callers check first). */
  uint8_t count() const { return isKnown() ? count_ : 0; }

  /** Valid only when isKnown(); returns 0 otherwise. */
  uint8_t freeSpaces() const {
    return isKnown() ? static_cast<uint8_t>(CAPACITY - count_) : 0;
  }

  uint32_t rejectedCount() const { return rejected_; }

private:
  OccupancyStatus status_;
  OccUnknownReason reason_;
  uint8_t count_;
  uint32_t rejected_;
};

} // namespace smartpark
