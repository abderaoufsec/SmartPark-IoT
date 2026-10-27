#pragma once
/**
 * SmartPark-IoT V2 — application orchestrator (pure logic, native-testable).
 *
 * Composition:
 *
 *   measurement ─► SensorFilter[4] ─┬─► passage lanes (entry/exit, FR-003)
 *   (slot scheduler)                ├─► zone guidance (LCD/LEDs)
 *                                   ├─► barrier guard input (entry lane)
 *                                   └─► EventQueue (serial now, MQTT in M2)
 *   OccupancyManager (boot UNKNOWN, FR-004/007) ─► admission decision
 *   BarrierController (FR-005 FSM, guarded close)
 *
 * Policy implemented here (all M1A provisional behaviors, see docs):
 *  - Admission allowed only when occupancy is KNOWN with free > 0 (FR-004).
 *    Boot without persistence ⇒ KNOWN never set ⇒ gate never opens until an
 *    explicit reconcile() — intended safe state, not a defect (NFR-004).
 *  - An entry APPROACH either opens the gate (admission allowed) or emits one
 *    ADMISSION_DENIED (queue-deduplicated, V1 F-09).
 *  - A PASSAGE_CONFIRMED event (structurally unreachable with the present
 *    single-beam wiring) is the ONLY thing that changes occupancy besides
 *    explicit reconcile() — never approach (FR-003).
 *  - The auto-close decision lives inside BarrierController::tick() and needs
 *    a valid FREE entry lane (never a bare timer; V1 F-04).
 *  - The exit lane never actuates the gate (no exit barrier in this model).
 *
 * Slot scheduling: the HAL measures one sensor per SENSOR_SLOT_MS using
 * slotFor(now); this class only ingests samples via onSample().
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
  OccupancyStatus occStatus;
  OccUnknownReason occReason;
  uint8_t count;      // valid only when occStatus == KNOWN
  uint8_t freeSpaces; // valid only when occStatus == KNOWN
  BarrierState gate;
  uint8_t gateAngle;
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

  /** Periodic policy pass: staleness, barrier tick, state-change events. */
  void tick(uint32_t nowMs);

  /** Effective (stale-aware) view for display/HAL. */
  ControllerView view(uint32_t nowMs) const;

  /** Drain events (serial in M1A; MQTT in M2 consumes the same queue). */
  bool popEvent(ParkEvent& out) { return events_.pop(out); }
  const EventQueue& events() const { return events_; }

  /** Explicit operator/system reconciliation (FR-011 surface, M1A: HAL only). */
  bool reconcile(uint8_t count, uint32_t nowMs);

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
                  uint32_t nowMs);
  void onApproach(bool isEntry, uint32_t nowMs);
  void applyConfirmedPassage(bool isEntry, uint32_t nowMs);

  SensorFilter filters_[SENSOR_COUNT];
  PassageDetector entry_;
  PassageDetector exit_;
  OccupancyManager occ_;
  BarrierController gate_;
  EventQueue events_;

  BarrierState lastGateState_;
  bool admissionDeniedEmitted_;
};

} // namespace smartpark
