#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "mqtt_client.hpp"

namespace smartpark {

namespace {
/** File-static MQTT client (single connection per device). */
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

/** JSON buffer for event serialization. Large enough for any event. */
char jsonBuf[128];
} // namespace

void MqttClient::makeTopicBase(char* out, size_t cap,
                               const char* facilityId,
                               const char* deviceId) {
  snprintf(out, cap, "smartpark/%s/%s", facilityId, deviceId);
}void MqttClient::topics(const char* facilityId, const char* deviceId) {
  makeTopicBase(topicBase_, sizeof(topicBase_), facilityId, deviceId);
  snprintf(topicEvents_, sizeof(topicEvents_), "%s/events", topicBase_);
  snprintf(topicHeartbeat_, sizeof(topicHeartbeat_), "%s/heartbeat",
           topicBase_);
}


MqttClient::MqttClient(PersistentQueue& queue)
    : queue_(queue), connected_(false),
      lastReconnectAttemptMs_(0), lastHeartbeatMs_(0),
      host_(nullptr), port_(0) {
  clientId_[0] = '\0';
  user_[0] = '\0';
  pass_[0] = '\0';
  topicBase_[0] = '\0';
  topicEvents_[0] = '\0';
  topicHeartbeat_[0] = '\0';
}

bool MqttClient::begin(const char* host, uint16_t port, const char* clientId,
                       const char* username, const char* password) {
  host_ = host;
  port_ = port;
  strncpy(clientId_, clientId ? clientId : "smartpark-esp32",
          sizeof(clientId_) - 1);
  clientId_[sizeof(clientId_) - 1] = '\0';
  if (username) {
    strncpy(user_, username, sizeof(user_) - 1);
    user_[sizeof(user_) - 1] = '\0';
  } else {
    user_[0] = '\0';
  }
  if (password) {
    strncpy(pass_, password, sizeof(pass_) - 1);
    pass_[sizeof(pass_) - 1] = '\0';
  } else {
    pass_[0] = '\0';
  }

  mqttClient.setServer(host, port);
  connected_ = mqttClient.connect(clientId_,
                                  user_[0] ? user_ : nullptr,
                                  pass_[0] ? pass_ : nullptr);
  if (connected_) {
    mqttClient.loop();
    Serial.println(F("MQTT: connected"));
  } else {
    Serial.println(F("MQTT: connect failed"));
  }
  return connected_;
}

void MqttClient::reconnect(uint32_t nowMs) {
  if (nowMs - lastReconnectAttemptMs_ < 5000) return;
  lastReconnectAttemptMs_ = nowMs;

  if (mqttClient.connect(clientId_,
                         user_[0] ? user_ : nullptr,
                         pass_[0] ? pass_ : nullptr)) {
    connected_ = true;
    mqttClient.loop();
    Serial.println(F("MQTT: reconnected"));
  } else {
    connected_ = false;
  }
}

void MqttClient::loop(uint32_t nowMs) {
  if (!connected_) {
    reconnect(nowMs);
    return;
  }
  mqttClient.loop();
  if (!mqttClient.connected()) {
    connected_ = false;
    Serial.println(F("MQTT: disconnected"));
  }
}

void MqttClient::publishQueuedEvents() {
  if (!connected_) return;

  ParkEvent e;
  while (queue_.peek(e)) {
    const char* typeStr = toString(e.type);
    int n = snprintf(jsonBuf, sizeof(jsonBuf),
                     "{\"t\":\"%s\",\"s\":%u,\"v\":%d,\"a\":%lu,\"q\":%lu}",
                     typeStr, e.sensorId, e.value,
                     static_cast<unsigned long>(e.atMs),
                     static_cast<unsigned long>(e.seq));
    if (n < 0 || static_cast<size_t>(n) >= sizeof(jsonBuf)) break;

    if (!mqttClient.publish(topicEvents_, jsonBuf, false)) {
      break;
    }
    queue_.pop(e);
  }
}

void MqttClient::publishHeartbeat(uint32_t nowMs) {
  if (!connected_) return;
  if (nowMs - lastHeartbeatMs_ < 30000) return;
  lastHeartbeatMs_ = nowMs;

  bool ok = mqttClient.publish(topicHeartbeat_, "alive", true);
  if (!ok) {
    Serial.println(F("MQTT: heartbeat publish failed"));
  }
}

void MqttClient::disconnect() {
  if (connected_) {
    mqttClient.disconnect();
    connected_ = false;
  }
}

} // namespace smartpark

