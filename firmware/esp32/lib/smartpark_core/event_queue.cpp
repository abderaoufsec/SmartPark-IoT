#include "event_queue.hpp"

namespace smartpark {

EventQueue::EventQueue()
    : buf_(), head_(0), count_(0), overflow_(0), seq_(0) {}

bool EventQueue::push(const ParkEvent& e) {
  // Duplicate suppression: identical to the most recent event.
  if (count_ > 0) {
    const ParkEvent& tail = buf_[(head_ + count_ - 1) % CAP];
    if (tail.type == e.type && tail.sensorId == e.sensorId &&
        tail.value == e.value) {
      return false;
    }
  }
  if (count_ == CAP) {
    ++overflow_; // drop-newest, counted: never a silent loss
    return false;
  }
  ParkEvent stored = e;
  stored.seq = seq_++;
  buf_[(head_ + count_) % CAP] = stored;
  ++count_;
  return true;
}

bool EventQueue::pop(ParkEvent& out) {
  if (count_ == 0) return false;
  out = buf_[head_];
  head_ = (head_ + 1) % CAP;
  --count_;
  return true;
}

} // namespace smartpark
