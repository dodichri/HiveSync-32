// Persist firmware version to ESP32 NVS (Arduino Preferences)
#pragma once

#include <Arduino.h>

namespace VersionStore {

// Return version string stored in NVS (empty if none yet)
String storedFwVersion();

// Save current FW_VERSION to NVS if changed
void saveCurrentFwVersion();

} // namespace VersionStore

