/**
 * SmartPark-IoT V2 — host-side unit tests (PlatformIO + Unity, `native` env).
 *
 * Covers the M1A + M1B required areas against lib/smartpark_core (pure logic
 * only — no Arduino, no hardware, runs with `pio test -e native`):
 *   1  sensor threshold boundaries and hysteresis
 *   2  timeouts / invalid readings => UNKNOWN (never FREE)
 *   3  stable-sample filtering and noise rejection
 *   4  repeated detection / debounce
 *   5  occupancy count lower and upper bounds
 *   6  duplicate event prevention (queue + controller)
 *   7  UNKNOWN / FAULT handling (sensors, barrier, admission)
 *   8  barrier state transitions incl. guarded close and aborts
 *   9  nonblocking timers incl. millis() rollover
 *  10  entry/exit events and incomplete passage sequences
 *
 * M1B additions (the 14 mandated scenarios):
 *  11  successful entry / successful exit (ordered two-point confirm, count)
 *  12  approach reversal and incomplete passage (grace-window cancellation)
 *  13  repeated sensor triggering (no double count, beam debounce)
 *  14  simultaneous entrance and exit (independent lanes, bounded count)
 *  15  sensor timeout / sensor fault (stale beams, tolerant-then-cancel)
 *  16  obstruction during closing, unknown safety input, emergency stop
 *  17  power interruption & recovery (NVS record encode/decode/validate)
 *  18  occupancy overflow/underflow driven by confirmed passages
 *  19  millis() rollover in the passage timers
 *  20  invalid persisted records (magic/version/capacity/count/CRC)
 *  21  event ordering: unique monotonic uint32 seq (70k events), FIFO
 *
 * These are LOGIC tests only: they do not prove anything about the physical
 * wiring (docs/hardware/pinmap-v2.md §10) nor about Wokwi execution
 * (simulation/wokwi/README.md) — both remain NOT TESTED until run.
 */
#include <stdio.h>
#include <string.h>

#include "../../lib/smartpark_core/barrier_controller.hpp"
#include "../../lib/smartpark_core/display_manager.hpp"
#include "../../lib/smartpark_core/event_queue.hpp"
#include "../../lib/smartpark_core/occupancy_manager.hpp"
#include "../../lib/smartpark_core/passage_detector.hpp"
#include "../../lib/smartpark_core/parking_controller.hpp"
#include "../../lib/smartpark_core/persistence.hpp"
#include "../../lib/smartpark_core/sensor_filter.hpp"
#include <unity.h>

using namespace smartpark;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/** Feed STABLE_READINGS identical samples starting at t0 (50 ms cadence). */
static void settle(SensorFilter& f, long cm, uint32_t t0) {
  for (uint8_t i = 0; i < STABLE_READINGS; ++i) f.update(cm, t0 + i * 50u);
}

static void feedSensor(ParkingController& c, uint8_t idx, long cm, uint32_t t0) {
  for (uint8_t i = 0; i < STABLE_READINGS; ++i)
    c.onSample(idx, cm, t0 + i * 50u);
}

static ParkEvent g_events[64];
static int g_eventCount = 0;

static void drain(ParkingController& c) {
  g_eventCount = 0;
  ParkEvent e;
  while (c.popEvent(e) && g_eventCount < 64) g_events[g_eventCount++] = e;
}

static int countType(EventType t) {
  int n = 0;
  for (int i = 0; i < g_eventCount; ++i)
    if (g_events[i].type == t) ++n;
  return n;
}

/** Index of the nth barrier-state event's value; -1 if not found. */
static int nthBarrierValue(int nth) {
  int seen = 0;
  for (int i = 0; i < g_eventCount; ++i) {
    if (g_events[i].type == EventType::BARRIER_STATE_CHANGED) {
      if (seen == nth) return g_events[i].value;
      ++seen;
    }
  }
  return -1;
}

// ---------------------------------------------------------------------------
// 1. sensor threshold boundaries
// ---------------------------------------------------------------------------

static void test_fresh_classification_boundaries(void) {
  { // 14 cm: below OCCUPIED_ENTER => OCCUPIED
    SensorFilter f; settle(f, 14, 0);
    TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_OCCUPIED, (int)f.state());
  }
  { // exactly 15 cm: not < 15 and not > 30 => ambiguous band => OCCUPIED
    SensorFilter f; settle(f, 15, 0);
    TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_OCCUPIED, (int)f.state());
  }
  { // 30 cm: still in band => OCCUPIED (never FREE on ambiguous data)
    SensorFilter f; settle(f, 30, 0);
    TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_OCCUPIED, (int)f.state());
  }
  { // 31 cm: beyond FREE_ENTER => FREE
    SensorFilter f; settle(f, 31, 0);
    TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());
  }
}

static void test_hysteresis_occupied_to_free(void) {
  SensorFilter f;
  settle(f, 10, 0); // OCCUPIED
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_OCCUPIED, (int)f.state());
  // 20 cm is inside the band: an occupied zone stays occupied
  settle(f, 20, 1000);
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_OCCUPIED, (int)f.state());
  // leaving OCCUPIED needs STABLE_READINGS samples beyond FREE_ENTER
  f.update(31, 2000);
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_OCCUPIED, (int)f.state());
  f.update(31, 2050);
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_OCCUPIED, (int)f.state());
  f.update(31, 2100);
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());
}

static void test_hysteresis_free_to_occupied(void) {
  SensorFilter f;
  settle(f, 40, 0); // FREE
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());
  // 16 cm (>= 15): a free zone stays free — hysteresis band
  settle(f, 16, 1000);
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());
  // 14 cm (< 15) three times: commits to OCCUPIED
  f.update(14, 2000);
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());
  f.update(14, 2050);
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());
  f.update(14, 2100);
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_OCCUPIED, (int)f.state());
}

// ---------------------------------------------------------------------------
// 2. timeouts / invalid readings
// ---------------------------------------------------------------------------

static void test_timeout_is_unknown_never_free(void) {
  { // V1 bug F-03: "999"/timeout was later treated as free. Now: UNKNOWN
    SensorFilter f;
    f.update(-1, 0);
    TEST_ASSERT_EQUAL_INT((int)SensorState::UNKNOWN, (int)f.state());
  }
  { // a trusted FREE sensor that times out flips to UNKNOWN immediately
    SensorFilter f; settle(f, 40, 0);
    TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());
    f.update(-1, 1000);
    TEST_ASSERT_EQUAL_INT((int)SensorState::UNKNOWN, (int)f.state());
  }
}

static void test_fault_escalation_and_recovery(void) {
  SensorFilter f;
  settle(f, 40, 0);
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());

  f.update(-1, 1000); // first timeout: UNKNOWN (not yet FAULT)
  TEST_ASSERT_EQUAL_INT((int)SensorState::UNKNOWN, (int)f.state());
  for (int i = 1; i < FAULT_AFTER_TIMEOUTS; ++i) f.update(-1, 1000 + i * 50u);
  TEST_ASSERT_EQUAL_INT((int)SensorState::FAULT, (int)f.state());

  // recovery needs a trusted run of valid samples: still FAULT after one
  f.update(40, 2000);
  TEST_ASSERT_EQUAL_INT((int)SensorState::FAULT, (int)f.state());
  f.update(40, 2050);
  TEST_ASSERT_EQUAL_INT((int)SensorState::FAULT, (int)f.state());
  f.update(40, 2100);
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());
}

static void test_stale_reading_becomes_unknown_rollover(void) {
  SensorFilter f;
  const uint32_t t0 = 0xFFFFFF00u; // 256 ms before millis() wrap
  settle(f, 40, t0); // last sample lands at t0 + 100
  const uint32_t last = t0 + 100u;
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.stateAt(last));
  // exactly at the limit — arithmetic wraps across 2^32: still valid
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE,
                        (int)f.stateAt(last + SENSOR_STALE_MS));
  // one ms past the limit, still across the wrap => UNKNOWN
  TEST_ASSERT_EQUAL_INT((int)SensorState::UNKNOWN,
                        (int)f.stateAt(last + SENSOR_STALE_MS + 1u));
}

// ---------------------------------------------------------------------------
// 3. filtering / hysteresis under noise
// ---------------------------------------------------------------------------

/** update() must report transitions — the basis for SENSOR_CHANGED events
 *  (this also documents that cppcheck's `knownConditionTrueFalse` on the
 *  return expression is a false positive: INIT->UNKNOWN etc. do occur). */
static void test_filter_reports_state_changes(void) {
  SensorFilter f;
  TEST_ASSERT_TRUE(f.update(-1, 0));    // INIT -> UNKNOWN is a change
  TEST_ASSERT_FALSE(f.update(-1, 50));  // UNKNOWN -> UNKNOWN is not
  TEST_ASSERT_FALSE(f.update(40, 100)); // 1st valid: not yet trusted
  TEST_ASSERT_FALSE(f.update(40, 150)); // 2nd valid: not yet trusted
  TEST_ASSERT_TRUE(f.update(40, 200));  // trusted classification lands (FREE)
  TEST_ASSERT_FALSE(f.update(40, 250)); // stable: no further change
}

