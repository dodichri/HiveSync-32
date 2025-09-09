// Configuration module: load/save runtime settings from LittleFS config.json
#pragma once

#include <Arduino.h>

namespace Config {

// Initialize filesystem and load configuration from /config.json.
// Creates file with defaults if missing. Returns true if config loaded.
bool begin();

// Reload configuration from disk. Returns true on success; keeps old values on failure.
bool reload();

// Accessors for sensor configuration
int ds18b20Pin();
unsigned long sampleIntervalMs();

// Mutators update in-memory value; call save() to persist.
void setDs18b20Pin(int pin);
void setSampleIntervalMs(unsigned long ms);

// Debug helper to print current config via Serial (honors debug macros)
void dumpToLog();

} // namespace Config

