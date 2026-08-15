#pragma once
#include <stdint.h>

namespace coreinit_nx {

// Wii U: bus a 248.625 MHz (non 250 tondi), timer = bus/4
constexpr uint32_t kWiiUBusClockHz  = 248625000u;
constexpr uint32_t kWiiUCoreClockHz = 1243125000u;   // Espresso, 5x bus
constexpr uint64_t kWiiUTimerHz     = kWiiUBusClockHz / 4;  // 62156250

// Secondi fra 1970-01-01 (Unix) e 2000-01-01 (Cafe OS)
constexpr int64_t kUnixToCafeEpoch = 946684800;

int64_t systemTimeTicks();
int64_t computeBaseTime();

} // namespace coreinit_nx