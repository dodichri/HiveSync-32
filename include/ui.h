// UI module for HiveSync (ESP32-S3 Reverse TFT)
#pragma once

#include <Arduino.h>

namespace UI {

// Font style options for printLine
enum class FontStyle {
  Default,
  RoundedSans,
  CleanSans
};

// Initialize display, power rails, backlight, header, and initial WiFi icon
void init();

// Draw WiFi icon in top-right with state-specific color
void drawWifiIcon(bool connected);

// Clear everything except the header band
void clearContentBelowHeader();

// Draw the application header
void printHeader();

// Print a message in a 1-based line slot under the header
// Default color is 0xF7BE (White Smoke) to match existing style
void printLine(int lineIndex1Based, const String &msg, uint16_t color = 0xF7BE, FontStyle style = FontStyle::Default);

} // namespace UI

