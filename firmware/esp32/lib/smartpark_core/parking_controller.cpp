#include "parking_controller.hpp"

namespace smartpark {

ParkingController::ParkingController()
    : lastGateState_(BarrierState::CLOSED), admissionDeniedEmitted_(false) {}

void ParkingController::pushEvent(EventType type, uint8_t sensorId,
                                  int16_t value, uint32_t nowMs) {
  ParkEvent e;
  e.type = type;
  e.sensorId = sensorId;
  e.value = value;
  e.atMs = nowMs;
  e.seq = 0; // assigned by the queue at enqueue
  events_.push(e); // duplicates-of-tail and overflow are handled there
}

void ParkingController::onSample(uint8_t sensorIdx, long distanceCm,
                                 uint32_t nowMs) {
  if (sensorIdx >= SENSOR_COUNT) return;

  const bool changed = filters_[sensorIdx].update(distanceCm, nowMs);
  if (changed) {
    const SensorState after = filters_[sensorIdx].state();
    if (after == SensorState::FAULT) {
      pushEvent(EventType::SENSOR_FAULT, sensorIdx,
                static_cast<int16_t>(after), nowMs);
    } else {
      pushEvent(EventType::SENSOR_CHANGED, sensorIdx,
                static_cast<int16_t>(after), nowMs);
    }
  }

  // Lanes are fed by their own sensor's samples. The SECOND passage point does
  // not exist in the current wiring, so it is always fed UNKNOWN: CONFIRMED
  // is unreachable until a second beam is installed (FR-003 hardware
  // limitation, docs/hardware/pinmap-v2.md §11). Never fabricated here.
  if (sensorIdx == SENSOR_ENTRY) {
    handleLane(entry_, true, filters_[SENSOR_ENTRY].state(), nowMs);
  } else if (sensorIdx == SENSOR_EXIT) {
    handleLane(exit_, false, filters_[SENSOR_EXIT].state(), nowMs);
  }
}

void ParkingController::handleLane(PassageDetector& lane, bool isEntry,
                                   SensorState primary, uint32_t nowMs) {
  PassageUpdate flags;
  lane.update(primary, SensorState::UNKNOWN, nowMs, flags);

  if (flags.approachStarted) {
    const uint8_t id = isEntry ? SENSOR_ENTRY : SENSOR_EXIT;
    pushEvent(isEntry ? EventType::ENTRY_APPROACH : EventType::EXIT_APPROACH,
              id, 0, nowMs);
    onApproach(isEntry, nowMs);
  }
  if (flags.cancelled) {
    const uint8_t id = isEntry ? SENSOR_ENTRY : SENSOR_EXIT;
    pushEvent(isEntry ? EventType::ENTRY_APPROACH_CANCELLED
                      : EventType::EXIT_APPROACH_CANCELLED,
              id, 0, nowMs);
    if (isEntry) admissionDeniedEmitted_ = false; // next approach may log anew
  }
  if (flags.confirmed) {
    const uint8_t id = isEntry ? SENSOR_ENTRY : SENSOR_EXIT;
    pushEvent(isEntry ? EventType::PASSAGE_CONFIRMED_ENTRY
                      : EventType::PASSAGE_CONFIRMED_EXIT,
              id, 0, nowMs);
    applyConfirmedPassage(isEntry, nowMs);
  }
}

void ParkingController::onApproach(bool isEntry, uint32_t nowMs) {
  if (!isEntry) return; // the model has no exit barrier (V1 behavior kept)

  if (occ_.isKnown() && occ_.freeSpaces() > 0) {
    gate_.requestOpen(nowMs);
    admissionDeniedEmitted_ = false;
  } else if (!admissionDeniedEmitted_) {
    // One denial event per approach (queue also dedups; F-09 no more spam).
    // value: -1 = occupancy unknown, otherwise remaining spaces (0 = full).
    const int16_t value =
        occ_.isKnown() ? static_cast<int16_t>(occ_.freeSpaces()) : -1;
    pushEvent(EventType::ADMISSION_DENIED, SENSOR_ENTRY, value, nowMs);
    admissionDeniedEmitted_ = true;
  }
}

void ParkingController::applyConfirmedPassage(bool isEntry, uint32_t nowMs) {
  // The ONLY automatic occupancy change (plus explicit reconcile). Bounds are
  // enforced inside OccupancyManager — rejection is counted and evented,
  // never silently clamped (V1 F-10).
  const int8_t delta = isEntry ? static_cast<int8_t>(1) : static_cast<int8_t>(-1);
  if (!occ_.change(delta)) {
    pushEvent(EventType::OCCUPANCY_CHANGE_REJECTED, EVENT_NO_SENSOR,
              static_cast<int16_t>(delta), nowMs);
  }
}

void ParkingController::tick(uint32_t nowMs) {
  // Barrier sees the *effective* entry-lane state: stale or absent readings
  // appear as UNKNOWN, which blocks/aborts any closing sweep.
  const SensorState lane = filters_[SENSOR_ENTRY].stateAt(nowMs);
  gate_.tick(nowMs, lane);

  const BarrierState gs = gate_.state();
  if (gs != lastGateState_) {
    pushEvent(EventType::BARRIER_STATE_CHANGED, EVENT_NO_SENSOR,
              static_cast<int16_t>(gs), nowMs);
    lastGateState_ = gs;
  }
}

bool ParkingController::reconcile(uint8_t count, uint32_t nowMs) {
  const bool ok = occ_.reconcile(count);
  pushEvent(ok ? EventType::OCCUPANCY_RECONCILED
               : EventType::OCCUPANCY_CHANGE_REJECTED,
            EVENT_NO_SENSOR, static_cast<int16_t>(count), nowMs);
  return ok;
}

ControllerView ParkingController::view(uint32_t nowMs) const {
  ControllerView v;
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i)
    v.sensor[i] = filters_[i].stateAt(nowMs);
  v.zone[0] = v.sensor[SENSOR_ZONE_A];
  v.zone[1] = v.sensor[SENSOR_ZONE_B];
  v.occStatus = occ_.status();
  v.occReason = occ_.reason();
  v.count = occ_.count();
  v.freeSpaces = occ_.freeSpaces();
  v.gate = gate_.state();
  v.gateAngle = gate_.angle();
  v.admissionAllowed = occ_.isKnown() && occ_.freeSpaces() > 0;
  v.entryApproach = entry_.state() == PassageState::APPROACH;
  v.exitApproach = exit_.state() == PassageState::APPROACH;
  v.lastEventSeq = events_.nextSeq(); // == number of events ever enqueued
  return v;
}

} // namespace smartpark
