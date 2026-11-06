#pragma once
/**
 * SmartPark-IoT V2 — NVS blob storage for the occupancy record (M1B, FR-007).
 *
 * Thin Arduino-Preferences wrapper: moves bytes, nothing else. All format
 * and validation logic lives in lib/smartpark_core/persistence.* where it is
 * host-tested; this file is deliberately too small to hide a bug in.
 *
 * Flash-wear policy (NFR-004 / FR-007 "avoid unnecessary writes"): the
 * caller only calls save() when the count actually changed — a stationary
 * count produces zero writes, and NVS wear-levelling spreads the rest.
 *
 * Crash consistency: Preferences::putBytes writes a whole value atomically
 * (NVS copy-on-write), so a power cut leaves either the old record or the
 * new one — both CRC-valid; a torn/corrupt record fails decode at boot and
 * occupancy stays UNKNOWN (never a guessed count). The count may be at most
 * one change stale after an unclean shutdown — documented in m1b.md.
 */
#include <Arduino.h>
#include <Preferences.h>

namespace smartpark {
namespace storage {

class NvsStore {
public:
  /** Open the namespace (read-write). False => caller must NOT restore. */
  bool begin() {
    open_ = prefs_.begin(ns(), false);
    return open_;
  }

  /**
   * Load the raw record into buf.
   * @param lenOut bytes actually read (0 when absent)
   * @return true when the namespace is open (lenOut==0 means "no record")
   */
  bool load(uint8_t* buf, size_t cap, size_t& lenOut) {
    lenOut = 0;
    if (!open_ || buf == nullptr || cap == 0) return false;
    const size_t have = prefs_.getBytesLength(key());
    if (have == 0 || have > cap) return true; // absent or too big: no restore
    lenOut = prefs_.getBytes(key(), buf, have);
    return true;
  }

  /** @return true when the whole blob was written. */
  bool save(const uint8_t* buf, size_t len) {
    if (!open_ || buf == nullptr || len == 0) return false;
    return prefs_.putBytes(key(), buf, len) == len;
  }

  void close() {
    if (open_) {
      prefs_.end();
      open_ = false;
    }
  }

private:
  // Function-scope statics: no in-class constexpr static member definitions
  // needed under the Arduino-ESP32 gnu++11 build.
  static const char* ns() { return "smartpark"; }
  static const char* key() { return "occ"; } // the single M1B record
  Preferences prefs_;
  bool open_ = false;
};

} // namespace storage
} // namespace smartpark
