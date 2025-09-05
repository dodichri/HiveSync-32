// Periodic GitHub firmware update check
#pragma once

#include <Arduino.h>

namespace UpdateChecker {

// Start a background task that checks GitHub for a newer release
// Compares latest release tag_name against FW_VERSION and logs to Serial.
// Configure repository via build flags: GITHUB_OWNER, GITHUB_REPO, FW_VERSION
void begin();

// High-level status of the last comparison
enum class Status {
  Unknown = 0,
  UpToDate,
  UpdateAvailable,
  LocalNewer,
  Error
};

// Return last known status (Unknown until first check completes)
Status status();

// Return the firmware version compiled into this build
String currentVersion();

// Return the last fetched latest version from GitHub (empty if unknown)
String latestVersion();

// Convenience: true if a newer version is available on GitHub
bool updateAvailable();

// Millis at which the last check finished (0 if never)
uint32_t lastCheckMillis();

} // namespace UpdateChecker
