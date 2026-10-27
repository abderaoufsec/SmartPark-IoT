#pragma once
/**
 * SmartPark-IoT V2 — event definitions.
 *
 * Events are the unit of observable behavior: serial logs today, MQTT in M2.
 * Pure C++ (native-testable), fixed-size struct, no allocation.
 */
#include <stdint.h>
#include "config.hpp"

namespace smartpark {

enum class EventType : uint8_t {
  ENTRY_APPROACH = 0,       // entry beam saw a stable OCCUPIED (never a count!)
  ENTRY_APPROACH_CANCELLED, // beam cleared without a confirmed passage
  EXIT_APPROACH,
  EXIT_APPROACH_CANCELLED,
  PASSAGE_CONFIRMED_ENTRY,  // unreachable with current single-point hardware
  PASSAGE_CONFIRMED_EXIT,
  OCCUPANCY_RECONCILED,     // operator/system set an explicit count
  OCCUPANCY_CHANGE_REJECTED,// out-of-range attempt (no silent clamping, F-10)
  ADMISSION_DENIED,         // gate refused entry (full or unknown, FR-004)
  BARRIER_STATE_CHANGED,    // value = BarrierState (see barrier_controller)
  SENSOR_CHANGED,           // sensorId + value = SensorState
  SENSOR_FAULT,             // sensorId escalated to FAULT
};

inline const char* toString(EventType t) {
  switch (t) {
    case EventType::ENTRY_APPROACH:            return "ENTRY_APPROACH";
    case EventType::ENTRY_APPROACH_CANCELLED:  return "ENTRY_APPROACH_CANCELLED";
    case EventType::EXIT_APPROACH:             return "EXIT_APPROACH";
    case EventType::EXIT_APPROACH_CANCELLED:   return "EXIT_APPROACH_CANCELLED";
    case EventType::PASSAGE_CONFIRMED_ENTRY:   return "PASSAGE_CONFIRMED_ENTRY";
    case EventType::PASSAGE_CONFIRMED_EXIT:    return "PASSAGE_CONFIRMED_EXIT";
    case EventType::OCCUPANCY_RECONCILED:      return "OCCUPANCY_RECONCILED";
    case EventType::OCCUPANCY_CHANGE_REJECTED: return "OCCUPANCY_CHANGE_REJECTED";
    case EventType::ADMISSION_DENIED:          return "ADMISSION_DENIED";
    case EventType::BARRIER_STATE_CHANGED:     return "BARRIER_STATE_CHANGED";
    case EventType::SENSOR_CHANGED:            return "SENSOR_CHANGED";
    case EventType::SENSOR_FAULT:              return "SENSOR_FAULT";
  }
  return "UNKNOWN_EVENT";
}

/** Fixed-size event record. `seq` is assigned by the queue (M2 uses it). */
struct ParkEvent {
  EventType type;
  uint8_t sensorId; // SENSOR_xxx for sensor events, 0xFF otherwise
  int16_t value;    // type-specific payload (state enums, counts, distances)
  uint32_t atMs;    // millis() timestamp supplied by the controller
  uint16_t seq;     // monotonically increasing, assigned on enqueue
};

constexpr uint8_t EVENT_NO_SENSOR = 0xFF;

} // namespace smartpark
