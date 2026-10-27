#include "sensor_reader.hpp"

#include <Arduino.h>

#include "pins.hpp"

namespace smartpark {

void SensorReader::begin() {
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
    pinMode(pins::TRIG[i], OUTPUT);
    digitalWrite(pins::TRIG[i], LOW);
    pinMode(pins::ECHO[i], INPUT); // driven by the divider, no pulls needed
  }
}

long SensorReader::measureCm(uint8_t sensorIdx) {
  if (sensorIdx >= SENSOR_COUNT) return -1;

  // Fire ONLY this transducer (independent triggering, F-01).
  digitalWrite(pins::TRIG[sensorIdx], LOW);
  delayMicroseconds(2);
  digitalWrite(pins::TRIG[sensorIdx], HIGH);
  delayMicroseconds(10); // HC-SR04 trigger pulse requirement (protocol, not app delay)
  digitalWrite(pins::TRIG[sensorIdx], LOW);

  const uint32_t durationUs = pulseIn(pins::ECHO[sensorIdx], HIGH, ECHO_TIMEOUT_US);
  if (durationUs == 0) return -1; // timeout => UNKNOWN upstream, never FREE (F-03)

  // cm = µs × 0.034 / 2, integer form: 30000 µs × 17 = 510000 (no overflow).
  return static_cast<long>((durationUs * 17UL) / 1000UL);
}

} // namespace smartpark