static void test_noise_does_not_flip_state(void) {
  SensorFilter f;
  settle(f, 40, 0); // FREE
  // alternating 14/16 cm samples: no commit ever happens
  uint32_t t = 1000;
  for (int i = 0; i < 6; ++i) {
    f.update((i % 2 == 0) ? 14 : 16, t);
    t += 50;
    TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());
  }
  // three consistent samples below the threshold do flip it
  f.update(14, t); t += 50;
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());
  f.update(14, t); t += 50;
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.state());
  f.update(14, t);
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_OCCUPIED, (int)f.state());
}

// ---------------------------------------------------------------------------
// 4. passage detection: debounce and repeated detection
// ---------------------------------------------------------------------------

static void test_approach_then_cancel_never_confirms(void) {
  PassageDetector p;
  PassageUpdate f;

  p.update(SensorState::VALID_OCCUPIED, SensorState::UNKNOWN, 100, f);
  TEST_ASSERT_TRUE(f.approachStarted);
  TEST_ASSERT_EQUAL_INT((int)PassageState::APPROACH, (int)p.state());

  p.update(SensorState::VALID_FREE, SensorState::UNKNOWN, 500, f);
  TEST_ASSERT_TRUE(f.cancelled);
  TEST_ASSERT_EQUAL_INT((int)PassageState::CANCELLED, (int)p.state());

  p.update(SensorState::VALID_FREE, SensorState::UNKNOWN, 600, f);
  TEST_ASSERT_EQUAL_INT((int)PassageState::IDLE, (int)p.state());
}

static void test_approach_debounce_blocks_retrigger(void) {
  PassageDetector p;
  PassageUpdate f;

  p.update(SensorState::VALID_OCCUPIED, SensorState::UNKNOWN, 100, f);
  TEST_ASSERT_TRUE(f.approachStarted);
  p.update(SensorState::VALID_FREE, SensorState::UNKNOWN, 500, f);
  p.update(SensorState::VALID_FREE, SensorState::UNKNOWN, 600, f);

  // re-occupied 900 ms after the first approach: inside the debounce window
  p.update(SensorState::VALID_OCCUPIED, SensorState::UNKNOWN, 1000, f);
  TEST_ASSERT_FALSE(f.approachStarted);
  TEST_ASSERT_EQUAL_INT((int)PassageState::IDLE, (int)p.state());

  // 2600 ms after the first approach: debounce elapsed => new APPROACH
  p.update(SensorState::VALID_OCCUPIED, SensorState::UNKNOWN, 2700, f);
  TEST_ASSERT_TRUE(f.approachStarted);
  TEST_ASSERT_EQUAL_INT((int)PassageState::APPROACH, (int)p.state());
}

static void test_single_beam_never_reaches_confirmed(void) {
  // The current hardware has NO second sensing point (fed UNKNOWN): approach
  // must never become CONFIRMED no matter how long the car sits there.
  PassageDetector p;
  PassageUpdate f;
  p.update(SensorState::VALID_OCCUPIED, SensorState::UNKNOWN, 0, f);
  TEST_ASSERT_TRUE(f.approachStarted);
  for (int i = 1; i <= 20; ++i) {
    p.update(SensorState::VALID_OCCUPIED, SensorState::UNKNOWN, i * 100u, f);
    TEST_ASSERT_FALSE(f.confirmed);
    TEST_ASSERT_EQUAL_INT((int)PassageState::APPROACH, (int)p.state());
  }
}

static void test_confirmed_requires_ordered_second_point(void) {
  PassageDetector p;
  PassageUpdate f;

  p.update(SensorState::VALID_OCCUPIED, SensorState::UNKNOWN, 0, f);
  TEST_ASSERT_EQUAL_INT((int)PassageState::APPROACH, (int)p.state());
  // second point fires while approaching (ordered) => CONFIRMED machinery
  // works for the future two-beam hardware (FR-003)
  p.update(SensorState::VALID_OCCUPIED, SensorState::VALID_OCCUPIED, 100, f);
  TEST_ASSERT_TRUE(f.confirmed);
  TEST_ASSERT_EQUAL_INT((int)PassageState::CONFIRMED, (int)p.state());
  p.update(SensorState::VALID_FREE, SensorState::UNKNOWN, 200, f);
  TEST_ASSERT_EQUAL_INT((int)PassageState::IDLE, (int)p.state());

  // out-of-order: secondary occupied while primary never saw a car => IDLE
  PassageDetector q;
  q.update(SensorState::VALID_FREE, SensorState::VALID_OCCUPIED, 0, f);
  TEST_ASSERT_EQUAL_INT((int)PassageState::IDLE, (int)q.state());
}

// ---------------------------------------------------------------------------
// 5. occupancy bounds
// ---------------------------------------------------------------------------

static void test_occupancy_boots_unknown(void) {
  OccupancyManager o;
  TEST_ASSERT_FALSE(o.isKnown());
  TEST_ASSERT_EQUAL_INT((int)OccUnknownReason::NO_PERSISTED_STATE,
                        (int)o.reason());
  TEST_ASSERT_EQUAL_UINT8(0, o.count());
  TEST_ASSERT_EQUAL_UINT8(0, o.freeSpaces()); // unknown => no claimed space
}

static void test_occupancy_reconcile_bounds(void) {
  OccupancyManager o;
  TEST_ASSERT_FALSE(o.reconcile(7)); // above CAPACITY: rejected, still UNKNOWN
  TEST_ASSERT_EQUAL_INT(1, (int)o.rejectedCount());
  TEST_ASSERT_FALSE(o.isKnown());

  TEST_ASSERT_TRUE(o.reconcile(6)); // upper bound accepted
  TEST_ASSERT_TRUE(o.isKnown());
  TEST_ASSERT_EQUAL_UINT8(6, o.count());
  TEST_ASSERT_EQUAL_UINT8(0, o.freeSpaces());

  TEST_ASSERT_TRUE(o.reconcile(0)); // lower bound accepted
  TEST_ASSERT_EQUAL_UINT8(0, o.count());
  TEST_ASSERT_EQUAL_UINT8(6, o.freeSpaces());
}

static void test_occupancy_change_bounds(void) {
  OccupancyManager o;
  // change() on an unknown count is refused (must reconcile first)
  TEST_ASSERT_FALSE(o.change(1));
  TEST_ASSERT_EQUAL_INT(1, (int)o.rejectedCount());

  o.reconcile(6);
  TEST_ASSERT_FALSE(o.change(1)); // would exceed CAPACITY: no silent clamp
  TEST_ASSERT_EQUAL_UINT8(6, o.count());
  TEST_ASSERT_EQUAL_INT(2, (int)o.rejectedCount());

  o.reconcile(0);
  TEST_ASSERT_FALSE(o.change(-1)); // would go below 0
  TEST_ASSERT_EQUAL_UINT8(0, o.count());

  TEST_ASSERT_TRUE(o.change(1)); // normal path still works
  TEST_ASSERT_EQUAL_UINT8(1, o.count());
  TEST_ASSERT_TRUE(o.change(-1));
  TEST_ASSERT_EQUAL_UINT8(0, o.count());
}

// ---------------------------------------------------------------------------
// 6. duplicate event prevention
// ---------------------------------------------------------------------------

static void test_event_queue_duplicate_rejected(void) {
  EventQueue q;
  ParkEvent e = {EventType::ADMISSION_DENIED, SENSOR_ENTRY, -1, 100, 0};
  TEST_ASSERT_TRUE(q.push(e));
  TEST_ASSERT_FALSE(q.push(e)); // identical tail => rejected
  TEST_ASSERT_EQUAL_UINT32(1, q.size());

  ParkEvent other = {EventType::ADMISSION_DENIED, SENSOR_ENTRY, 0, 100, 0};
  TEST_ASSERT_TRUE(q.push(other)); // different value => accepted
  TEST_ASSERT_EQUAL_UINT32(2, q.size());
}

static void test_event_queue_overflow_counted_and_fifo(void) {
  EventQueue q;
  for (uint32_t i = 0; i < EVENT_QUEUE_CAP; ++i) {
    ParkEvent e = {EventType::SENSOR_CHANGED, static_cast<uint8_t>(i), 1,
                   i * 10u, 0};
    TEST_ASSERT_TRUE(q.push(e));
  }
  ParkEvent extra = {EventType::SENSOR_CHANGED, 99, 1, 999, 0};
  TEST_ASSERT_FALSE(q.push(extra)); // full => drop-newest
  TEST_ASSERT_EQUAL_UINT32(1, q.overflowCount()); // never a silent loss
  TEST_ASSERT_EQUAL_UINT32(EVENT_QUEUE_CAP, q.size());
  TEST_ASSERT_EQUAL_UINT8(0, q.at(0).sensorId); // oldest kept (FIFO)

  ParkEvent out;
  TEST_ASSERT_TRUE(q.pop(out));
  TEST_ASSERT_EQUAL_UINT8(0, out.sensorId);
  TEST_ASSERT_EQUAL_UINT32(0, out.seq); // sequence assigned at enqueue
  TEST_ASSERT_EQUAL_UINT32(EVENT_QUEUE_CAP - 1, q.size());
  TEST_ASSERT_EQUAL_UINT32(EVENT_QUEUE_CAP, q.nextSeq());
}

