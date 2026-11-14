#pragma once
/**
 * SmartPark-IoT V2 — fixed-capacity event queue.
 *
 * - No dynamic allocation (static ring buffer).
 * - Duplicate suppression: an event identical to the current tail
 *   (type + sensorId + value) is rejected — prevents repeated denial/state
 *   spam within one occurrence (V1 finding F-09 re-logging every loop pass).
 * - Sequence numbers assigned at enqueue (basis for durable event IDs in M2).
 * - Overflow is counted, never silent (drops the NEW event, keeps history).
 */
#include "event_types.hpp"

namespace smartpark {

class EventQueue {
public:
  EventQueue();

  /** Returns false if the event was a duplicate-of-tail or the queue is full. */
  bool push(const ParkEvent& e);

  /** Pops the oldest event. Returns false when empty. */
  bool pop(ParkEvent& out);

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  uint32_t overflowCount() const { return overflow_; }
  uint32_t nextSeq() const { return seq_; }

  /** Set the next sequence number (used when restoring a persisted queue). */
  void setNextSeq(uint32_t seq) { seq_ = seq; }

  /**
   * Push without assigning a new sequence number (used when restoring a
   * persisted queue; seq values are preserved from the stored record).
   * Returns false on duplicate-of-tail or overflow.
   */
  bool pushRaw(const ParkEvent& e);

  /** Random-access for tests/inspection; index 0 = oldest. */
  const ParkEvent& at(size_t i) const { return buf_[(head_ + i) % CAP]; }

private:
  static constexpr size_t CAP = EVENT_QUEUE_CAP;
  ParkEvent buf_[CAP];
  size_t head_;  // index of oldest
  size_t count_;
  uint32_t overflow_;
  uint32_t seq_;
};

} // namespace smartpark
