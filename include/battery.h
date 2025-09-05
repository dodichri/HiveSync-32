// Battery fuel gauge (MAX17048/49) module
#pragma once

#include <Arduino.h>

namespace Battery {

// Initialize I2C and fuel gauge. Returns true if detected.
bool begin();

// Poll the gauge periodically. Internally rate-limited.
void update();

// Last known SoC percent [0..100], or -1 if unknown.
int percent();

} // namespace Battery