static void test_controller_denial_emitted_once(void) {
  ParkingController ctrl; // boot: occupancy UNKNOWN (no reconcile)
  feedSensor(ctrl, SENSOR_ENTRY, 10, 100); // approach while UNKNOWN
  for (int i = 0; i < 10; ++i) // keep sampling: still one approach only
    ctrl.onSample(SENSOR_ENTRY, 10, 500 + i * 80u);
  ctrl.tick(1500);
  drain(ctrl);

  TEST_ASSERT_EQUAL_INT(1, countType(EventType::ENTRY_APPROACH));
  TEST_ASSERT_EQUAL_INT(1, countType(EventType::ADMISSION_DENIED));
  int di = -1;
  for (int i = 0; i < g_eventCount; ++i) {
    if (g_events[i].type == EventType::ADMISSION_DENIED) { di = i; break; }
  }
  TEST_ASSERT_TRUE(di >= 0);
  TEST_ASSERT_EQUAL_INT(-1, g_events[di].value); // -1 == occupancy unknown
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSED,
                        (int)ctrl.barrier().state());
}

// ---------------------------------------------------------------------------
// 7. UNKNOWN / FAULT handling: admission and barrier guards
// ---------------------------------------------------------------------------

static void test_admission_gates_on_occupancy(void) {
  ParkingController ctrl;
  ControllerView v = ctrl.view(0);
  TEST_ASSERT_EQUAL_INT((int)OccupancyStatus::UNKNOWN, (int)v.occStatus);
  TEST_ASSERT_FALSE(v.admissionAllowed);

  ctrl.reconcile(6, 10); // full
  v = ctrl.view(20);
  TEST_ASSERT_TRUE(v.occStatus == OccupancyStatus::KNOWN);
  TEST_ASSERT_FALSE(v.admissionAllowed); // full => no admission

  ctrl.reconcile(2, 30);
  v = ctrl.view(40);
  TEST_ASSERT_TRUE(v.admissionAllowed); // spaces free => admission allowed
  TEST_ASSERT_EQUAL_UINT8(4, v.freeSpaces);
}

