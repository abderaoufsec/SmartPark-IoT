#include "persistence.hpp"

namespace smartpark {
namespace persistence {

namespace {
constexpr uint8_t MAGIC[4] = {0x53, 0x50, 0x52, 0x4B}; // 'S','P','R','K'

void putU16Le(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}
void putU32Le(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}
uint16_t getU16Le(const uint8_t* p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                               static_cast<uint16_t>(p[1]) << 8);
}
uint32_t getU32Le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
} // namespace

uint32_t crc32(const uint8_t* data, size_t len) {
  // Bitwise (table-free) IEEE CRC-32: ~8 lines, no lookup tables in RAM,
  // runs once per boot/save on a few bytes — speed is irrelevant here.
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc ^ 0xFFFFFFFFu;
}

size_t encodeOccupancy(uint8_t count, uint8_t* out, size_t cap) {
  if (out == nullptr || cap < OCC_RECORD_SIZE) return 0;
  if (count > static_cast<uint8_t>(CAPACITY)) return 0; // never write garbage

  for (size_t i = 0; i < OCC_RECORD_SIZE; ++i) out[i] = 0;
  for (size_t i = 0; i < 4; ++i) out[i] = MAGIC[i];
  putU16Le(out + 4, OCC_RECORD_VERSION);
  putU16Le(out + 6, static_cast<uint16_t>(CAPACITY));
  out[8] = count;
  // [9..11] stay 0 (reserved)
  putU32Le(out + 12, crc32(out, 12));
  return OCC_RECORD_SIZE;
}

RecordStatus decodeOccupancy(const uint8_t* data, size_t len,
                             uint8_t& countOut) {
  if (data == nullptr) return RecordStatus::NULLPTR;
  if (len != OCC_RECORD_SIZE) return RecordStatus::BAD_LENGTH;
  for (size_t i = 0; i < 4; ++i)
    if (data[i] != MAGIC[i]) return RecordStatus::BAD_MAGIC;
  if (getU16Le(data + 4) != OCC_RECORD_VERSION)
    return RecordStatus::BAD_VERSION;
  if (getU16Le(data + 6) != static_cast<uint16_t>(CAPACITY))
    return RecordStatus::BAD_CAPACITY; // count semantics would differ
  const uint8_t count = data[8];
  if (count > static_cast<uint8_t>(CAPACITY)) return RecordStatus::BAD_COUNT;
  if (data[9] != 0 || data[10] != 0 || data[11] != 0)
    return RecordStatus::BAD_RESERVED;
  if (getU32Le(data + 12) != crc32(data, 12)) return RecordStatus::BAD_CRC;
  countOut = count; // written only after every check passed
  return RecordStatus::OK;
}

} // namespace persistence
} // namespace smartpark
