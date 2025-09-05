// Persist firmware version to ESP32 NVS (Arduino Preferences)

#include <Arduino.h>
#include <Preferences.h>

#ifndef FW_VERSION
#define FW_VERSION "0.0.0"
#endif

namespace VersionStore {

static const char *kNs = "hivesync"; // NVS namespace
static const char *kKey = "fw";      // key for firmware version

String storedFwVersion() {
  Preferences prefs;
  if (!prefs.begin(kNs, true)) {
    return String();
  }
  String v = prefs.getString(kKey, "");
  prefs.end();
  return v;
}

void saveCurrentFwVersion() {
  const String current = String(FW_VERSION);
  // Avoid unnecessary writes
  String prev = storedFwVersion();
  if (prev == current) return;

  Preferences prefs;
  if (!prefs.begin(kNs, false)) {
    Serial.println(F("[VersionStore] NVS begin failed"));
    return;
  }
  prefs.putString(kKey, current);
  prefs.end();
  Serial.print(F("[VersionStore] Stored FW version: "));
  Serial.println(current);
}

} // namespace VersionStore