static void test_unknown_lane_aborts_closing_sweep(void) {
  BarrierController b;
  b.requestOpen(0);
  for (uint32_t t = 15; t <= 675; t += 15) b.tick(t, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());

  b.tick(700, SensorState::VALID_FREE, ObstructionState::CLEAR);
  b.tick(3700, SensorState::VALID_FREE, ObstructionState::CLEAR); // 3000 ms of valid FREE => CLOSING
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSING, (int)b.state());

  // lane turns UNKNOWN mid-sweep => abort + reopen, never finish a blind close
  b.tick(3715, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::BLOCKED, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(1, b.abortCount());
  b.tick(3730, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPENING, (int)b.state());
  for (uint32_t t = 3745; t <= 4405; t += 15)
    b.tick(t, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
}

static void test_occupied_lane_aborts_closing_sweep(void) {
  BarrierController b;
  b.requestOpen(0);
  for (uint32_t t = 15; t <= 675; t += 15) b.tick(t, SensorState::UNKNOWN, ObstructionState::CLEAR);
  b.tick(700, SensorState::VALID_FREE, ObstructionState::CLEAR);
  b.tick(3700, SensorState::VALID_FREE, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSING, (int)b.state());

  b.tick(3715, SensorState::VALID_OCCUPIED, ObstructionState::CLEAR); // car re-entered the lane
  TEST_ASSERT_EQUAL_INT((int)BarrierState::BLOCKED, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(1, b.abortCount());
}

// ---------------------------------------------------------------------------
// 8. barrier state transitions
// ---------------------------------------------------------------------------

static void test_barrier_open_sweep_is_nonblocking(void) {
  BarrierController b;
  b.requestOpen(0);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPENING, (int)b.state());
  b.tick(14, SensorState::UNKNOWN, ObstructionState::CLEAR); // not a full step yet
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPENING, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(0, b.angle());
  b.tick(15, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_UINT8(2, b.angle()); // 2° per 15 ms step
  b.tick(100, SensorState::UNKNOWN, ObstructionState::CLEAR); // 6 steps in
  TEST_ASSERT_EQUAL_UINT8(12, b.angle());
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPENING, (int)b.state());
  // continue the sweep from t=115 (step grid = 115 + 15k): 675 ms is hit at
  // t=685's tick since 115+15*37=670 only reaches 88° — run past 675.
  for (uint32_t t = 115; t <= 690; t += 15) b.tick(t, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(90, b.angle());
}

static void test_barrier_close_needs_sustained_valid_free(void) {
  BarrierController b;
  b.requestOpen(0);
  for (uint32_t t = 15; t <= 675; t += 15) b.tick(t, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());

  b.tick(690, SensorState::VALID_FREE, ObstructionState::CLEAR); // clear-confirm window starts
  b.tick(3689, SensorState::VALID_FREE, ObstructionState::CLEAR); // 2999 ms: not yet
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
  b.tick(3690, SensorState::VALID_FREE, ObstructionState::CLEAR); // 3000 ms observed clear => CLOSING
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSING, (int)b.state());
  for (uint32_t t = 3705; t <= 4365; t += 15)
    b.tick(t, SensorState::VALID_FREE, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSED, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(0, b.angle());
}

static void test_barrier_close_blocked_by_occupied_lane(void) {
  BarrierController b;
  b.requestOpen(0);
  for (uint32_t t = 15; t <= 675; t += 15) b.tick(t, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());

  b.requestClose(700); // someone wants it closed...
  b.tick(710, SensorState::VALID_OCCUPIED, ObstructionState::CLEAR); // ...but a car is in the lane
  TEST_ASSERT_EQUAL_INT((int)BarrierState::BLOCKED, (int)b.state());

  b.tick(720, SensorState::VALID_FREE, ObstructionState::CLEAR); // lane clears => back to OPEN
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
  b.tick(3720, SensorState::VALID_FREE, ObstructionState::CLEAR); // confirm window satisfied
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSING, (int)b.state());
  for (uint32_t t = 3735; t <= 4395; t += 15)
    b.tick(t, SensorState::VALID_FREE, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSED, (int)b.state());
}

static void test_barrier_unknown_lane_never_closes(void) {
  // Fails OPEN: without a valid FREE lane reading the gate must not move.
  BarrierController b;
  b.requestOpen(0);
  for (uint32_t t = 15; t <= 675; t += 15) b.tick(t, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
  for (uint32_t t = 700; t <= 20000; t += 100)
    b.tick(t, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(90, b.angle());
}

static void test_barrier_fault_latches(void) {
  BarrierController b;
  b.setActuatorFault();
  b.requestOpen(100);
  b.tick(1000, SensorState::VALID_FREE, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::FAULT, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(0, b.angle()); // no motion against a fault
}

static void test_barrier_manual_isolation(void) {
  BarrierController b;
  b.setManual();
  b.requestOpen(100);
  b.tick(1000, SensorState::VALID_FREE, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::MANUAL, (int)b.state());
  b.exitManual(0); // angle 0 → settled CLOSED
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSED, (int)b.state());
}

// Regression: releasing MANUAL mid-sweep used to report CLOSED/OPEN while
// `angle()` still held a mid-position (state/angle mismatch). The release
// must resolve deterministically and always respect the lane guard.
static void test_barrier_manual_release_mid_sweep_is_guarded(void) {
  BarrierController b;
  b.requestOpen(0);
  b.tick(15, SensorState::UNKNOWN, ObstructionState::CLEAR); // 2° into the sweep
  TEST_ASSERT_EQUAL_UINT8(2, b.angle());
  b.setManual();
  b.tick(1000, SensorState::VALID_FREE, ObstructionState::CLEAR); // operator hold: nothing moves
  TEST_ASSERT_EQUAL_INT((int)BarrierState::MANUAL, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(2, b.angle());

  // Release into a guarded close; the UNKNOWN lane aborts at once and the
  // gate fails OPEN — never a blind drop to CLOSED.
  b.exitManual(2000);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSING, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(2, b.angle());
  b.tick(2010, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::BLOCKED, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(2, b.angle());
  b.tick(2025, SensorState::UNKNOWN, ObstructionState::CLEAR); // abort flag consumed → reopen
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPENING, (int)b.state());
  b.tick(2700, SensorState::UNKNOWN, ObstructionState::CLEAR); // full sweep from 2°
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(BARRIER_OPEN_ANGLE, b.angle());
}

// Same release with an observably clear lane: the guarded close completes
// and state/angle agree at CLOSED.
static void test_barrier_manual_release_mid_sweep_closes_when_clear(void) {
  BarrierController b;
  b.requestOpen(0);
  b.tick(15, SensorState::UNKNOWN, ObstructionState::CLEAR); // 2° into the sweep
  b.setManual();
  b.exitManual(2000);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSING, (int)b.state());
  b.tick(2015, SensorState::VALID_FREE, ObstructionState::CLEAR); // one 2° step → 0°
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSED, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(0, b.angle());
  // Settled-position releases resolve directly, with no residual intent:
  b.setManual();
  b.exitManual(3000); // angle 0 → CLOSED
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSED, (int)b.state());
}

// Regression: setManual()/exitManual() used to keep a stale abortReopen_
// flag; a later, legitimate BLOCKED (pending-close path) then self-reopened
// on the very next tick without an abort having occurred.
static void test_barrier_takeover_clears_stale_abort_reopen(void) {
  BarrierController b;
  b.requestOpen(0);
  for (uint32_t t = 15; t <= 690; t += 15) b.tick(t, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
  // lane clear for the full 3 s window → close starts at t=3705
  for (uint32_t t = 705; t <= 3705; t += 100)
    b.tick(t, SensorState::VALID_FREE, ObstructionState::CLEAR);
  b.tick(3720, SensorState::VALID_OCCUPIED, ObstructionState::CLEAR); // abort → BLOCKED (stale flag set)
  TEST_ASSERT_EQUAL_INT((int)BarrierState::BLOCKED, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(1, b.abortCount());

  b.setManual();   // takeover must drop the stale reopen intent
  b.exitManual(4000); // angle 90 → settled OPEN
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());

  // A genuine pending-close BLOCKED with the lane still occupied must STAY
  // BLOCKED (no abort happened on these ticks — no self-reopen, no new abort).
  b.requestClose(4100);
  b.tick(4110, SensorState::VALID_OCCUPIED, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::BLOCKED, (int)b.state());
  b.tick(4125, SensorState::VALID_OCCUPIED, ObstructionState::CLEAR);
  b.tick(4140, SensorState::VALID_OCCUPIED, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::BLOCKED, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(1, b.abortCount()); // unchanged: these were not aborts
}

// ---------------------------------------------------------------------------
// 9. nonblocking timers + millis() rollover
// ---------------------------------------------------------------------------

static void test_barrier_sweep_survives_millis_rollover(void) {
  BarrierController b;
  const uint32_t t0 = 0xFFFFFF00u; // 256 ms before wrap
  b.requestOpen(t0);
  b.tick(t0 + 100u, SensorState::UNKNOWN, ObstructionState::CLEAR); // wraps? no: still pre-wrap
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPENING, (int)b.state());
  // 0xFFFFFF00 + 512 crosses the wrap => unsigned math still sees 512 ms
  b.tick(0x00000100u, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_UINT8(68, b.angle()); // (512/15)=34 steps * 2°
  // full sweep completes at t0+675 (wrapped address)
  b.tick(t0 + 675u, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(90, b.angle());
}

static void test_lane_clear_timer_survives_millis_rollover(void) {
  BarrierController b;
  const uint32_t t0 = 0xFFFFF500u; // 2816 ms before wrap
  b.requestOpen(t0 - 700u);        // sweep starts pre-wrap
  // Sweep needs 675 ms; the 15 ms grid (t0-685, +15) lands on t0-10 at
  // latest and never exactly on t0 — so the loop must be bounded by VALUE.
  // (An equality bound here would orbit the whole uint32 space first:
  // 1,431,655,811 ticks — it would "pass" only after seconds of CPU.)
  for (uint32_t t = t0 - 685u; t <= t0; t += 15u) {
    b.tick(t, SensorState::UNKNOWN, ObstructionState::CLEAR);
  }
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());

  b.tick(t0, SensorState::VALID_FREE, ObstructionState::CLEAR); // clear-confirm window opens pre-wrap
  b.tick(t0 + 2999u, SensorState::VALID_FREE, ObstructionState::CLEAR); // crosses wrap; 2999 ms
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
  b.tick(t0 + 3000u, SensorState::VALID_FREE, ObstructionState::CLEAR); // wrapped time: 3000 ms exact
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSING, (int)b.state());
}

static void test_sensor_stale_timer_survives_rollover(void) {
  SensorFilter f;
  const uint32_t t0 = 0xFFFFFC00u; // 1024 ms before millis() wrap
  settle(f, 40, t0); // last sample at t0 + 100
  const uint32_t last = t0 + 100u;
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE, (int)f.stateAt(last));
  // the stale boundary itself lands across the wrap (last + 1000 > 2^32)
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE,
                        (int)f.stateAt(last + SENSOR_STALE_MS));
  TEST_ASSERT_EQUAL_INT((int)SensorState::UNKNOWN,
                        (int)f.stateAt(last + SENSOR_STALE_MS + 1u));
}

// ---------------------------------------------------------------------------
// 10. entry/exit events and incomplete passages (controller level)
// ---------------------------------------------------------------------------

static void test_entry_approach_opens_gate_but_never_counts(void) {
  ParkingController ctrl;
  ctrl.reconcile(2, 10); // KNOWN: 2/6 used, admission allowed

  feedSensor(ctrl, SENSOR_ENTRY, 10, 100); // car arrives at entry beam
  drain(ctrl);
  TEST_ASSERT_EQUAL_INT(1, countType(EventType::ENTRY_APPROACH));
  TEST_ASSERT_EQUAL_INT(0, countType(EventType::ADMISSION_DENIED));
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPENING,
                        (int)ctrl.barrier().state()); // gate opening

  ctrl.tick(875); // sweep completes (675 ms)
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)ctrl.barrier().state());
  TEST_ASSERT_EQUAL_UINT8(2, ctrl.occupancy().count()); // approach: no count

  // Incomplete passage: beam clears with NO second point => CANCELLED.
  feedSensor(ctrl, SENSOR_ENTRY, 50, 900);
  drain(ctrl);
  TEST_ASSERT_EQUAL_INT(1, countType(EventType::ENTRY_APPROACH_CANCELLED));
  TEST_ASSERT_EQUAL_UINT8(2, ctrl.occupancy().count()); // still no count!
}

static void test_full_cycle_closes_only_when_lane_observed_clear(void) {
  ParkingController ctrl;
  ctrl.reconcile(2, 10);
  feedSensor(ctrl, SENSOR_ENTRY, 10, 100); // approach => gate opens
  ctrl.tick(200); // emit OPENING promptly, exactly like loop() does
  ctrl.tick(875);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)ctrl.barrier().state());

  // Car parks (beam clear) — keep sampling so the lane stays valid-FREE.
  // The obstruction input is fed CLEAR each pass (as loop() does): a close
  // additionally requires a fresh, non-stale obstruction sample (M1B).
  bool closed = false;
  uint32_t t = 1200;
  while (t < 1200u + 20000u) {
    feedSensor(ctrl, SENSOR_ENTRY, 50, t);
    ctrl.setObstruction(false, t + 100u);
    ctrl.tick(t + 100u);
    if (ctrl.barrier().state() == BarrierState::CLOSED) {
      closed = true;
      break;
    }
    t += 200;
  }
  TEST_ASSERT_TRUE(closed);

  drain(ctrl);
  TEST_ASSERT_EQUAL_UINT8(2, ctrl.occupancy().count());
  // observed barrier sequence: OPENING, OPEN, CLOSING, CLOSED (in order)
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPENING, nthBarrierValue(0));
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, nthBarrierValue(1));
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSING, nthBarrierValue(2));
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSED, nthBarrierValue(3));
}

static void test_exit_approach_never_touches_gate(void) {
  ParkingController ctrl;
  ctrl.reconcile(2, 10);

  feedSensor(ctrl, SENSOR_EXIT, 10, 100); // car at the exit beam
  ctrl.tick(500);
  drain(ctrl);
  TEST_ASSERT_EQUAL_INT(1, countType(EventType::EXIT_APPROACH));
  TEST_ASSERT_EQUAL_INT(0, countType(EventType::ENTRY_APPROACH));
  TEST_ASSERT_EQUAL_INT(0, countType(EventType::ADMISSION_DENIED));
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSED,
                        (int)ctrl.barrier().state());
  TEST_ASSERT_EQUAL_UINT8(2, ctrl.occupancy().count());
}

