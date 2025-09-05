// Device information utilities (MAC-derived names)
#pragma once

#include <Arduino.h>

namespace DeviceInfo {

// Return uppercase MAC without colons, e.g., AABBCCDDEEFF
String macNoColonsUpper();

// Derive service name and POP from MAC
// serviceName = "HiveSync-" + last4 hex, pop = "Hive-" + last6 hex
void deriveNames(String &serviceNameOut, String &popOut);

} // namespace DeviceInfo

