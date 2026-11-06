#pragma once
/**
 * SmartPark-IoT V2 — application orchestrator (pure logic, native-testable).
 *
 * Composition:
 *
 *   measurement ─► SensorFilter[4] ─┬─► passage lanes (entry/exit, FR-003)
 *   (slot scheduler)                ├─► zone guidance (LCD/LEDs)
 *   IR beams[2] ─debounce─► 2nd     ├─► barrier guard inputs (entry lane +
 *   (GPIO 4/16, M1B)     passage pt │    obstruction input, M1B)
 *                                   └─► EventQueue (serial now, MQTT in M2)
 *   OccupancyManager (boot UNKNOWN or NVS-restored, FR-004/007) ─► admission
 *   BarrierController (FR-005 FSM, guarded close)
 *
 * Policy implemented here (M1A provisional behaviors + M1B extensions):
 *  - Admission allowed only when occupancy is KNOWN with free > 0 (FR-004).
 *    Boot without a valid persisted record ⇒ KNOWN never set ⇒ gate never
 *    opens until an explicit reconcile() — intended safe state (NFR-004).
 *  - An entry APPROACH either opens the gate (admission allowed) or emits one
 *    ADMISSION_DENIED (queue-deduplicated, V1 F-09).
 *  - A PASSAGE_CONFIRMED event is the ONLY automatic occupancy change; it
 *    requires the ordered two-point sequence (secondary transition observed
 *    during the approach) — never approach alone (FR-003). With the IR beams
 *    unwired the controller feeds UNKNOWN and CONFIRMED stays unreachable.
 *  - NVS restore never invents data: an invalid/corrupt/foreign record is
 *    discarded (occupancy stays UNKNOWN); a valid record is restored and
 *    flagged isRestored() + announced with OCCUPANCY_RESTORED, so the count
 *    is known to be recovered, not re-verified (FR-007 / NFR-004).
 *  - The auto-close decision lives inside BarrierController::tick() and needs
 *    a valid FREE entry lane AND a fresh CLEAR obstruction (never a bare
 *    timer; V1 F-04).
 *  - The exit lane never actuates the gate (no exit barrier in this model).
 *
 * Slot scheduling: the HAL measures one sensor per SENSOR_SLOT_MS using
 * slotFor(now); this class only ingests samples via onSample(). Beams and
 * the obstruction input are fed by onBeam()/setObstruction() every loop().
 */
#include <stdint.h>
#include "barrier_controller.hpp"
#include "config.hpp"
#include "event_queue.hpp"
#include "occupancy_manager.hpp"
#include "passage_detector.hpp"
#include "sensor_filter.hpp"

namespace smartpark {

/** Immutable snapshot handed to display/HAL layers. */
struct ControllerView {
  SensorState sensor[SENSOR_COUNT]; // post-staleness effective states
  SensorState zone[2];              // guidance zones (sensors 1 and 2)
  SensorState beam[BEAM_COUNT];     // second passage points (stale-aware)
  OccupancyStatus occStatus;
  OccUnknownReason occReason;
  bool occRestored;    // count came from NVS (recovered, not re-verified)
  uint8_t count;      // valid only when occStatus == KNOWN
  uint8_t freeSpaces; // valid only when occStatus == KNOWN
  BarrierState gate;
  uint8_t gateAngle;
  ObstructionState obstruction; // effective close-guard input (stale-aware)
  bool admissionAllowed;
  bool entryApproach;
  bool exitApproach;
  uint32_t lastEventSeq; // events ever enqueued == next seq to assign (0 if none)
};

class ParkingController {
public:
  ParkingController();

  /** Ingest one completed measurement for a sensor slot. */
  void onSample(uint8_t sensorIdx, long distanceCm, uint32_t nowMs);

  /**
   * Ingest one raw reading of a second passage point (IR beam, M1B).
   * @param beamIdx  BEAM_ENTRY / BEAM_EXIT
   * @param occupied true = beam broken (vehicle present); polarity inversion
   *                 from the physical active-LOW wiring happens in the glue
   * @param nowMs    current millis()
   * Debounced (BEAM_DEBOUNCE_MS) and staleness-aware: a silent reader makes
   * the beam UNKNOWN, never FREE.
   */
  void onBeam(uint8_t beamIdx, bool occupied, uint32_t nowMs);

  /**
   * Ingest the obstruction/close-guard input (M1B, GPIO 14 semantics:
   * true = blocked-or-faulted). Stale samples degrade to UNKNOWN, which can
   * never authorize a closing sweep.
   */
  void setObstruction(bool blocked, uint32_t nowMs);

  /** Periodic policy pass: staleness, barrier tick, state-change events. */
  void tick(uint32_t nowMs);

  /** Effective (stale-aware) view for display/HAL. */
  ControllerView view(uint32_t nowMs) const;

  /** Drain events (serial in M1A; MQTT in M2 consumes the same queue). */
  bool popEvent(ParkEvent& out) { return events_.pop(out); }
  const EventQueue& events() const { return events_; }

  /** Explicit operator/system reconciliation (FR-011 surface, M1A: HAL only). */
  bool reconcile(uint8_t count, uint32_t nowMs);

  /**
   * Restore a count validated from a persisted record (FR-007). Rejects
   * out-of-range values exactly like reconcile() and announces success with
   * OCCUPANCY_RESTORED; the restored flag stays set until the next explicit
   * reconcile() so callers can tell recovered from re-verified counts.
   */
  bool restoreOccupancy(uint8_t count, uint32_t nowMs);

  /** Static helper: which sensor slot is due at time `nowMs`. */
  static uint8_t slotFor(uint32_t nowMs) {
    return static_cast<uint8_t>((nowMs / SENSOR_SLOT_MS) % SENSOR_COUNT);
  }

  /** Direct access for tests and diagnostics. */
  const SensorFilter& filter(uint8_t idx) const { return filters_[idx]; }
  const PassageDetector& entryLane() const { return entry_; }
  const PassageDetector& exitLane() const { return exit_; }
  const OccupancyManager& occupancy() const { return occ_; }
  const BarrierController& barrier() const { return gate_; }
  BarrierController& barrier() { return gate_; }

private:
  void pushEvent(EventType type, uint8_t sensorId, int16_t value,
                 uint32_t nowMs);
  void handleLane(PassageDetector& lane, bool isEntry, SensorState primary,
                  SensorState secondary, uint32_t nowMs);
  void onApproach(bool isEntry, uint32_t nowMs);
  void applyConfirmedPassage(bool isEntry, uint32_t nowMs);
  SensorState beamStateAt(uint8_t beamIdx, uint32_t nowMs) const;
  ObstructionState obstructionAt(uint32_t nowMs) const;

  SensorFilter filters_[SENSOR_COUNT];
  PassageDetector entry_;
  PassageDetector exit_;
  OccupancyManager occ_;
  BarrierController gate_;
  EventQueue events_;

  // Second passage points (beams): debounced committed state + reader stamp.
  SensorState beamState_[BEAM_COUNT];
  SensorState beamPending_[BEAM_COUNT];
  uint32_t beamPendingSince_[BEAM_COUNT];
  uint32_t beamLastMs_[BEAM_COUNT];
  bool beamFed_[BEAM_COUNT];

  // Obstruction/close-guard input (single line, polarity resolved by glue).
  ObstructionState obstructionState_;
  uint32_t obstructionLastMs_;
  bool obstructionFed_;

  BarrierState lastGateState_;
  bool admissionDeniedEmitted_;
};

} // namespace smartpark