static void test_reconcile_drives_events_and_view(void) {
  ParkingController ctrl;
  ctrl.reconcile(99, 10); // above CAPACITY => rejected + event, stays UNKNOWN
  drain(ctrl);
  TEST_ASSERT_EQUAL_INT(1, countType(EventType::OCCUPANCY_CHANGE_REJECTED));
  TEST_ASSERT_EQUAL_INT((int)OccupancyStatus::UNKNOWN,
                        (int)ctrl.view(20).occStatus);

  ctrl.reconcile(3, 30);
  drain(ctrl);
  TEST_ASSERT_EQUAL_INT(1, countType(EventType::OCCUPANCY_RECONCILED));
  ControllerView v = ctrl.view(40);
  TEST_ASSERT_EQUAL_INT((int)OccupancyStatus::KNOWN, (int)v.occStatus);
  TEST_ASSERT_EQUAL_UINT8(3, v.count);
}

// ---------------------------------------------------------------------------
// display formatting (V1 F-07 overflow fix)
// ---------------------------------------------------------------------------

static void test_display_lines_are_exactly_16_chars(void) {
  ControllerView v;
  memset(&v, 0, sizeof(v));
  v.occStatus = OccupancyStatus::UNKNOWN;
  v.gate = BarrierState::CLOSED;
  v.zone[0] = SensorState::UNKNOWN;
  v.zone[1] = SensorState::FAULT;

  DisplayLines l = formatDisplay(v);
  TEST_ASSERT_EQUAL_UINT32(16, strlen(l.line1));
  TEST_ASSERT_EQUAL_UINT32(16, strlen(l.line2));
  TEST_ASSERT_EQUAL_MEMORY("OCC:??? NO ENTRY", l.line2, 16);

  v.occStatus = OccupancyStatus::KNOWN;
  v.freeSpaces = 4;
  v.zone[0] = SensorState::VALID_FREE;
  v.zone[1] = SensorState::VALID_OCCUPIED;
  l = formatDisplay(v);
  TEST_ASSERT_EQUAL_UINT32(16, strlen(l.line2));
  TEST_ASSERT_EQUAL_MEMORY("F:4 A:F B:O G:CL", l.line2, 16);
}

static void test_display_priority_messages(void) {
  ControllerView v;
  memset(&v, 0, sizeof(v));
  v.occStatus = OccupancyStatus::KNOWN;
  v.freeSpaces = 0;
  v.gate = BarrierState::CLOSED;

  DisplayLines l = formatDisplay(v);
  TEST_ASSERT_EQUAL_MEMORY("F:0 PARK FULL", l.line2, 13);
  TEST_ASSERT_EQUAL_UINT32(16, strlen(l.line2));

  v.freeSpaces = 2;
  v.entryApproach = true;
  v.admissionAllowed = false; // e.g. unknown/full at the moment of approach
  l = formatDisplay(v);
  TEST_ASSERT_EQUAL_MEMORY("ENTRY: FULL STOP", l.line1, 16); // V1 F-09 fix

  v.gate = BarrierState::FAULT;
  l = formatDisplay(v);
  TEST_ASSERT_EQUAL_MEMORY("GATE FAULT", l.line1, 10);
  TEST_ASSERT_EQUAL_UINT32(16, strlen(l.line1));
}

static void test_pad_line_truncates_and_pads(void) {
  char buf[17];
  snprintf(buf, sizeof(buf), "SHORT");
  padLine16(buf);
  TEST_ASSERT_EQUAL_UINT32(16, strlen(buf));
  TEST_ASSERT_EQUAL_UINT8(' ', buf[15]);

  // snprintf-limited input is never longer than 16; padding is idempotent
  snprintf(buf, sizeof(buf), "ABCDEFGHIJKLMNOP"); // 16 chars exactly
  padLine16(buf);
  TEST_ASSERT_EQUAL_UINT32(16, strlen(buf));
  TEST_ASSERT_EQUAL_MEMORY("ABCDEFGHIJKLMNOP", buf, 16);
}

// ---------------------------------------------------------------------------
// 11. M1B — successful entry / exit (ordered two-point confirmation)
// ---------------------------------------------------------------------------

/** Drive a full ordered crossing of one lane at controller level. */
static void crossLane(ParkingController& ctrl, uint8_t sensorIdx,
                      uint8_t beamIdx, uint32_t t0) {
  ctrl.onBeam(beamIdx, false, t0 + 50u);  // point 2 clear at rest
  ctrl.onBeam(beamIdx, false, t0 + 120u); // ...held past BEAM_DEBOUNCE_MS
  feedSensor(ctrl, sensorIdx, 10, t0);    // point 1 sees the vehicle
  ctrl.onBeam(beamIdx, true, t0 + 200u);  // vehicle reaches point 2
  ctrl.onBeam(beamIdx, true, t0 + 300u);  // committed (>= 50 ms)
  feedSensor(ctrl, sensorIdx, 10, t0 + 350u); // point-1 sample drives the FSM
}

static void test_successful_entry_confirms_and_counts(void) {
  ParkingController ctrl;
  ctrl.reconcile(2, 10); // KNOWN 2/6: admission allowed

  crossLane(ctrl, SENSOR_ENTRY, BEAM_ENTRY, 100);
  drain(ctrl);

  TEST_ASSERT_EQUAL_INT(1, countType(EventType::ENTRY_APPROACH));
  TEST_ASSERT_EQUAL_INT(1, countType(EventType::PASSAGE_CONFIRMED_ENTRY));
  TEST_ASSERT_EQUAL_INT(0, countType(EventType::OCCUPANCY_CHANGE_REJECTED));
  TEST_ASSERT_EQUAL_UINT8(3, ctrl.occupancy().count());
  TEST_ASSERT_FALSE(ctrl.occupancy().isRestored()); // counted, not recovered
  // re-arm: the primary must release before another approach is possible
  TEST_ASSERT_EQUAL_INT((int)PassageState::CONFIRMED,
                        (int)ctrl.entryLane().state());
}

static void test_successful_exit_confirms_and_counts(void) {
  ParkingController ctrl;
  ctrl.reconcile(2, 10);

  crossLane(ctrl, SENSOR_EXIT, BEAM_EXIT, 100);
  drain(ctrl);

  TEST_ASSERT_EQUAL_INT(1, countType(EventType::EXIT_APPROACH));
  TEST_ASSERT_EQUAL_INT(1, countType(EventType::PASSAGE_CONFIRMED_EXIT));
  TEST_ASSERT_EQUAL_INT(0, countType(EventType::PASSAGE_CONFIRMED_ENTRY));
  TEST_ASSERT_EQUAL_UINT8(1, ctrl.occupancy().count()); // 2 - 1
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSED,
                        (int)ctrl.barrier().state()); // exit never opens it
}

// ---------------------------------------------------------------------------
// 12. M1B — reversal and incomplete passage (grace-window cancellation)
// ---------------------------------------------------------------------------

static void test_reversal_cancels_after_grace_without_confirm(void) {
  PassageDetector p;
  PassageUpdate f;

  p.update(SensorState::VALID_OCCUPIED, SensorState::VALID_FREE, 100, f);
  TEST_ASSERT_TRUE(f.approachStarted);
  TEST_ASSERT_EQUAL_INT((int)PassageState::APPROACH, (int)p.state());

  p.update(SensorState::VALID_FREE, SensorState::VALID_FREE, 500, f);
  TEST_ASSERT_FALSE(f.cancelled); // gap-crossing grace is running
  p.update(SensorState::VALID_FREE, SensorState::VALID_FREE,
           500u + PASSAGE_GRACE_MS - 1u, f);
  TEST_ASSERT_FALSE(f.cancelled); // one ms before expiry: still waiting
  p.update(SensorState::VALID_FREE, SensorState::VALID_FREE,
           500u + PASSAGE_GRACE_MS, f);
  TEST_ASSERT_TRUE(f.cancelled); // reversal/incomplete => cancel, no count
  TEST_ASSERT_EQUAL_INT((int)PassageState::CANCELLED, (int)p.state());
}

static void test_gap_crossing_confirms_within_grace(void) {
  // Vehicle shorter than the point spacing: E1 clears BEFORE E2 trips.
  PassageDetector p;
  PassageUpdate f;

  p.update(SensorState::VALID_OCCUPIED, SensorState::VALID_FREE, 0, f);
  TEST_ASSERT_TRUE(f.approachStarted);
  p.update(SensorState::VALID_FREE, SensorState::VALID_FREE, 200, f);
  TEST_ASSERT_FALSE(f.cancelled); // grace running, nothing confirmed yet
  p.update(SensorState::VALID_FREE, SensorState::VALID_OCCUPIED, 800, f);
  TEST_ASSERT_TRUE(f.confirmed); // ordered second point within the grace
  TEST_ASSERT_EQUAL_INT((int)PassageState::CONFIRMED, (int)p.state());
}

// ---------------------------------------------------------------------------
// 13. M1B — repeated triggering, beam debounce, sensor timeout/fault
// ---------------------------------------------------------------------------

