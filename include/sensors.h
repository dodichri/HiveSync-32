// Sensors module: periodic reads every N minutes
#pragma once

#include <Arduino.h>

namespace Sensors {

// Initialize any configured sensors (e.g., DS18B20)
void begin();

// Call regularly from loop(); performs rate-limited sampling
void loop();

// DS18B20 helpers (valid only if present)
bool ds18b20Available();
float ds18b20LastTempC();
uint32_t lastSampleMillis();

} // namespace Sensors

