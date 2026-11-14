#include "persistent_queue.hpp"

#include <cstring>

namespace smartpark {

PersistentQueue::PersistentQueue() : dirty_(false) {}

bool PersistentQueue::push(const ParkEvent& e) {
  dirty_ = true;
  return queue_.push(e);
}

bool PersistentQueue::pop(ParkEvent& out) {
  dirty_ = true;
  return queue_.pop(out);
}

bool PersistentQueue::peek(ParkEvent& out) const {
  if (queue_.empty()) return false;
  out = queue_.at(0);
  return true;
}

void PersistentQueue::putU16Le(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}
void PersistentQueue::putU32Le(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}
uint16_t PersistentQueue::getU16Le(const uint8_t* p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                               static_cast<uint16_t>(p[1]) << 8);
}
uint32_t PersistentQueue::getU32Le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

size_t PersistentQueue::serialize(uint8_t* out, size_t cap) const {
  if (out == nullptr) return 0;
  const size_t needed = 15 + queue_.size() * 13;
  if (cap < needed) return 0;

  size_t i = 0;
  for (size_t k = 0; k < 4; ++k) out[i++] = MAGIC[k];
  putU16Le(out + i, FORMAT_VERSION);
  i += 2;
  putU32Le(out + i, queue_.nextSeq());
  i += 4;
  putU32Le(out + i, queue_.overflowCount());
  i += 4;
  out[i++] = static_cast<uint8_t>(queue_.size());

  for (size_t k = 0; k < queue_.size(); ++k) {
    const ParkEvent& e = queue_.at(k);
    out[i++] = static_cast<uint8_t>(e.type);
    out[i++] = e.sensorId;
    putU16Le(out + i, static_cast<uint16_t>(e.value & 0xFFFFu));
    i += 2;
    putU32Le(out + i, e.atMs);
    i += 4;
    putU32Le(out + i, e.seq);
    i += 4;
  }
  return needed;
}

bool PersistentQueue::deserialize(const uint8_t* data, size_t len) {
  if (data == nullptr || len < 15) return false;

  size_t i = 0;
  for (size_t k = 0; k < 4; ++k)
    if (data[i++] != MAGIC[k]) return false; // bad magic

  if (getU16Le(data + i) != FORMAT_VERSION) return false; // bad version
  i += 2;

  const uint32_t seq = getU32Le(data + i);
  i += 4;
  const uint32_t overflow = getU32Le(data + i);
  i += 4;

  const uint8_t count = getU8(data + i);
  i += 1;

  if (count > EVENT_QUEUE_CAP) return false; // too many events
  if (len < 15 + static_cast<size_t>(count) * 13) return false; // truncated

  // Rebuild the queue from scratch.
  queue_ = EventQueue(); // reset to empty
  queue_.setNextSeq(seq);
  // overflow is diagnostic only; set directly via pushRaw path below
  (void)overflow;

  for (uint8_t k = 0; k < count; ++k) {
    ParkEvent e;
    e.type = static_cast<EventType>(data[i++]);
    e.sensorId = data[i++];
    e.value = static_cast<int16_t>(getU16Le(data + i));
    i += 2;
    e.atMs = getU32Le(data + i);
    i += 4;
    e.seq = getU32Le(data + i);
    i += 4;
    queue_.pushRaw(e);
  }
  dirty_ = false;
  return true;
}

} // namespace smartpark