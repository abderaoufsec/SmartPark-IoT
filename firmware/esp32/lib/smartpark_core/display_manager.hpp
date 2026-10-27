#pragma once
/**
 * SmartPark-IoT V2 — 16x2 LCD line formatting (pure, native-testable).
 *
 * Fixes V1 finding F-07: V1 printed fixed strings + numbers that could
 * overflow the 16-character rows and corrupt adjacent data. Every line here
 * is produced with snprintf into a 17-byte buffer and then padded/truncated
 * to EXACTLY 16 visible characters + NUL (never more, never less).
 *
 * Layout (provisional wording, tuned for readability over 16 cols):
 *   line1 priority: GATE FAULT > approach alerts > default banner
 *     "SMARTPARK V2"     idle
 *     "CAR AT ENTRY"     entry lane approach (advisory, never a count)
 *     "CAR AT EXIT"      exit lane approach
 *     "ENTRY: FULL STOP" approach while admission denied (V1 F-09 fix:
 *                        the refusal is now *visible*)
 *   line2 occupancy/gate:
 *     "OCC:??? NO ENTRY"   occupancy unknown (boot w/o persistence, FR-004)
 *     "F:n A:x B:x G:xx"   known count + zone states + gate code
 *     "F:0 PARK FULL"      capacity reached (padded)
 *
 * Zone chars: F=FREE, O=OCCUPIED, U=UNKNOWN, X=FAULT.
 * Gate codes: CL closed, oP opening, OP open, cL closing, BL blocked,
 *             FA fault, MN manual.
 */
#include <stddef.h>
#include "parking_controller.hpp"

namespace smartpark {

struct DisplayLines {
  char line1[17];
  char line2[17];
};

/** Pad (or truncate) `buf` to exactly 16 chars; NUL at index 16. */
void padLine16(char* buf);

/** Single-char abbreviation for a sensor state. */
char stateChar(SensorState s);

/** Two-char barrier abbreviation (see file header). */
const char* gateCode(BarrierState s);

/** Format both LCD rows for the current controller view. Never overflows. */
DisplayLines formatDisplay(const ControllerView& v);

} // namespace smartpark
