// Configuration module implementation

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#define HS_LOG_PREFIX "CONF"
#include "debug.h"
#include "config.h"

// Factory defaults used only if config.json is missing

namespace {
  // In-memory config state
  struct ConfigState {
    int ds18b20Pin = -1;               // disabled by default
    unsigned long sampleIntervalMs = 900000UL; // 15 minutes
  } g_cfg;

  const char* kConfigPath = "/config.json";

  // Serialize current config to a JSON document
  void toJson(JsonDocument& doc) {
    doc["ds18b20_pin"] = g_cfg.ds18b20Pin;
    doc["sample_interval_ms"] = g_cfg.sampleIntervalMs;
  }

  // Load config fields from a JSON document (with validation)
  void fromJson(const JsonDocument& doc) {
    if (doc.containsKey("ds18b20_pin")) {
      g_cfg.ds18b20Pin = (int)doc["ds18b20_pin"].as<long>();
    }
    if (doc.containsKey("sample_interval_ms")) {
      unsigned long v = doc["sample_interval_ms"].as<unsigned long>();
      // Sanity clamp: 5s .. 24h
      if (v < 5000UL) v = 5000UL;
      if (v > 24UL * 60UL * 60UL * 1000UL) v = 24UL * 60UL * 60UL * 1000UL;
      g_cfg.sampleIntervalMs = v;
    }
  }
}

namespace Config {

bool begin() {
  // Mount LittleFS; format-on-fail for first-time boot convenience
  if (!LittleFS.begin(false)) {
    LOGLN("LittleFS mount failed");
    return false;
  }

  if (!LittleFS.exists(kConfigPath)) {
    LOGLN("config.json not found");
    return false;
  }

  return reload();
}

bool reload() {
  File f = LittleFS.open(kConfigPath, FILE_READ);
  if (!f) {
    LOGLN(String("Failed to open ") + kConfigPath);
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    LOGF("JSON parse error: %s\n", err.c_str());
    return false;
  }

  fromJson(doc);
  dumpToLog();
  return true;
}

int ds18b20Pin() { return g_cfg.ds18b20Pin; }

unsigned long sampleIntervalMs() { return g_cfg.sampleIntervalMs; }

void setDs18b20Pin(int pin) { g_cfg.ds18b20Pin = pin; }

void setSampleIntervalMs(unsigned long ms) { g_cfg.sampleIntervalMs = ms; }

void dumpToLog() {
  LOGF("Config: ds18b20_pin=%d, sample_interval_ms=%lu (%.1f min)\n",
       g_cfg.ds18b20Pin,
       (unsigned long)g_cfg.sampleIntervalMs,
       (double)g_cfg.sampleIntervalMs / 60000.0);
}

} // namespace Config