static void test_beam_chatter_never_double_confirms(void) {
  PassageDetector p;
  PassageUpdate f;

  p.update(SensorState::VALID_OCCUPIED, SensorState::VALID_FREE, 0, f);
  p.update(SensorState::VALID_OCCUPIED, SensorState::VALID_OCCUPIED, 100, f);
  TEST_ASSERT_TRUE(f.confirmed); // one crossing => one confirmation
  // vehicle still on the primary: secondary flapping again cannot re-confirm
  p.update(SensorState::VALID_OCCUPIED, SensorState::VALID_FREE, 200, f);
  p.update(SensorState::VALID_OCCUPIED, SensorState::VALID_OCCUPIED, 300, f);
  TEST_ASSERT_FALSE(f.confirmed);
  TEST_ASSERT_EQUAL_INT((int)PassageState::CONFIRMED, (int)p.state());
  // primary releases => re-armed; instant re-approach is still debounced
  p.update(SensorState::VALID_FREE, SensorState::VALID_FREE, 400, f);
  TEST_ASSERT_EQUAL_INT((int)PassageState::IDLE, (int)p.state());
  p.update(SensorState::VALID_OCCUPIED, SensorState::VALID_FREE, 450, f);
  TEST_ASSERT_FALSE(f.approachStarted); // 450 ms < APPROACH_DEBOUNCE_MS
}

static void test_beam_debounce_rejects_short_glitches(void) {
  ParkingController ctrl;
  ctrl.onBeam(BEAM_ENTRY, true, 0);  // 20 ms glitch: too short to trust
  ctrl.onBeam(BEAM_ENTRY, false, 20);
  ctrl.onBeam(BEAM_ENTRY, false, 30);
  TEST_ASSERT_EQUAL_INT((int)SensorState::UNKNOWN,
                        (int)ctrl.view(30).beam[BEAM_ENTRY]); // never commit
  ctrl.onBeam(BEAM_ENTRY, false, 80); // FREE held >= BEAM_DEBOUNCE_MS
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE,
                        (int)ctrl.view(80).beam[BEAM_ENTRY]);
}

static void test_beam_stale_becomes_unknown_never_free(void) {
  ParkingController ctrl;
  // never fed => UNKNOWN (a missing reader can never look like "free")
  TEST_ASSERT_EQUAL_INT((int)SensorState::UNKNOWN,
                        (int)ctrl.view(0).beam[BEAM_EXIT]);

  ctrl.onBeam(BEAM_ENTRY, false, 0);
  ctrl.onBeam(BEAM_ENTRY, false, 60); // committed FREE
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE,
                        (int)ctrl.view(60).beam[BEAM_ENTRY]);
  // stale boundary is inclusive, same rule as the ultrasonic filters
  TEST_ASSERT_EQUAL_INT((int)SensorState::VALID_FREE,
                        (int)ctrl.view(60 + SENSOR_STALE_MS).beam[BEAM_ENTRY]);
  TEST_ASSERT_EQUAL_INT((int)SensorState::UNKNOWN,
                        (int)ctrl.view(61 + SENSOR_STALE_MS).beam[BEAM_ENTRY]);
}

static void test_primary_sensor_loss_cancels_after_tolerance(void) {
  { // timeout/stale: bounded hold, then cancel — a dead sensor cannot pin
    // a lane, and the approach is never confirmed while unobservable.
    PassageDetector p;
    PassageUpdate f;
    p.update(SensorState::VALID_OCCUPIED, SensorState::VALID_FREE, 0, f);
    TEST_ASSERT_TRUE(f.approachStarted);
    p.update(SensorState::UNKNOWN, SensorState::VALID_FREE, 100, f);
    TEST_ASSERT_FALSE(f.cancelled);
    TEST_ASSERT_EQUAL_INT((int)PassageState::APPROACH, (int)p.state());
    p.update(SensorState::UNKNOWN, SensorState::VALID_FREE,
             100u + PASSAGE_UNKNOWN_TOLERANCE_MS - 1u, f);
    TEST_ASSERT_FALSE(f.cancelled);
    p.update(SensorState::UNKNOWN, SensorState::VALID_FREE,
             100u + PASSAGE_UNKNOWN_TOLERANCE_MS, f);
    TEST_ASSERT_TRUE(f.cancelled);
    TEST_ASSERT_EQUAL_INT((int)PassageState::CANCELLED, (int)p.state());
  }
  { // FAULT behaves exactly like UNKNOWN for the hold/cancel decision
    PassageDetector q;
    PassageUpdate g;
    q.update(SensorState::VALID_OCCUPIED, SensorState::VALID_FREE, 0, g);
    q.update(SensorState::FAULT, SensorState::VALID_FREE, 100, g);
    TEST_ASSERT_FALSE(g.cancelled);
    q.update(SensorState::FAULT, SensorState::VALID_FREE,
             100u + PASSAGE_UNKNOWN_TOLERANCE_MS, g);
    TEST_ASSERT_TRUE(g.cancelled);
  }
}

static void test_faulted_secondary_never_confirms(void) {
  // A faulted second point can never satisfy the ordered sequence — the
  // lane holds its approach until the tolerance cancels it (fail toward
  // "no count", never a fabricated one).
  PassageDetector p;
  PassageUpdate f;
  p.update(SensorState::VALID_OCCUPIED, SensorState::UNKNOWN, 0, f);
  TEST_ASSERT_TRUE(f.approachStarted);
  for (int i = 1; i <= 10; ++i) {
    p.update(SensorState::VALID_OCCUPIED, SensorState::FAULT,
             static_cast<uint32_t>(i) * 200u, f);
    TEST_ASSERT_FALSE(f.confirmed);
    TEST_ASSERT_EQUAL_INT((int)PassageState::APPROACH, (int)p.state());
  }
}

// ---------------------------------------------------------------------------
// 14. M1B — simultaneous entrance and exit + bounded counts
// ---------------------------------------------------------------------------

static void test_simultaneous_entry_and_exit(void) {
  ParkingController ctrl;
  ctrl.reconcile(3, 10); // KNOWN 3/6

  // Both lanes see a vehicle at the same time; both second points trip.
  crossLane(ctrl, SENSOR_ENTRY, BEAM_ENTRY, 100);
  crossLane(ctrl, SENSOR_EXIT, BEAM_EXIT, 100);
  drain(ctrl);

  TEST_ASSERT_EQUAL_INT(1, countType(EventType::PASSAGE_CONFIRMED_ENTRY));
  TEST_ASSERT_EQUAL_INT(1, countType(EventType::PASSAGE_CONFIRMED_EXIT));
  TEST_ASSERT_EQUAL_INT(0, countType(EventType::OCCUPANCY_CHANGE_REJECTED));
  TEST_ASSERT_EQUAL_UINT8(3, ctrl.occupancy().count()); // +1 and -1 net 0
  TEST_ASSERT_TRUE(ctrl.occupancy().isKnown());
}

static void test_confirmed_passage_respects_occupancy_bounds(void) {
  { // overflow: a confirmed entry at CAPACITY is rejected, never clamped
    ParkingController ctrl;
    ctrl.reconcile(static_cast<uint8_t>(CAPACITY), 10);
    crossLane(ctrl, SENSOR_ENTRY, BEAM_ENTRY, 100);
    drain(ctrl);
    TEST_ASSERT_EQUAL_INT(1, countType(EventType::PASSAGE_CONFIRMED_ENTRY));
    TEST_ASSERT_EQUAL_INT(1, countType(EventType::OCCUPANCY_CHANGE_REJECTED));
    TEST_ASSERT_EQUAL_UINT8(CAPACITY, ctrl.occupancy().count());
    TEST_ASSERT_EQUAL_UINT32(1, ctrl.occupancy().rejectedCount());
  }
  { // underflow: a confirmed exit at 0 is rejected the same way
    ParkingController ctrl;
    ctrl.reconcile(0, 10);
    crossLane(ctrl, SENSOR_EXIT, BEAM_EXIT, 100);
    drain(ctrl);
    TEST_ASSERT_EQUAL_INT(1, countType(EventType::PASSAGE_CONFIRMED_EXIT));
    TEST_ASSERT_EQUAL_INT(1, countType(EventType::OCCUPANCY_CHANGE_REJECTED));
    TEST_ASSERT_EQUAL_UINT8(0, ctrl.occupancy().count());
  }
}

// ---------------------------------------------------------------------------
// 15/16. M1B — obstruction guard (Phase 5 barrier safety)
// ---------------------------------------------------------------------------

