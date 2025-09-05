// Periodic GitHub firmware update check
#pragma once

#include <Arduino.h>

namespace UpdateChecker {

// Start a background task that checks GitHub for a newer release
// Compares latest release tag_name against FW_VERSION and logs to Serial.
// Configure repository via build flags: GITHUB_OWNER, GITHUB_REPO, FW_VERSION
void begin();

} // namespace UpdateChecker

