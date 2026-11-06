#include "parking_controller.hpp"

namespace smartpark {

ParkingController::ParkingController()
    : lastGateState_(BarrierState::CLOSED), admissionDeniedEmitted_(false) {
  for (uint8_t i = 0; i < BEAM_COUNT; ++i) {
    beamState_[i] = SensorState::UNKNOWN; // nothing observed yet
    beamPending_[i] = SensorState::INIT;
    beamPendingSince_[i] = 0;
    beamLastMs_[i] = 0;
    beamFed_[i] = false;
  }
  obstructionState_ = ObstructionState::UNKNOWN;
  obstructionLastMs_ = 0;
  obstructionFed_ = false; // a close must never be authorized pre-sample
}

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

  // Lanes are fed by their own primary sensor's samples. The SECOND point is
  // the IR beam (M1B): its committed, stale-aware state decides whether an
  // ordered confirmation is possible — with no beam installed/feeding it
  // reads UNKNOWN and CONFIRMED stays unreachable (FR-003 hardware gate).
  if (sensorIdx == SENSOR_ENTRY) {
    handleLane(entry_, true, filters_[SENSOR_ENTRY].state(),
               beamStateAt(BEAM_ENTRY, nowMs), nowMs);
  } else if (sensorIdx == SENSOR_EXIT) {
    handleLane(exit_, false, filters_[SENSOR_EXIT].state(),
               beamStateAt(BEAM_EXIT, nowMs), nowMs);
  }
}

void ParkingController::onBeam(uint8_t beamIdx, bool occupied,
                               uint32_t nowMs) {
  if (beamIdx >= BEAM_COUNT) return;

  const SensorState raw =
      occupied ? SensorState::VALID_OCCUPIED : SensorState::VALID_FREE;
  beamLastMs_[beamIdx] = nowMs; // the reader is alive (refreshes staleness)
  beamFed_[beamIdx] = true;

  if (raw != beamPending_[beamIdx]) {
    beamPending_[beamIdx] = raw;
    beamPendingSince_[beamIdx] = nowMs;
  }
  if (beamState_[beamIdx] != raw &&
      nowMs - beamPendingSince_[beamIdx] >= BEAM_DEBOUNCE_MS) {
    beamState_[beamIdx] = raw; // stable long enough: commit the transition
    pushEvent(EventType::SENSOR_CHANGED,
              beamIdx == BEAM_ENTRY ? SENSOR_BEAM_ENTRY : SENSOR_BEAM_EXIT,
              static_cast<int16_t>(raw), nowMs);
  }
}

void ParkingController::setObstruction(bool blocked, uint32_t nowMs) {
  const ObstructionState next =
      blocked ? ObstructionState::BLOCKED : ObstructionState::CLEAR;
  obstructionLastMs_ = nowMs;
  obstructionFed_ = true;
  if (next != obstructionState_) {
    obstructionState_ = next;
    // value = ObstructionState for sensorId SENSOR_OBSTRUCTION (documented
    // payload exception in event_types.hpp).
    pushEvent(EventType::SENSOR_CHANGED, SENSOR_OBSTRUCTION,
              static_cast<int16_t>(next), nowMs);
  }
}

SensorState ParkingController::beamStateAt(uint8_t beamIdx,
                                           uint32_t nowMs) const {
  if (beamIdx >= BEAM_COUNT || !beamFed_[beamIdx]) return SensorState::UNKNOWN;
  if (nowMs - beamLastMs_[beamIdx] > SENSOR_STALE_MS)
    return SensorState::UNKNOWN;
  return beamState_[beamIdx];
}

ObstructionState ParkingController::obstructionAt(uint32_t nowMs) const {
  if (!obstructionFed_) return ObstructionState::UNKNOWN;
  if (nowMs - obstructionLastMs_ > OBSTRUCTION_STALE_MS)
    return ObstructionState::UNKNOWN;
  return obstructionState_;
}

void ParkingController::handleLane(PassageDetector& lane, bool isEntry,
                                   SensorState primary, SensorState secondary,
                                   uint32_t nowMs) {
  PassageUpdate flags;
  lane.update(primary, secondary, nowMs, flags);

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
  // appear as UNKNOWN, which blocks/aborts any closing sweep. Same for the
  // obstruction input — stale/never-sampled => UNKNOWN => never a close.
  const SensorState lane = filters_[SENSOR_ENTRY].stateAt(nowMs);
  gate_.tick(nowMs, lane, obstructionAt(nowMs));

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

bool ParkingController::restoreOccupancy(uint8_t count, uint32_t nowMs) {
  // The record was already CRC/version-validated by persistence::decode;
  // this is the last line of defense (range check) plus observability.
  const bool ok = occ_.restore(count);
  pushEvent(ok ? EventType::OCCUPANCY_RESTORED
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
  v.beam[BEAM_ENTRY] = beamStateAt(BEAM_ENTRY, nowMs);
  v.beam[BEAM_EXIT] = beamStateAt(BEAM_EXIT, nowMs);
  v.occStatus = occ_.status();
  v.occReason = occ_.reason();
  v.occRestored = occ_.isRestored();
  v.count = occ_.count();
  v.freeSpaces = occ_.freeSpaces();
  v.gate = gate_.state();
  v.gateAngle = gate_.angle();
  v.obstruction = obstructionAt(nowMs);
  v.admissionAllowed = occ_.isKnown() && occ_.freeSpaces() > 0;
  v.entryApproach = entry_.state() == PassageState::APPROACH;
  v.exitApproach = exit_.state() == PassageState::APPROACH;
  v.lastEventSeq = events_.nextSeq(); // == number of events ever enqueued
  return v;
}

} // namespace smartpark
