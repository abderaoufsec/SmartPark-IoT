#pragma once
/**
 * SmartPark-IoT V2 — MQTT client (M2, ESP32 only).
 *
 * Thin wrapper around PubSubClient. Publishes events from the PersistentQueue
 * and a periodic heartbeat. All logic is non-blocking: loop() must be called
 * from the Arduino loop().
 *
 * Pure header (no Arduino includes) so it can be referenced by test code;
 * the PubSubClient instance lives in mqtt_client.cpp.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "persistent_queue.hpp"

namespace smartpark {

class MqttClient {
public:
  /** Build topic base from facility + device IDs (see .env.example). */
  static void makeTopicBase(char* out, size_t cap,
                            const char* facilityId,
                            const char* deviceId);

  explicit MqttClient(PersistentQueue& queue);

  /** Configure broker connection. Call once from setup(). */
  bool begin(const char* host, uint16_t port, const char* clientId,
             const char* username = nullptr, const char* password = nullptr);

  /** Build topic strings from facility/device IDs. Call before begin(). */
  void topics(const char* facilityId, const char* deviceId);

  /** Non-blocking: call every loop(). Handles reconnect + keepalive. */
  void loop(uint32_t nowMs);

  /** Connected to broker? */
  bool connected() const { return connected_; }

  /** Publish a retained heartbeat. Call periodically from loop(). */
  void publishHeartbeat(uint32_t nowMs);

  /** Publish pending events from the queue (in order, stop on first failure). */
  void publishQueuedEvents();

  /** Disconnect cleanly. */
  void disconnect();

  const char* topicEvents() const { return topicEvents_; }
  const char* topicHeartbeat() const { return topicHeartbeat_; }

private:
  void reconnect(uint32_t nowMs);
  bool publishEvent(const ParkEvent& e);

  PersistentQueue& queue_;
  bool connected_;
  uint32_t lastReconnectAttemptMs_;
  uint32_t lastHeartbeatMs_;
  const char* host_;
  uint16_t port_;
  char clientId_[32];
  char user_[32];
  char pass_[32];

  char topicBase_[64];   // smartpark/{facility}/{device}
  char topicEvents_[96]; // smartpark/{facility}/{device}/events
  char topicHeartbeat_[96]; // smartpark/{facility}/{device}/heartbeat
};

} // namespace smartpark