#include "barrier_controller.hpp"

namespace smartpark {

BarrierController::BarrierController()
    : state_(BarrierState::CLOSED), angle_(BARRIER_CLOSED_ANGLE),
      sweepStartMs_(0), sweepStartAngle_(BARRIER_CLOSED_ANGLE),
      laneFreeSinceMs_(0), laneFreeTimerActive_(false),
      pendingClose_(false), abortReopen_(false), aborts_(0) {}

void BarrierController::setActuatorFault() {
  pendingClose_ = false; // no automated intent may outlive the latch
  abortReopen_ = false;
  state_ = BarrierState::FAULT;
}

void BarrierController::setManual() {
  if (state_ != BarrierState::FAULT) {
    pendingClose_ = false; // operator takeover cancels automated intents:
    abortReopen_ = false;  // they must not fire after exitManual()
    state_ = BarrierState::MANUAL;
  }
}

void BarrierController::exitManual(uint32_t nowMs) {
  if (state_ != BarrierState::MANUAL) return;
  pendingClose_ = false;
  abortReopen_ = false;
  if (angle_ == BARRIER_OPEN_ANGLE) {
    state_ = BarrierState::OPEN;
    laneFreeTimerActive_ = false; // re-observe the lane from now
    laneFreeSinceMs_ = nowMs;
  } else if (angle_ == BARRIER_CLOSED_ANGLE) {
    state_ = BarrierState::CLOSED;
  } else {
    // Released mid-sweep: resume the guarded close from the current angle.
    // The CLOSING lane guard applies on the next tick, so an unknown or
    // occupied lane aborts to BLOCKED and reopens — fails open, and the
    // reported state always matches the physical angle (never "CLOSED"
    // while the servo still holds a mid-position).
    startSweep(BarrierState::CLOSING, nowMs);
  }
}

void BarrierController::startSweep(BarrierState sweeping, uint32_t nowMs) {
  sweepStartMs_ = nowMs;
  sweepStartAngle_ = angle_;
  state_ = sweeping;
}

void BarrierController::requestOpen(uint32_t nowMs) {
  switch (state_) {
  case BarrierState::CLOSED:
  case BarrierState::BLOCKED:
  case BarrierState::CLOSING:
    pendingClose_ = false;
    abortReopen_ = false;
    laneFreeTimerActive_ = false; // a fresh open invalidates the lane timer
    startSweep(BarrierState::OPENING, nowMs);
    break;
  case BarrierState::OPEN:
  case BarrierState::OPENING:
    break; // already doing the right thing
  case BarrierState::FAULT:
  case BarrierState::MANUAL:
    break; // never actuate against a latched fault / manual ownership
  }
}

void BarrierController::requestClose(uint32_t nowMs) {
  (void)nowMs;
  switch (state_) {
  case BarrierState::OPEN:
    pendingClose_ = true; // evaluated in tick() against the lane state
    break;
  case BarrierState::CLOSED:
  case BarrierState::OPENING:
  case BarrierState::CLOSING:
  case BarrierState::BLOCKED:
  case BarrierState::FAULT:
  case BarrierState::MANUAL:
    break; // close intents are meaningless outside OPEN
  }
}

void BarrierController::updateLaneFreeTimer(uint32_t nowMs,
                                            SensorState laneEntry) {
  if (laneEntry == SensorState::VALID_FREE) {
    if (!laneFreeTimerActive_) {
      laneFreeTimerActive_ = true;
      laneFreeSinceMs_ = nowMs; // wrap-safe baseline
    }
  } else {
    laneFreeTimerActive_ = false;
  }
}

void BarrierController::tick(uint32_t nowMs, SensorState laneEntry,
                             ObstructionState obstruction) {
  // The obstruction input may only authorize a close when it is a *fresh*
  // CLEAR: UNKNOWN (stale/never sampled) and BLOCKED are equivalent here —
  // an unverified safety input can never start or finish a closing sweep.
  switch (state_) {
  case BarrierState::FAULT:
  case BarrierState::MANUAL:
  case BarrierState::CLOSED:
    return; // nothing moves; CLOSED is the resting state

  case BarrierState::OPENING: {
    const uint32_t steps = (nowMs - sweepStartMs_) / BARRIER_STEP_MS;
    const uint32_t target =
        static_cast<uint32_t>(sweepStartAngle_) +
        steps * static_cast<uint32_t>(BARRIER_STEP_DEG);
    if (target >= BARRIER_OPEN_ANGLE) {
      angle_ = BARRIER_OPEN_ANGLE;
      state_ = BarrierState::OPEN;
      laneFreeTimerActive_ = false; // start observing the lane afresh
      laneFreeSinceMs_ = nowMs;
    } else {
      angle_ = static_cast<uint8_t>(target);
    }
    return;
  }

  case BarrierState::OPEN:
    updateLaneFreeTimer(nowMs, laneEntry);
    // Auto-close ONLY on sustained, valid, observed lane-FREE *and* a fresh
    // CLEAR obstruction — never on a bare delay expiry (V1 F-04). UNKNOWN
    // lane or obstruction ⇒ the sweep never starts ⇒ stays open.
    if (laneFreeTimerActive_ &&
        (nowMs - laneFreeSinceMs_) >= LANE_CLEAR_CONFIRM_MS) {
      if (obstruction == ObstructionState::CLEAR) {
        laneFreeTimerActive_ = false;
        pendingClose_ = false;
        startSweep(BarrierState::CLOSING, nowMs);
      }
      // Not CLEAR (BLOCKED/UNKNOWN): keep the satisfied window armed so a
      // later CLEAR closes immediately, but do not move. An explicit
      // pendingClose with a non-clear guard still surfaces BLOCKED below.
    }
    if (state_ == BarrierState::OPEN && pendingClose_ &&
        (laneEntry != SensorState::VALID_FREE ||
         obstruction != ObstructionState::CLEAR)) {
      state_ = BarrierState::BLOCKED; // wanted to close, guard input says no
    }
    return;

  case BarrierState::BLOCKED:
    if (abortReopen_) {
      // Aborted mid-sweep last tick → reopen now (from current angle).
      abortReopen_ = false;
      laneFreeTimerActive_ = false;
      startSweep(BarrierState::OPENING, nowMs);
      return;
    }
    if (laneEntry == SensorState::VALID_FREE &&
        obstruction == ObstructionState::CLEAR) {
      // Both guards observably clear: return to OPEN and start the confirm
      // window NOW (so a satisfied window closes without a hidden restart).
      state_ = BarrierState::OPEN;
      laneFreeTimerActive_ = true;
      laneFreeSinceMs_ = nowMs;
    }
    return; // otherwise stay BLOCKED (close wanted, guard input not clear)

  case BarrierState::CLOSING: {
    if (laneEntry != SensorState::VALID_FREE ||
        obstruction != ObstructionState::CLEAR) {
      // Abort: occupied OR unknown OR faulty lane, or a non-CLEAR
      // obstruction (object present / faulted / stale) during the sweep.
      if (aborts_ < 0xFFu) ++aborts_; // saturating diagnostics counter
      abortReopen_ = true;
      state_ = BarrierState::BLOCKED;
      return;
    }
    const uint32_t steps = (nowMs - sweepStartMs_) / BARRIER_STEP_MS;
    const int32_t target =
        static_cast<int32_t>(sweepStartAngle_) -
        static_cast<int32_t>(steps * BARRIER_STEP_DEG);
    if (target <= BARRIER_CLOSED_ANGLE) {
      angle_ = BARRIER_CLOSED_ANGLE;
      state_ = BarrierState::CLOSED;
      pendingClose_ = false;
    } else {
      angle_ = static_cast<uint8_t>(target);
    }
    return;
  }
  }
}

} // namespace smartpark
