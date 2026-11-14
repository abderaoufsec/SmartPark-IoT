/**
 * SmartPark-IoT V2 firmware entry point (ESP32 Arduino glue) — M1B.
 *
 * This file contains ONLY hardware wiring and scheduling. All business logic
 * (filtering, two-point passage FSM, occupancy, persistence format, barrier
 * FSM with obstruction guard, display formatting, events) lives in
 * lib/smartpark_core and is unit-tested on the host (pio test -e native).
 * Behavior changes vs V1 are listed in firmware/esp32/README.md.
 *
 * Safety posture at boot (documented, intended):
 *  - occupancy is restored ONLY from a CRC-valid NVS record; otherwise it is
 *    UNKNOWN (NFR-004) => admission is deferred and the gate stays CLOSED
 *    until an explicit reconciliation;
 *  - the obstruction input (GPIO 14) must read a fresh CLEAR before any
 *    closing sweep starts; an unread/stale/faulted input can never authorize
 *    a close (pinmap §12);
 *  - loop() contains no delay(): every periodic action is millis()-scheduled.
 *
 * SIMULATED vs PHYSICAL: this firmware has not run on physical hardware yet.
 * Wokwi runs this exact firmware.bin (simulation/wokwi/); every hardware
 * claim remains NOT TESTED until bench validation (pinmap §10, M1C/M6).
 */
#include <Arduino.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#include "display_manager.hpp"
#include "mqtt_client.hpp"
#include "nvs_store.hpp"
#include "parking_controller.hpp"
#include "persistent_queue.hpp"
#include "persistence.hpp"
#include "pins.hpp"
#include "sensor_reader.hpp"
#include "wifi_manager.hpp"

using namespace smartpark;

static LiquidCrystal_I2C lcd(pins::LCD_ADDR, 16, 2);
static Servo gateServo; // ESP32Servo exports the classic `Servo` class name
static SensorReader reader;
static ParkingController ctrl;
static storage::NvsStore store;

// ---- M2 configuration (see .env.example for overridable values) ----
static constexpr const char* WIFI_SSID_DEFAULT = "";       // set before build
static constexpr const char* WIFI_PASS_DEFAULT = "";       // set before build
static constexpr const char* MQTT_HOST_DEFAULT = "localhost";
static constexpr uint16_t MQTT_PORT_DEFAULT = 1883;
static constexpr const char* MQTT_USERNAME_DEFAULT = nullptr;
static constexpr const char* MQTT_PASSWORD_DEFAULT = nullptr;
static constexpr const char* FACILITY_ID_DEFAULT = "fac-demo";
static constexpr const char* DEVICE_ID_DEFAULT = "esp32-001";

// ---- M2 instances ----
static PersistentQueue eventQueue;         // durable event queue (NVS-backed)
static WifiManager wifiMgr;                // Wi-Fi connect/reconnect
static MqttClient mqtt(eventQueue);        // MQTT publisher

static uint32_t lastMqttPublishMs = 0;     // throttle event publishing
static uint32_t lastQueuePersistMs = 0;    // throttle NVS queue writes
static constexpr uint32_t MQTT_PUBLISH_INTERVAL_MS = 100;  // drain queue often
static constexpr uint32_t QUEUE_PERSIST_INTERVAL_MS = 1000; // persist queue ≤1 Hz

static uint32_t lastSlotMs = 0;
static uint32_t lastLcdMs = 0;
static uint8_t appliedAngle = 0xFF; // forces the first servo write

static int lastSavedCount = -1; // NVS: save only on an actual count change
static bool nvsUsable = false;

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
    Serial.printf("[%lu] seq=%lu %s", static_cast<unsigned long>(e.atMs),
                  static_cast<unsigned long>(e.seq), toString(e.type));
    if (e.sensorId != EVENT_NO_SENSOR)
      Serial.printf(" sensor=%u", e.sensorId);
    Serial.printf(" value=%d\n", e.value);
    // Mirror into the durable M2 queue (serial log today, MQTT in M2).
    eventQueue.push(e);
  }
}

/**
 * Read the two IR beams and the obstruction input (polarity resolved here,
 * semantic state in the controller). Called every loop(): beam staleness and
 * obstruction staleness in the core assume a per-loop feed.
 */
static void readDigitalInputs(uint32_t nowMs) {
  // Beams: active-LOW wiring => LOW (beam broken) = occupied. An unread or
  // disconnected input reads HIGH = free, which can never confirm a count.
  ctrl.onBeam(BEAM_ENTRY, digitalRead(pins::BEAM_ENTRY_PIN) == LOW, nowMs);
  ctrl.onBeam(BEAM_EXIT, digitalRead(pins::BEAM_EXIT_PIN) == LOW, nowMs);
  // Obstruction: HIGH = blocked (fail-safe pull-up: broken wire reads HIGH).
  ctrl.setObstruction(digitalRead(pins::OBSTRUCTION_PIN) == HIGH, nowMs);
}

