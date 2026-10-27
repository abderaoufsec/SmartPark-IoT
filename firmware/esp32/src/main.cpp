/**
 * SmartPark-IoT V2 firmware entry point (ESP32 Arduino glue) — M1A.
 *
 * This file contains ONLY hardware wiring and scheduling. All business logic
 * (filtering, passage, occupancy, barrier FSM, display formatting, events)
 * lives in lib/smartpark_core and is unit-tested on the host (pio test -e
 * native). Behavior changes vs V1 are listed in firmware/esp32/README.md.
 *
 * Safety posture at boot (documented, intended):
 *  - occupancy is UNKNOWN (no persistence yet, NFR-004) => admission is
 *    deferred and the gate stays CLOSED until an explicit reconciliation;
 *  - the gate path must be CLEAR when power is applied (no obstruction
 *    sensor in M1A — docs/hardware/pinmap-v2.md §9/§10);
 *  - loop() contains no delay(): every periodic action is millis()-scheduled.
 */
#include <Arduino.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#include "display_manager.hpp"
#include "parking_controller.hpp"
#include "pins.hpp"
#include "sensor_reader.hpp"

using namespace smartpark;

static LiquidCrystal_I2C lcd(pins::LCD_ADDR, 16, 2);
static Servo gateServo; // ESP32Servo exports the classic `Servo` class name
static SensorReader reader;
static ParkingController ctrl;

static uint32_t lastSlotMs = 0;
static uint32_t lastLcdMs = 0;
static uint8_t appliedAngle = 0xFF; // forces the first servo write

static bool zoneLedLevel(SensorState s, bool blink) {
  if (s == SensorState::VALID_OCCUPIED) return true;
  if (s == SensorState::VALID_FREE) return false;
  return blink; // INIT / UNKNOWN / FAULT blink: "do not trust this zone"
}

static void applyLeds(const ControllerView& v, uint32_t nowMs) {
  // Admission lamps: green = known free space, red = known full, both OFF =
  // occupancy unknown (never advertise availability we cannot prove).
  const bool known = v.occStatus == OccupancyStatus::KNOWN;
  digitalWrite(pins::LED_GREEN, (known && v.freeSpaces > 0) ? HIGH : LOW);
  digitalWrite(pins::LED_RED, (known && v.freeSpaces == 0) ? HIGH : LOW);

  const bool blink = ((nowMs / LED_BLINK_MS) & 1UL) != 0UL;
  digitalWrite(pins::LED_ZONE_A, zoneLedLevel(v.zone[0], blink) ? HIGH : LOW);
  digitalWrite(pins::LED_ZONE_B, zoneLedLevel(v.zone[1], blink) ? HIGH : LOW);
}

static void refreshLcd(const ControllerView& v, uint32_t nowMs) {
  if (nowMs - lastLcdMs < LCD_INTERVAL_MS) return;
  lastLcdMs = nowMs;
  const DisplayLines lines = formatDisplay(v); // always exactly 16 chars
  lcd.setCursor(0, 0);
  lcd.print(lines.line1);
  lcd.setCursor(0, 1);
  lcd.print(lines.line2);
}

static void drainEvents() {
  ParkEvent e;
  while (ctrl.popEvent(e)) {
    Serial.printf("[%lu] seq=%u %s", static_cast<unsigned long>(e.atMs),
                  static_cast<unsigned>(e.seq), toString(e.type));
    if (e.sensorId != EVENT_NO_SENSOR)
      Serial.printf(" sensor=%u", e.sensorId);
    Serial.printf(" value=%d\n", e.value);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("SmartPark-IoT V2 (M1A) boot"));
  Serial.println(F("occupancy=UNKNOWN until reconciled; gate stays CLOSED"));

  reader.begin();

  pinMode(pins::LED_GREEN, OUTPUT);
  pinMode(pins::LED_RED, OUTPUT);
  pinMode(pins::LED_ZONE_A, OUTPUT);
  pinMode(pins::LED_ZONE_B, OUTPUT);
  digitalWrite(pins::LED_GREEN, LOW);
  digitalWrite(pins::LED_RED, LOW);
  digitalWrite(pins::LED_ZONE_A, LOW);
  digitalWrite(pins::LED_ZONE_B, LOW);

  // Gate rests CLOSED at boot (V1-compatible resting position). Power on
  // with the path clear: there is no obstruction sensor in M1A.
  gateServo.setPeriodHertz(50);
  gateServo.attach(pins::SERVO, 500, 2400);
  gateServo.write(BARRIER_CLOSED_ANGLE);
  appliedAngle = BARRIER_CLOSED_ANGLE;
  if (!gateServo.attached()) {
    ctrl.barrier().setActuatorFault();
    Serial.println(F("E: servo attach failed -> barrier latched FAULT"));
  }

  lcd.init(); // library-internal init timings only; no app splash delay
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("SMARTPARK V2"));
  lcd.setCursor(0, 1);
  lcd.print(F("BOOT - NO COUNT"));

  lastSlotMs = millis();
}

void loop() {
  const uint32_t nowMs = millis();

  // 1. Sensor slot scheduler: one transducer per SENSOR_SLOT_MS window
  //    (independent triggering — no two sensors fire together, F-01).
  if (nowMs - lastSlotMs >= SENSOR_SLOT_MS) {
    lastSlotMs = nowMs; // simple reschedule; catch-up bursts are pointless
    const uint8_t idx = ParkingController::slotFor(nowMs);
    const long cm = reader.measureCm(idx); // bounded: <= ECHO_TIMEOUT_US
    ctrl.onSample(idx, cm, nowMs);
  }

  // 2. Policy pass: passage lanes, barrier FSM, state-change events.
  ctrl.tick(nowMs);

  // 3. Actuator: apply the servo command only when it changed (the FSM
  //    already quantizes sweeps to 2°/15 ms steps — no delay() needed).
  const uint8_t target = ctrl.barrier().angle();
  if (target != appliedAngle) {
    gateServo.write(target);
    appliedAngle = target;
  }

  // 4. Indicators and display — interval-gated, never blocking.
  const ControllerView v = ctrl.view(nowMs);
  applyLeds(v, nowMs);
  refreshLcd(v, nowMs);
  drainEvents(); // serial event log (MQTT will consume this queue in M2)
}
