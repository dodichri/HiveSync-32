// GitHub OTA Updater for ESP32 (Arduino)
#pragma once

#include <Arduino.h>

namespace Updater {

// Call regularly from loop(); performs a one-time check after WiFi connects.
void loop();

// Expose the current firmware version string (from build flag) for display/logs.
const char* currentVersion();

} // namespace Updater