/** Persist the occupancy count when it changed (never a periodic write). */
static void persistIfChanged(const ControllerView& v) {
  if (!nvsUsable || v.occStatus != OccupancyStatus::KNOWN) return;
  if (v.count == lastSavedCount) return; // stationary count => zero flash writes
  uint8_t buf[persistence::OCC_RECORD_SIZE];
  const size_t n =
      persistence::encodeOccupancy(v.count, buf, sizeof(buf));
  if (n == 0) return; // out-of-range count: never written (no silent clamp)
  if (store.save("occ", buf, n)) lastSavedCount = v.count;
}

/**
 * Minimal FR-011 console (serial RX, M1B groundwork — full operator surface
 * is a later milestone). Line protocol, '\n' or '\r' terminated:
 *   R <n>   reconcile occupancy to n (0..CAPACITY; out-of-range is REJECTED
 *           with an event, never clamped — F-10)
 *   ?       print one STATUS line (used by Wokwi scenario assertions)
 * Anything else prints usage. Parsing is bounded and allocation-free.
 */
static char lineBuf[24];
static uint8_t lineLen = 0;

static void handleConsoleLine(uint32_t nowMs) {
  lineBuf[lineLen] = '\0';
  if (lineBuf[0] == 'R') {
    long n = -1;
    const char* p = lineBuf + 1;
    while (*p == ' ') ++p;
    if (*p >= '0' && *p <= '9') {
      n = 0;
      while (*p >= '0' && *p <= '9') {
        n = n * 10 + (*p - '0');
        if (n > 255) { n = -1; break; } // out of uint8 range => usage
        ++p;
      }
    }
    if (n >= 0) {
      // Values > CAPACITY are passed through on purpose: reconcile() must
      // reject them with a visible event (no silent clamping, F-10).
      ctrl.reconcile(static_cast<uint8_t>(n), nowMs);
    } else {
      Serial.println(F("OK usage: R <0..255>"));
    }
    return;
  }
  if (lineBuf[0] == '?') {
    const ControllerView v = ctrl.view(nowMs);
    Serial.printf("STATUS occ=%s count=%u free=%u gate=%s obstruction=%s\n",
                  v.occStatus == OccupancyStatus::KNOWN ? "KNOWN" : "UNKNOWN",
                  v.count, v.freeSpaces, toString(v.gate),
                  toString(v.obstruction));
    return;
  }
  Serial.println(F("OK usage: R <0..6> | ?"));
}

