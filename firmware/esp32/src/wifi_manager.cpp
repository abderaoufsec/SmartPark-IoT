#include <Arduino.h>
#include <WiFi.h>

#include "wifi_manager.hpp"

namespace smartpark {

WifiManager::WifiManager()
    : connected_(false), lastResult_(false), lastAttemptMs_(0), backoff_(0) {
  ssid_[0] = '\0';
  pass_[0] = '\0';
}

void WifiManager::begin(const char* ssid, const char* pass) {
  strncpy(ssid_, ssid ? ssid : "", sizeof(ssid_) - 1);
  ssid_[sizeof(ssid_) - 1] = '\0';
  strncpy(pass_, pass ? pass : "", sizeof(pass_) - 1);
  pass_[sizeof(pass_) - 1] = '\0';
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_, pass_);
  Serial.print(F("Wi-Fi: connecting"));
}

void WifiManager::connect(uint32_t nowMs) {
  if (ssid_[0] == '\0') return; // nothing configured
  WiFi.begin(ssid_, pass_);
  lastAttemptMs_ = nowMs;
  backoff_ = (backoff_ < 8) ? static_cast<uint8_t>(backoff_ + 1) : 8u;
  Serial.print(F("."));
}

bool WifiManager::loop(uint32_t nowMs) {
  if (ssid_[0] == '\0') return false;

  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (!connected_) {
      connected_ = true;
      lastResult_ = true;
      Serial.println();
      Serial.print(F("Wi-Fi: connected, IP="));
      Serial.println(WiFi.localIP());
    }
    return true;
  }

  // Not connected: retry with backoff
  uint32_t interval = 2000u << backoff_; // 2s, 4s, 8s... capped
  if (nowMs - lastAttemptMs_ >= interval) {
    connect(nowMs);
  }
  connected_ = false;
  lastResult_ = false;
  return false;
}

} // namespace smartpark
