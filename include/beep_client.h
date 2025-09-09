// Beep API client for uploading sensor data
#pragma once

#include <Arduino.h>

namespace BeepClient {

// Returns true if all required config values are non-empty
bool isConfigured();

// Ensure system time is synchronized via NTP (UTC).
// Returns true if epoch time appears valid.
bool ensureTimeSynced(uint32_t timeoutMs = 7000);

// Key/value pair for sensor readings (e.g., {"t_i", 23.45}).
struct KV {
  const char* key; // value key as expected by API (e.g., "t_i")
  float value;     // numeric value
};

// Upload a set of sensor readings in a single request.
// - items: array of key/value sensor readings
// - count: number of items in array
// - sampleMillis: millis() when the readings were taken (same timestamp for all)
// Returns true on success; 'err' filled on failure.
bool uploadReadings(const KV* items, size_t count, uint32_t sampleMillis, String &err);

// Update the device's firmware version on BEEP.nl (via /api/devices).
// Uses credentials and device key from /config.json.
// Returns true on success; 'err' filled on failure.
bool updateFirmwareVersion(const char* version, String &err);

} // namespace BeepClient
