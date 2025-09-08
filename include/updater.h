// GitHub OTA Updater for ESP32 (Arduino)
#pragma once

#include <Arduino.h>

namespace Updater {

// Call regularly from loop(); performs a one-time check after WiFi connects.
void loop();

// Expose the current firmware version string (from build flag) for display/logs.
const char* currentVersion();

// True once the firmware check/update process has completed (this boot).
// Useful for sequencing other subsystems after OTA check.
bool checkCompleted();

} // namespace Updater
