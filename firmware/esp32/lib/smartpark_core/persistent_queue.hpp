#pragma once
/**
 * SmartPark-IoT V2 — persistent event queue (M2).
 *
 * Wraps EventQueue with serialization for NVS persistence. The queue state
 * (events + sequence counter + overflow count) can be serialized to a flat
 * byte buffer and restored from it, so events survive a reboot.
 *
 * Pure logic: no Arduino, no NVS, no dynamic allocation. The caller moves the
 * serialized bytes to/from NVS (see src/nvs_store.hpp for the ESP32 side).
 *
 * Serialized format (little-endian, explicitly laid out — no struct padding
 * / endianness assumptions):
 *   [0..3]   magic  'S','P','Q','1'
 *   [4..5]   format version (uint16) = 1
 *   [6..9]   next sequence number (uint32)
 *   [10..13] overflow count (uint32)
 *   [14]     event count (uint8)
 *   [15..]   events, each 13 bytes:
 *            type(uint8) sensorId(uint8) value(int16) atMs(uint32) seq(uint32)
 *
 * Fail-closed decode: a corrupt/truncated/foreign buffer is rejected and the
 * queue is left empty (never guess at event state).
 */
#include <cstddef>
#include <cstdint>

#include "config.hpp"
#include "event_queue.hpp"

namespace smartpark {

class PersistentQueue {
public:
  /** Serialized record magic: 'S','P','Q','1'. */
  static constexpr uint8_t MAGIC[4] = {0x53, 0x50, 0x51, 0x31};
  /** Bump when the layout changes. */
  static constexpr uint16_t FORMAT_VERSION = 1;

  /** Heap-free upper bound on the serialized size in bytes. */
  static constexpr size_t MAX_SERIALIZED_SIZE =
      15 + EVENT_QUEUE_CAP * 13; // header + max events

  PersistentQueue();

  // ---- EventQueue pass-through ------------------------------------------

  bool push(const ParkEvent& e);
  bool pop(ParkEvent& out);
  bool peek(ParkEvent& out) const; // oldest event without removing

  size_t size() const { return queue_.size(); }
  bool empty() const { return queue_.empty(); }
  uint32_t overflowCount() const { return queue_.overflowCount(); }
  uint32_t nextSeq() const { return queue_.nextSeq(); }

  // ---- Persistence ------------------------------------------------------

  /**
   * Serialize the full queue state into `out`.
   * @return bytes written, or 0 when out is null / cap too small.
   */
  size_t serialize(uint8_t* out, size_t cap) const;

  /**
   * Restore queue state from a serialized buffer.
   * Fail-closed: returns false (and leaves the queue empty) on any validation
   * failure — bad magic, version, length, or event count out of range.
   */
  bool deserialize(const uint8_t* data, size_t len);

  /** @return true when the queue holds events that have not been persisted. */
  bool dirty() const { return dirty_; }
  /** Mark the queue as persisted (call after a successful NVS write). */
  void clean() { dirty_ = false; }

private:
  EventQueue queue_;
  bool dirty_;

  static void putU16Le(uint8_t* p, uint16_t v);
  static void putU32Le(uint8_t* p, uint32_t v);
  static uint8_t getU8(const uint8_t* p) { return *p; }
  static uint16_t getU16Le(const uint8_t* p);
  static uint32_t getU32Le(const uint8_t* p);
};

} // namespace smartpark