#pragma once
/**
 * SmartPark-IoT V2 — barrier (gate) finite-state machine (FR-005).
 *
 * States (exactly the requirement set):
 *   CLOSED → OPENING → OPEN → CLOSING → CLOSED
 *   BLOCKED  — a close is wanted but the lane is not observably clear
 *   FAULT    — actuator reported a problem at attach time (latched)
 *   MANUAL   — reserved for future local operator control (unreachable in M1A)
 *
 * Safety properties implemented here:
 *  - No `delay()`: sweeps advance by BARRIER_STEP_MS/BARRIER_STEP_DEG inside
 *    tick(); all timing is wrap-safe unsigned millis() arithmetic (F-05).
 *  - NO blind timed close (V1 F-04): a closing sweep starts only after the
 *    entry lane read a *valid* FREE for LANE_CLEAR_CONFIRM_MS, and ANY
 *    non-FREE reading (OCCUPIED/UNKNOWN/FAULT/INIT) during the sweep aborts
 *    it: BLOCKED → auto-reopen (F-04 provisional guarded close; this is NOT
 *    a certified obstruction sensor — docs/hardware/pinmap-v2.md §9).
 *  - A lane that never reports valid FREE leaves the gate OPEN (fails open
 *    toward traffic safety; documented trade-off).
 *  - Pure logic (native-testable); the hardware glue applies `angle()`.
 */
#include <stdint.h>
#include "config.hpp"
#include "sensor_filter.hpp"

namespace smartpark {

enum class BarrierState : uint8_t {
  CLOSED = 0,
  OPENING,
  OPEN,
  CLOSING,
  BLOCKED,
  FAULT,
  MANUAL,
};

inline const char* toString(BarrierState s) {
  switch (s) {
    case BarrierState::CLOSED:   return "CLOSED";
    case BarrierState::OPENING:  return "OPENING";
    case BarrierState::OPEN:     return "OPEN";
    case BarrierState::CLOSING:  return "CLOSING";
    case BarrierState::BLOCKED:  return "BLOCKED";
    case BarrierState::FAULT:    return "FAULT";
    case BarrierState::MANUAL:   return "MANUAL";
  }
  return "INVALID_STATE";
}

class BarrierController {
public:
  BarrierController();

  /** Ask the gate to open (entry approach with admission allowed). */
  void requestOpen(uint32_t nowMs);

  /**
   * Ask the gate to close. Executes only when the lane is observably clear
   * (LANE_CLEAR_CONFIRM_MS); otherwise the FSM surfaces BLOCKED. Ignored in
   * FAULT/MANUAL.
   */
  void requestClose(uint32_t nowMs);

  /**
   * Latch FAULT (e.g. servo attach failed). Clears only on reboot in M1A.
   * Also drops pending automated intents so none can outlive the latch.
   */
  void setActuatorFault();

  /**
   * Enter/leave MANUAL — reserved; no M1A code path reaches these.
   * setManual() cancels pending automated intents (the operator owns the
   * gate, so a stale close/abort request must not fire after the release).
   * exitManual(nowMs) resolves the physical position deterministically:
   * fully open → OPEN, fully closed → CLOSED, mid-sweep → resume the
   * guarded close (lane not observably clear ⇒ BLOCKED → reopen: fails
   * open). The reported state therefore always matches `angle()`.
   */
  void setManual();
  void exitManual(uint32_t nowMs);

  /**
   * Advance the FSM.
   * @param laneEntry effective entry-lane state (stale-aware UNKNOWN ok)
   */
  void tick(uint32_t nowMs, SensorState laneEntry);

  BarrierState state() const { return state_; }

  /** Servo command, 0 = closed … BARRIER_OPEN_ANGLE = open. */
  uint8_t angle() const { return angle_; }

  /** Count of aborted closing sweeps (diagnostics / tests; saturates at 255). */
  uint8_t abortCount() const { return aborts_; }

  bool isMoving() const {
    return state_ == BarrierState::OPENING || state_ == BarrierState::CLOSING;
  }

private:
  void startSweep(BarrierState sweeping, uint32_t nowMs);
  void updateLaneFreeTimer(uint32_t nowMs, SensorState laneEntry);

  BarrierState state_;
  uint8_t angle_;
  uint32_t sweepStartMs_;
  uint8_t sweepStartAngle_;
  uint32_t laneFreeSinceMs_;
  bool laneFreeTimerActive_;
  bool pendingClose_;
  bool abortReopen_;   // BLOCKED came from an aborted sweep → reopen next tick
  uint8_t aborts_;
};

} // namespace smartpark