static void test_obstruction_unknown_never_authorizes_close(void) {
  BarrierController b;
  b.requestOpen(0);
  for (uint32_t t = 15; t <= 675; t += 15)
    b.tick(t, SensorState::UNKNOWN, ObstructionState::UNKNOWN);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());

  // Lane reads valid FREE for the full window, but the safety input was
  // never observed: the auto-close must not start (unknown != clear).
  b.tick(700, SensorState::VALID_FREE, ObstructionState::UNKNOWN);
  b.tick(3700, SensorState::VALID_FREE, ObstructionState::UNKNOWN);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(BARRIER_OPEN_ANGLE, b.angle());

  // A fresh CLEAR then closes immediately (the satisfied window stayed
  // armed) — and an obstruction appearing MID-SWEEP aborts to BLOCKED.
  b.tick(3710, SensorState::VALID_FREE, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSING, (int)b.state());
  b.tick(3725, SensorState::VALID_FREE, ObstructionState::BLOCKED);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::BLOCKED, (int)b.state());
  TEST_ASSERT_EQUAL_UINT8(1, b.abortCount());
  b.tick(3740, SensorState::VALID_FREE, ObstructionState::BLOCKED);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPENING, (int)b.state()); // reopen
}

static void test_obstruction_blocks_explicit_close_intent(void) {
  BarrierController b;
  b.requestOpen(0);
  for (uint32_t t = 15; t <= 675; t += 15)
    b.tick(t, SensorState::UNKNOWN, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());

  b.requestClose(700); // a close is wanted...
  b.tick(710, SensorState::VALID_FREE, ObstructionState::BLOCKED);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::BLOCKED, (int)b.state()); // stop

  b.tick(720, SensorState::VALID_FREE, ObstructionState::CLEAR); // released
  TEST_ASSERT_EQUAL_INT((int)BarrierState::OPEN, (int)b.state());
  // ...and with both guards clear the normal window logic still works:
  b.tick(730, SensorState::VALID_FREE, ObstructionState::CLEAR);
  b.tick(3730, SensorState::VALID_FREE, ObstructionState::CLEAR);
  TEST_ASSERT_EQUAL_INT((int)BarrierState::CLOSING, (int)b.state());
}

// ---------------------------------------------------------------------------
// 17/20. M1B — persistence: recovery, corruption, invalid records
// ---------------------------------------------------------------------------

static void test_restore_from_valid_record(void) {
  ParkingController ctrl;
  TEST_ASSERT_FALSE(ctrl.occupancy().isKnown()); // boot without a record

  uint8_t buf[persistence::OCC_RECORD_SIZE];
  TEST_ASSERT_EQUAL_UINT32(
      persistence::OCC_RECORD_SIZE,
      persistence::encodeOccupancy(4, buf, sizeof(buf)));
  uint8_t count = 0;
  TEST_ASSERT_EQUAL_INT(
      (int)persistence::RecordStatus::OK,
      (int)persistence::decodeOccupancy(buf, sizeof(buf), count));
  TEST_ASSERT_EQUAL_UINT8(4, count);

  TEST_ASSERT_TRUE(ctrl.restoreOccupancy(count, 50));
  drain(ctrl);
  TEST_ASSERT_EQUAL_INT(1, countType(EventType::OCCUPANCY_RESTORED));
  ControllerView v = ctrl.view(60);
  TEST_ASSERT_EQUAL_INT((int)OccupancyStatus::KNOWN, (int)v.occStatus);
  TEST_ASSERT_EQUAL_UINT8(4, v.count);
  TEST_ASSERT_TRUE(v.occRestored); // recovered, not re-verified
  TEST_ASSERT_TRUE(v.admissionAllowed);

  // An explicit operator reconcile supersedes the recovered value/flag.
  ctrl.reconcile(2, 70);
  TEST_ASSERT_FALSE(ctrl.view(80).occRestored);
}

static void test_corrupt_record_rejected_stays_unknown(void) {
  uint8_t buf[persistence::OCC_RECORD_SIZE];
  TEST_ASSERT_EQUAL_UINT32(
      persistence::OCC_RECORD_SIZE,
      persistence::encodeOccupancy(4, buf, sizeof(buf)));
  buf[8] = 5; // count byte flipped without repairing the CRC (torn write)

  uint8_t count = 42; // sentinel: decode must NOT write on failure
  TEST_ASSERT_EQUAL_INT((int)persistence::RecordStatus::BAD_CRC,
                        (int)persistence::decodeOccupancy(buf, sizeof(buf),
                                                           count));
  TEST_ASSERT_EQUAL_UINT8(42, count); // never a half-trusted count

  // A decoded-but-out-of-range count is rejected again by the controller.
  ParkingController ctrl;
  TEST_ASSERT_FALSE(
      ctrl.restoreOccupancy(static_cast<uint8_t>(CAPACITY) + 1, 10));
  drain(ctrl);
  TEST_ASSERT_EQUAL_INT(1, countType(EventType::OCCUPANCY_CHANGE_REJECTED));
  TEST_ASSERT_EQUAL_INT(0, countType(EventType::OCCUPANCY_RESTORED));
  TEST_ASSERT_EQUAL_INT((int)OccupancyStatus::UNKNOWN,
                        (int)ctrl.view(20).occStatus);
}

