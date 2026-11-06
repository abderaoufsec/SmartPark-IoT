#pragma once
/**
 * SmartPark-IoT V2 — persisted occupancy record (FR-007, M1B).
 *
 * Pure serialization/validation logic: no Arduino, no NVS, no allocation —
 * the byte format is host-testable (pio test -e native); the ESP32 glue
 * (src/nvs_store.*) only moves bytes in and out of flash.
 *
 * Design rules:
 *  - VERSIONED record: a format change invalidates old records instead of
 *    misreading them (capacity is embedded for the same reason: a count is
 *    only meaningful against the CAPACITY it was recorded under).
 *  - Integrity: CRC-32 over the header bytes. A torn flash write, bit rot
 *    or a foreign blob fails validation and the boot stays UNKNOWN — never
 *    guess (NFR-004: no invented free spaces).
 *  - Fail-closed decode: every rejection reason is distinct for diagnostics
 *    and tests; decode() never returns a count unless every check passed.
 *
 * Record layout (16 bytes, little-endian, explicitly serialized — no struct
 * padding / endianness assumptions):
 *   [0..3]   magic  'S','P','R','K'
 *   [4..5]   format version (uint16)
 *   [6..7]   capacity the count was recorded against (uint16)
 *   [8]      occupancy count 0..CAPACITY
 *   [9..11]  reserved, must be 0
 *   [12..15] CRC-32 (IEEE 802.3, reflected) over bytes [0..11]
 */
#include <stddef.h>
#include <stdint.h>
#include "config.hpp"

namespace smartpark {
namespace persistence {

/** Serialized record size in bytes. */
constexpr size_t OCC_RECORD_SIZE = 16;

/** Bump when the layout changes; older records then fail validation. */
constexpr uint16_t OCC_RECORD_VERSION = 1;

/** IEEE CRC-32 (polynomial 0xEDB88320, reflected), exposed for tests. */
uint32_t crc32(const uint8_t* data, size_t len);

enum class RecordStatus : uint8_t {
  OK = 0,
  NULLPTR,       // caller passed a null buffer
  BAD_LENGTH,    // not exactly OCC_RECORD_SIZE (truncated / extended)
  BAD_MAGIC,     // not a SmartPark record at all
  BAD_VERSION,   // written by a different firmware format
  BAD_CAPACITY,  // CAPACITY changed since the record was written
  BAD_COUNT,     // count outside [0, CAPACITY]
  BAD_RESERVED,  // reserved bytes not zero (foreign/torn data)
  BAD_CRC,       // integrity check failed (torn write / corruption)
};

inline const char* toString(RecordStatus s) {
  switch (s) {
    case RecordStatus::OK:           return "OK";
    case RecordStatus::NULLPTR:      return "NULLPTR";
    case RecordStatus::BAD_LENGTH:   return "BAD_LENGTH";
    case RecordStatus::BAD_MAGIC:    return "BAD_MAGIC";
    case RecordStatus::BAD_VERSION:  return "BAD_VERSION";
    case RecordStatus::BAD_CAPACITY: return "BAD_CAPACITY";
    case RecordStatus::BAD_COUNT:    return "BAD_COUNT";
    case RecordStatus::BAD_RESERVED: return "BAD_RESERVED";
    case RecordStatus::BAD_CRC:      return "BAD_CRC";
  }
  return "INVALID_STATUS";
}

/**
 * Serialize `count` into `out` (OCC_RECORD_SIZE bytes).
 * @return bytes written, or 0 when out is null, cap too small, or count is
 *         out of range (an invalid count is never written).
 */
size_t encodeOccupancy(uint8_t count, uint8_t* out, size_t cap);

/**
 * Validate `data` and extract the count.
 * Fail-closed: `countOut` is written ONLY on RecordStatus::OK.
 * @return OK or the specific rejection reason (for diagnostics/tests).
 */
RecordStatus decodeOccupancy(const uint8_t* data, size_t len,
                             uint8_t& countOut);

} // namespace persistence
} // namespace smartpark
