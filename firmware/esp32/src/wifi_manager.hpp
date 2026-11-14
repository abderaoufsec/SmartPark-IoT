#pragma once
/**
 * SmartPark-IoT V2 — Wi-Fi connection manager (M2, ESP32 only).
 *
 * Non-blocking Wi-Fi connect/reconnect with exponential backoff. Local
 * operation (sensing, gate control) is entirely independent of Wi-Fi state
 * (NFR-001): network loss never affects local safety logic.
 *
 * Pure header (no Arduino includes). Implementation in wifi_manager.cpp.
 */
#include <cstdint>
#include <cstring>

namespace smartpark {

class WifiManager {
public:
  WifiManager();

  /** Configure SSID/passphrase. Call once from setup(). */
  void begin(const char* ssid, const char* pass);

  /** Non-blocking: call every loop(). Returns true when connected. */
  bool loop(uint32_t nowMs);

  /** Connected to AP? */
  bool connected() const { return connected_; }

  /** Last connection result (for diagnostics). */
  bool lastResult() const { return lastResult_; }

private:
  void connect(uint32_t nowMs);

  char ssid_[32];
  char pass_[64];
  bool connected_;
  bool lastResult_;
  uint32_t lastAttemptMs_;
  uint8_t backoff_;
};

} // namespace smartpark