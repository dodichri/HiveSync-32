// Device information utilities implementation

#include <Arduino.h>
#include <WiFi.h>

#include "device_info.h"

namespace DeviceInfo {

String macNoColonsUpper() {
  String mac = WiFi.macAddress(); // format: AA:BB:CC:DD:EE:FF
  String out;
  out.reserve(12);
  for (size_t i = 0; i < mac.length(); i++) {
    if (mac[i] != ':') out += (char)toupper((int)mac[i]);
  }
  return out; // 12 hex chars
}

void deriveNames(String &serviceNameOut, String &popOut) {
  String mac = macNoColonsUpper();
  if (mac.length() != 12) {
    mac = F("000000000000");
  }
  String last4 = mac.substring(8);
  String last6 = mac.substring(6);
  serviceNameOut = String(F("HiveSync-")) + last4;
  popOut = String(F("Hive-")) + last6;
}

} // namespace DeviceInfo

