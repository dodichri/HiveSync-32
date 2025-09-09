// UI module for HiveSync (ESP32-S3 Reverse TFT)
#pragma once

#include <Arduino.h>

namespace UI {

// Colors and layout
static const uint16_t COLOR_HIVE_YELLOW = 0xFDA0;   // #FFB400 in RGB565
static const uint16_t COLOR_SIGNAL_BLUE = 0x4C9C;   // #4A90E2 in RGB565
static const uint16_t COLOR_WHITE_SMOKE = 0xF7BE;   // #F5F5F5 in RGB565
static const uint16_t COLOR_DEEP_TEAL   = 0x03F2;   // #007C91 in RGB565
static const uint16_t COLOR_BG = ST77XX_BLACK;
static const int TEXT_SIZE = 2;
static const int LINE_HEIGHT = 8 * TEXT_SIZE + 2; // GFX default font is 6x8

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

// Update the battery percent to be shown next to the Wi-Fi icon
// Pass -1 to hide.
void setBatteryPercent(int percent);

// Clear everything except the header band
void clearContentBelowHeader();

// Draw the application header
void printHeader();

// Print a message in a 1-based line slot under the header
// Default color is 0xF7BE (White Smoke) to match existing style
void printLine(int lineIndex1Based, const String &msg, uint16_t color = 0xF7BE, FontStyle style = FontStyle::Default);

// Overload for flash-resident literals: UI::printLine(3, F("text"))
void printLine(int lineIndex1Based, const __FlashStringHelper* msg, uint16_t color = 0xF7BE, FontStyle style = FontStyle::Default);

// Turn off backlight and I2C/display power rails to save energy before deep sleep
void powerDown();

} // namespace UI
