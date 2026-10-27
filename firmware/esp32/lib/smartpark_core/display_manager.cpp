#include "display_manager.hpp"
#include <stdio.h>
#include <string.h>

namespace smartpark {

void padLine16(char* buf) {
  // buf is a 17-byte buffer. Truncate/pad to exactly 16 visible chars.
  size_t len = strlen(buf);
  if (len > 16) {
    buf[16] = '\0';
    len = 16;
  }
  for (size_t i = len; i < 16; ++i) buf[i] = ' ';
  buf[16] = '\0';
}

char stateChar(SensorState s) {
  switch (s) {
    case SensorState::VALID_FREE:     return 'F';
    case SensorState::VALID_OCCUPIED: return 'O';
    case SensorState::INIT:
    case SensorState::UNKNOWN:        return 'U';
    case SensorState::FAULT:          return 'X';
  }
  return '?';
}

const char* gateCode(BarrierState s) {
  switch (s) {
    case BarrierState::CLOSED:  return "CL";
    case BarrierState::OPENING: return "oP";
    case BarrierState::OPEN:    return "OP";
    case BarrierState::CLOSING: return "cL";
    case BarrierState::BLOCKED: return "BL";
    case BarrierState::FAULT:   return "FA";
    case BarrierState::MANUAL:  return "MN";
  }
  return "??";
}

DisplayLines formatDisplay(const ControllerView& v) {
  DisplayLines out;
  memset(&out, 0, sizeof(out));

  // ---- line 1: alerts win over the default banner ----
  if (v.gate == BarrierState::FAULT) {
    snprintf(out.line1, sizeof(out.line1), "GATE FAULT");
  } else if (v.entryApproach && !v.admissionAllowed) {
    snprintf(out.line1, sizeof(out.line1), "ENTRY: FULL STOP");
  } else if (v.entryApproach) {
    snprintf(out.line1, sizeof(out.line1), "CAR AT ENTRY");
  } else if (v.exitApproach) {
    snprintf(out.line1, sizeof(out.line1), "CAR AT EXIT");
  } else {
    snprintf(out.line1, sizeof(out.line1), "SMARTPARK V2");
  }
  padLine16(out.line1);

  // ---- line 2: occupancy + zones + gate ----
  if (v.occStatus == OccupancyStatus::UNKNOWN) {
    snprintf(out.line2, sizeof(out.line2), "OCC:??? NO ENTRY");
  } else if (v.freeSpaces == 0) {
    snprintf(out.line2, sizeof(out.line2), "F:0 PARK FULL");
  } else {
    snprintf(out.line2, sizeof(out.line2), "F:%u A:%c B:%c G:%s",
             static_cast<unsigned>(v.freeSpaces), stateChar(v.zone[0]),
             stateChar(v.zone[1]), gateCode(v.gate));
  }
  padLine16(out.line2);

  return out;
}

} // namespace smartpark