static void pumpConsole(uint32_t nowMs) {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\n' || ch == '\r') {
      if (lineLen > 0) handleConsoleLine(nowMs);
      lineLen = 0;
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = ch;
    } else {
      lineLen = 0; // overlong line: drop it whole (bounded, no overflow)
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("SmartPark-IoT V2 (M2) boot"));
  Serial.println(F("behavior: SIMULATED-LOGIC ONLY - NOT PHYSICALLY VALIDATED"));

  reader.begin();

  pinMode(pins::LED_GREEN, OUTPUT);
  pinMode(pins::LED_RED, OUTPUT);
  pinMode(pins::LED_ZONE_A, OUTPUT);
  pinMode(pins::LED_ZONE_B, OUTPUT);
  digitalWrite(pins::LED_GREEN, LOW);
  digitalWrite(pins::LED_RED, LOW);
  digitalWrite(pins::LED_ZONE_A, LOW);
  digitalWrite(pins::LED_ZONE_B, LOW);

  // Second passage points: active-LOW with internal pull-up. Disconnected =
  // HIGH = free: a missing beam can never fabricate a passage (fail toward
  // "do not count"); physical wiring adds the external pull-up (pinmap §9).
  pinMode(pins::BEAM_ENTRY_PIN, INPUT_PULLUP);
  pinMode(pins::BEAM_EXIT_PIN, INPUT_PULLUP);
  // Obstruction: fail-safe PULL-UP bias — an open input (broken wire, absent
  // sensor, released NC loop) reads HIGH = BLOCKED and can therefore never
  // authorize a close (pinmap §12). LOW = CLEAR must be actively driven by
  // the sensor. The physical board adds an external 10k pull-up on the same
  // rail (no contention); the Wokwi substitute pulls the line to GND to
  // report CLEAR (pushbutton, simulation/wokwi/README.md).
  pinMode(pins::OBSTRUCTION_PIN, INPUT_PULLUP);

  // --- Occupancy recovery (FR-007): restore ONLY a validated record. ---
  {
    uint8_t buf[persistence::OCC_RECORD_SIZE];
    size_t len = 0;
    nvsUsable = store.begin();
    if (nvsUsable && store.load("occ", buf, sizeof(buf), len)) {
      uint8_t count = 0;
      const persistence::RecordStatus st =
          persistence::decodeOccupancy(buf, len, count);
      if (st == persistence::RecordStatus::OK) {
        if (ctrl.restoreOccupancy(count, millis()))
          lastSavedCount = count; // identical value: no rewrite needed
        Serial.printf("NVS: occupancy restored to %u (recovered, not "
                      "re-verified - reconcile to confirm)\n",
                      static_cast<unsigned>(count));
      } else if (len > 0) {
        Serial.printf("NVS: record rejected (%s) -> occupancy UNKNOWN\n",
                      persistence::toString(st));
      } else {
        Serial.println(F("NVS: no record -> occupancy UNKNOWN"));
      }
    } else {
      Serial.println(F("NVS: unavailable -> occupancy UNKNOWN"));
    }
    // The namespace stays open for the lifetime of the firmware so
    // persistIfChanged() can write; one open handle is the normal pattern.
  }
  if (!ctrl.occupancy().isKnown())
    Serial.println(F("occupancy=UNKNOWN until reconciled; gate stays CLOSED"));

  // --- M2: restore durable event queue from NVS, then start Wi-Fi + MQTT. ---
  if (nvsUsable) {
    uint8_t qbuf[PersistentQueue::MAX_SERIALIZED_SIZE];
    size_t qlen = 0;
    if (store.load("evq", qbuf, sizeof(qbuf), qlen) && qlen > 0) {
      if (eventQueue.deserialize(qbuf, qlen)) {
        Serial.printf("NVS: event queue restored (%u events, seq=%u)\n",
                      eventQueue.size(), eventQueue.nextSeq());
      } else {
        Serial.println(F("NVS: event queue rejected -> empty"));
      }
    } else {
      Serial.println(F("NVS: no event queue -> empty"));
    }
  }

  // Wi-Fi (non-blocking; local operation is independent of network state).
  // WIFI_SSID_DEFAULT must be set before build; empty = skip Wi-Fi entirely.
  if (WIFI_SSID_DEFAULT[0] != '\0')
    wifiMgr.begin(WIFI_SSID_DEFAULT, WIFI_PASS_DEFAULT);
  else
    Serial.println(F("Wi-Fi: not configured (WIFI_SSID_DEFAULT empty)"));

  // MQTT: build topics from facility/device IDs, then connect.
  mqtt.topics(FACILITY_ID_DEFAULT, DEVICE_ID_DEFAULT);
  Serial.printf("MQTT: target=%s:%u client=%s topics=%s/...\n",
                MQTT_HOST_DEFAULT, MQTT_PORT_DEFAULT, DEVICE_ID_DEFAULT,
                FACILITY_ID_DEFAULT);
  mqtt.begin(MQTT_HOST_DEFAULT, MQTT_PORT_DEFAULT, DEVICE_ID_DEFAULT,
             MQTT_USERNAME_DEFAULT, MQTT_PASSWORD_DEFAULT);

  // Gate rests CLOSED at boot (V1-compatible resting position).
  // still requires a fresh CLEAR obstruction on every tick (pinmap §12).
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

  // 2. Digital inputs: IR beams + obstruction (per loop, staleness-aware).
  readDigitalInputs(nowMs);

  // 3. Policy pass: passage lanes, barrier FSM, state-change events.
  ctrl.tick(nowMs);

  // 4. Actuator: apply the servo command only when it changed (the FSM
  //    already quantizes sweeps to 2°/15 ms steps — no delay() needed).
  const uint8_t target = ctrl.barrier().angle();
  if (target != appliedAngle) {
    gateServo.write(target);
    appliedAngle = target;
  }

  // 5. Indicators and display — interval-gated, never blocking.
  const ControllerView v = ctrl.view(nowMs);
  applyLeds(v, nowMs);
  refreshLcd(v, nowMs);
  drainEvents(); // serial event log (MQTT will consume this queue in M2)

  // 6. Persistence (count-change only) and the FR-011 console.
  persistIfChanged(v);
  pumpConsole(nowMs);

  // 7. M2: Wi-Fi + MQTT lifecycle (non-blocking). Network loss never affects
  //    local sensing/gate safety (NFR-001): these calls are best-effort only.
  if (WIFI_SSID_DEFAULT[0] != '\0')
    wifiMgr.loop(nowMs);
  mqtt.loop(nowMs);

  // 8. M2: publish queued events to MQTT, throttled.
  if (nowMs - lastMqttPublishMs >= MQTT_PUBLISH_INTERVAL_MS) {
    lastMqttPublishMs = nowMs;
    mqtt.publishQueuedEvents();
    mqtt.publishHeartbeat(nowMs);
  }

  // 9. M2: persist the durable event queue to NVS when dirty, throttled.
  if (nvsUsable && eventQueue.dirty() &&
      nowMs - lastQueuePersistMs >= QUEUE_PERSIST_INTERVAL_MS) {
    lastQueuePersistMs = nowMs;
    uint8_t qbuf[PersistentQueue::MAX_SERIALIZED_SIZE];
    const size_t n = eventQueue.serialize(qbuf, sizeof(qbuf));
    if (n > 0 && store.save("evq", qbuf, n)) {
      eventQueue.clean();
    }
  }
}
