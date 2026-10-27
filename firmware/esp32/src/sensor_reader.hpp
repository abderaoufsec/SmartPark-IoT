#pragma once
/**
 * SmartPark-IoT V2 — ultrasonic measurement HAL (ESP32/Arduino only).
 *
 * This is the ONLY hardware-touching piece of the sensing path; it is
 * excluded from the native test environment by design (all decisions live in
 * lib/smartpark_core). Not unit-tested: it drives real GPIOs.
 *
 * Cross-talk policy (V1 F-01): each HC-SR04 gets its own TRIG pulse; the
 * scheduler in ParkingController::slotFor() ensures only one transducer fires
 * per SENSOR_SLOT_MS window. pulseIn() blocks at most ECHO_TIMEOUT_US
 * (30 ms) per slot — a bounded, documented stall (pinmap §4).
 */
#include <stdint.h>
#include "config.hpp"

namespace smartpark {

class SensorReader {
public:
  SensorReader() {}
  /** Configure TRIG/ECHO pin modes. Call once from setup(). */
  void begin();

  /**
   * Measure one sensor: pulse its TRIG, read its ECHO with a bounded timeout.
   * @return distance in centimetres, or -1 for timeout/invalid (never 0=free)
   */
  long measureCm(uint8_t sensorIdx);
};

} // namespace smartpark