static void test_persistence_record_validation(void) {
  namespace p = persistence;
  uint8_t buf[p::OCC_RECORD_SIZE];
  uint8_t count = 77; // sentinel

  // NULL / length
  TEST_ASSERT_EQUAL_INT((int)p::RecordStatus::NULLPTR,
                        (int)p::decodeOccupancy(nullptr, p::OCC_RECORD_SIZE,
                                                 count));
  TEST_ASSERT_EQUAL_INT((int)p::RecordStatus::BAD_LENGTH,
                        (int)p::decodeOccupancy(buf, p::OCC_RECORD_SIZE - 1,
                                                 count));
  TEST_ASSERT_EQUAL_INT((int)p::RecordStatus::BAD_LENGTH,
                        (int)p::decodeOccupancy(buf, p::OCC_RECORD_SIZE + 1,
                                                 count));
  TEST_ASSERT_EQUAL_UINT8(77, count); // nothing written on any failure

  // encode refuses to ever write an out-of-range count
  TEST_ASSERT_EQUAL_UINT32(
      0, p::encodeOccupancy(static_cast<uint8_t>(CAPACITY) + 1, buf,
                            sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT32(0, p::encodeOccupancy(4, buf, 4)); // cap too small

  // round-trip at both bounds
  TEST_ASSERT_EQUAL_UINT32(p::OCC_RECORD_SIZE, p::encodeOccupancy(0, buf,
                                                                   sizeof(buf)));
  TEST_ASSERT_EQUAL_INT((int)p::RecordStatus::OK,
                        (int)p::decodeOccupancy(buf, sizeof(buf), count));
  TEST_ASSERT_EQUAL_UINT8(0, count);
  TEST_ASSERT_EQUAL_UINT32(
      p::OCC_RECORD_SIZE,
      p::encodeOccupancy(static_cast<uint8_t>(CAPACITY), buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_INT((int)p::RecordStatus::OK,
                        (int)p::decodeOccupancy(buf, sizeof(buf), count));
  TEST_ASSERT_EQUAL_UINT8(CAPACITY, count);

  // Each field is validated BEFORE the CRC, so one mutation hits exactly
  // one status (checks run in documented order: magic, version, capacity,
  // count, reserved, crc).
  TEST_ASSERT_EQUAL_UINT32(p::OCC_RECORD_SIZE, p::encodeOccupancy(4, buf,
                                                                   sizeof(buf)));
  buf[0] = 'X';
  TEST_ASSERT_EQUAL_INT((int)p::RecordStatus::BAD_MAGIC,
                        (int)p::decodeOccupancy(buf, sizeof(buf), count));

  TEST_ASSERT_EQUAL_UINT32(p::OCC_RECORD_SIZE, p::encodeOccupancy(4, buf,
                                                                   sizeof(buf)));
  buf[4] ^= 0x01u;
  TEST_ASSERT_EQUAL_INT((int)p::RecordStatus::BAD_VERSION,
                        (int)p::decodeOccupancy(buf, sizeof(buf), count));

  TEST_ASSERT_EQUAL_UINT32(p::OCC_RECORD_SIZE, p::encodeOccupancy(4, buf,
                                                                   sizeof(buf)));
  buf[6] = static_cast<uint8_t>(CAPACITY) + 1u;
  TEST_ASSERT_EQUAL_INT((int)p::RecordStatus::BAD_CAPACITY,
                        (int)p::decodeOccupancy(buf, sizeof(buf), count));

  TEST_ASSERT_EQUAL_UINT32(p::OCC_RECORD_SIZE, p::encodeOccupancy(4, buf,
                                                                   sizeof(buf)));
  buf[8] = static_cast<uint8_t>(CAPACITY) + 1u;
  TEST_ASSERT_EQUAL_INT((int)p::RecordStatus::BAD_COUNT,
                        (int)p::decodeOccupancy(buf, sizeof(buf), count));

  TEST_ASSERT_EQUAL_UINT32(p::OCC_RECORD_SIZE, p::encodeOccupancy(4, buf,
                                                                   sizeof(buf)));
  buf[9] = 1u;
  TEST_ASSERT_EQUAL_INT((int)p::RecordStatus::BAD_RESERVED,
                        (int)p::decodeOccupancy(buf, sizeof(buf), count));

  TEST_ASSERT_EQUAL_UINT32(p::OCC_RECORD_SIZE, p::encodeOccupancy(4, buf,
                                                                   sizeof(buf)));
  buf[12] ^= 0xFFu;
  TEST_ASSERT_EQUAL_INT((int)p::RecordStatus::BAD_CRC,
                        (int)p::decodeOccupancy(buf, sizeof(buf), count));

  // CRC implementation check against the IEEE reference vector
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u,
                          p::crc32(reinterpret_cast<const uint8_t*>(
                                       "123456789"),
                                   9));
}

// ---------------------------------------------------------------------------
// 19. M1B — millis() rollover in the passage timers
// ---------------------------------------------------------------------------

static void test_passage_grace_survives_millis_rollover(void) {
  PassageDetector p;
  PassageUpdate f;
  const uint32_t t0 = 0xFFFFF500u; // 2816 ms before the wrap

  p.update(SensorState::VALID_OCCUPIED, SensorState::VALID_FREE, t0, f);
  TEST_ASSERT_TRUE(f.approachStarted);
  p.update(SensorState::VALID_FREE, SensorState::VALID_FREE, t0 + 100u, f);
  TEST_ASSERT_FALSE(f.cancelled); // grace started pre-wrap
  p.update(SensorState::VALID_FREE, SensorState::VALID_FREE,
           t0 + 100u + PASSAGE_GRACE_MS - 1u, f); // crosses 2^32
  TEST_ASSERT_FALSE(f.cancelled);                 // 2999 ms of 3000
  p.update(SensorState::VALID_FREE, SensorState::VALID_FREE,
           t0 + 100u + PASSAGE_GRACE_MS, f);
  TEST_ASSERT_TRUE(f.cancelled); // 3000 ms exact across the wrap
}

// ---------------------------------------------------------------------------
// 21. M1B — event ordering: unique monotonic seq (uint32, not uint16)
// ---------------------------------------------------------------------------

static void test_event_seq_unique_and_monotonic(void) {
  EventQueue q;
  // 70 000 push/pop cycles: a uint16 seq would wrap at 65 536 and repeat —
  // this proves unique, monotonic IDs within a boot session (FR: unique
  // event IDs; durable cross-boot IDs are M2).
  for (uint32_t i = 0; i < 70000u; ++i) {
    ParkEvent in;
    in.type = EventType::SENSOR_CHANGED;
    in.sensorId = SENSOR_ZONE_A;
    in.value = static_cast<int16_t>(i & 0x7FFF);
    in.atMs = i;
    in.seq = 0; // assigned by the queue, not by the caller
    if (!q.push(in)) {
      TEST_FAIL_MESSAGE("queue rejected an event while not full");
      break;
    }
    ParkEvent out;
    TEST_ASSERT_TRUE(q.pop(out));
    TEST_ASSERT_EQUAL_UINT32(i, out.seq);
  }
  TEST_ASSERT_EQUAL_UINT32(70000u, q.nextSeq());
}

// ---------------------------------------------------------------------------
// scheduler
// ---------------------------------------------------------------------------

static void test_slot_for_advances_and_survives_rollover(void) {
  TEST_ASSERT_EQUAL_UINT8(0, ParkingController::slotFor(0));
  TEST_ASSERT_EQUAL_UINT8(1, ParkingController::slotFor(80));
  TEST_ASSERT_EQUAL_UINT8(2, ParkingController::slotFor(160));
  TEST_ASSERT_EQUAL_UINT8(3, ParkingController::slotFor(240));
  TEST_ASSERT_EQUAL_UINT8(0, ParkingController::slotFor(320));

  bool seen[4] = {false, false, false, false};
  uint32_t t = 0xFFFFFF00u; // schedule crossing the millis() wrap
  for (int i = 0; i < 40; ++i) {
    seen[ParkingController::slotFor(t)] = true;
    t += SENSOR_SLOT_MS;
  }
  TEST_ASSERT_TRUE(seen[0] && seen[1] && seen[2] && seen[3]);
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();

  // 1. thresholds
  RUN_TEST(test_fresh_classification_boundaries);
  RUN_TEST(test_hysteresis_occupied_to_free);
  RUN_TEST(test_hysteresis_free_to_occupied);
  // 2. timeouts / invalid
  RUN_TEST(test_timeout_is_unknown_never_free);
  RUN_TEST(test_fault_escalation_and_recovery);
  RUN_TEST(test_stale_reading_becomes_unknown_rollover);
  // 3. filtering
  RUN_TEST(test_filter_reports_state_changes);
  RUN_TEST(test_noise_does_not_flip_state);
  // 4. passage / debounce
  RUN_TEST(test_approach_then_cancel_never_confirms);
  RUN_TEST(test_approach_debounce_blocks_retrigger);
  RUN_TEST(test_single_beam_never_reaches_confirmed);
  RUN_TEST(test_confirmed_requires_ordered_second_point);
  // 5. occupancy bounds
  RUN_TEST(test_occupancy_boots_unknown);
  RUN_TEST(test_occupancy_reconcile_bounds);
  RUN_TEST(test_occupancy_change_bounds);
  // 6. duplicates
  RUN_TEST(test_event_queue_duplicate_rejected);
  RUN_TEST(test_event_queue_overflow_counted_and_fifo);
  RUN_TEST(test_controller_denial_emitted_once);
  // 7. unknown / fault
  RUN_TEST(test_admission_gates_on_occupancy);
  RUN_TEST(test_unknown_lane_aborts_closing_sweep);
  RUN_TEST(test_occupied_lane_aborts_closing_sweep);
  // 8. barrier
  RUN_TEST(test_barrier_open_sweep_is_nonblocking);
  RUN_TEST(test_barrier_close_needs_sustained_valid_free);
  RUN_TEST(test_barrier_close_blocked_by_occupied_lane);
  RUN_TEST(test_barrier_unknown_lane_never_closes);
  RUN_TEST(test_barrier_fault_latches);
  RUN_TEST(test_barrier_manual_isolation);
  RUN_TEST(test_barrier_manual_release_mid_sweep_is_guarded);
  RUN_TEST(test_barrier_manual_release_mid_sweep_closes_when_clear);
  RUN_TEST(test_barrier_takeover_clears_stale_abort_reopen);
  // 9. timers / rollover
  RUN_TEST(test_barrier_sweep_survives_millis_rollover);
  RUN_TEST(test_lane_clear_timer_survives_millis_rollover);
  RUN_TEST(test_sensor_stale_timer_survives_rollover);
  // 10. entry/exit integration
  RUN_TEST(test_entry_approach_opens_gate_but_never_counts);
  RUN_TEST(test_full_cycle_closes_only_when_lane_observed_clear);
  RUN_TEST(test_exit_approach_never_touches_gate);
  RUN_TEST(test_reconcile_drives_events_and_view);
  // 11. M1B: successful entry / exit (two-point confirmation)
  RUN_TEST(test_successful_entry_confirms_and_counts);
  RUN_TEST(test_successful_exit_confirms_and_counts);
  // 12. M1B: reversal / incomplete passage (grace window)
  RUN_TEST(test_reversal_cancels_after_grace_without_confirm);
  RUN_TEST(test_gap_crossing_confirms_within_grace);
  // 13. M1B: repeated triggering, debounce, timeout/fault
  RUN_TEST(test_beam_chatter_never_double_confirms);
  RUN_TEST(test_beam_debounce_rejects_short_glitches);
  RUN_TEST(test_beam_stale_becomes_unknown_never_free);
  RUN_TEST(test_primary_sensor_loss_cancels_after_tolerance);
  RUN_TEST(test_faulted_secondary_never_confirms);
  // 14. M1B: simultaneous lanes + bounded counts
  RUN_TEST(test_simultaneous_entry_and_exit);
  RUN_TEST(test_confirmed_passage_respects_occupancy_bounds);
  // 15/16. M1B: obstruction guard / emergency stop (FR-005)
  RUN_TEST(test_obstruction_unknown_never_authorizes_close);
  RUN_TEST(test_obstruction_blocks_explicit_close_intent);
  // 17/20. M1B: persistence recovery and record validation (FR-007)
  RUN_TEST(test_restore_from_valid_record);
  RUN_TEST(test_corrupt_record_rejected_stays_unknown);
  RUN_TEST(test_persistence_record_validation);
  // 19. M1B: passage timers across millis() rollover
  RUN_TEST(test_passage_grace_survives_millis_rollover);
  // 21. M1B: unique monotonic event seq
  RUN_TEST(test_event_seq_unique_and_monotonic);
  // display
  RUN_TEST(test_display_lines_are_exactly_16_chars);
  RUN_TEST(test_display_priority_messages);
  RUN_TEST(test_pad_line_truncates_and_pads);
  // scheduler
  RUN_TEST(test_slot_for_advances_and_survives_rollover);

  return UNITY_END();
}
