// UI module implementation for HiveSync

#include <Arduino.h>
#include <SPI.h>
#include <pgmspace.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>

#if __has_include("fa_wifi_icon.h")
#include "fa_wifi_icon.h"
#endif

#include "ui.h"

// Board variant fallbacks (verified in Arduino variant headers for Adafruit ESP32-S3 Reverse TFT)
#ifndef TFT_CS
#define TFT_CS 42
#endif
#ifndef TFT_DC
#define TFT_DC 40
#endif
#ifndef TFT_RST
#define TFT_RST 41
#endif
#ifndef TFT_BACKLITE
#define TFT_BACKLITE 45
#endif
#ifndef TFT_I2C_POWER
#define TFT_I2C_POWER 7
#endif

namespace UI {

// TFT driver instance is module-local
static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

// Colors and layout
static const uint16_t COLOR_HIVE_YELLOW = 0xFDA0;   // #FFB400 in RGB565
static const uint16_t COLOR_SIGNAL_BLUE = 0x4C9C;   // #4A90E2 in RGB565
static const uint16_t COLOR_WHITE_SMOKE = 0xF7BE;   // #F5F5F5 in RGB565
static const uint16_t COLOR_DEEP_TEAL   = 0x03F2;   // #007C91 in RGB565
static const uint16_t COLOR_BG = ST77XX_BLACK;
static const int TEXT_SIZE = 2;
static const int LINE_HEIGHT = 8 * TEXT_SIZE + 2; // GFX default font is 6x8

// Cached battery percent for status bar and Wi-Fi state
static int s_battPercent = -1;
static bool s_wifiConnected = false;

// Fallback simple 16x12 monochrome bitmap (approximation) if FA icon not generated
static const uint16_t WIFI_ICON_16x12[] PROGMEM = {
  0b0000011111100000,
  0b0001111111111000,
  0b0011111111111100,
  0b0111110000111110,
  0b1111000000001111,
  0b1110001111000111,
  0b1100011111110011,
  0b0000111111110000,
  0b0000011111100000,
  0b0000001111000000,
  0b0000000110000000,
  0b0000000000000000,
};

// Generic 1bpp bitmap drawer (MSB-first per byte)
static void drawMonoBitmap1BPP(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *data, uint16_t fg, uint16_t bg) {
  int bytesPerRow = (w + 7) / 8;
  for (int16_t row = 0; row < h; ++row) {
    for (int16_t col = 0; col < w; ++col) {
      int byteIndex = row * bytesPerRow + (col / 8);
      uint8_t bits = pgm_read_byte(&data[byteIndex]);
      bool on = bits & (0x80 >> (col % 8));
      tft.drawPixel(x + col, y + row, on ? fg : bg);
    }
  }
}

// Helper: draw 1-bit bitmap where each row is a 16-bit word (fallback 16x12)
static void drawMonoBitmap16x12(int16_t x, int16_t y, const uint16_t *data, uint16_t color, uint16_t bg) {
  for (int16_t row = 0; row < 12; ++row) {
    uint16_t bits = pgm_read_word(&data[row]);
    for (int16_t col = 0; col < 16; ++col) {
      bool on = bits & (1 << (15 - col));
      tft.drawPixel(x + col, y + row, on ? color : bg);
    }
  }
}

static inline const GFXfont* fontForStyle(FontStyle style) {
  switch (style) {
    case FontStyle::RoundedSans: return &FreeSansBold9pt7b;
    case FontStyle::CleanSans:   return &FreeSans9pt7b;
    case FontStyle::Default:
    default:                     return nullptr; // built-in font
  }
}

// Forward declaration
static void drawBatteryTextOnly(int16_t iconX, int16_t iconW);

void drawWifiIcon(bool connected) {
  // Remember connection state so battery updates can reposition the icon
  s_wifiConnected = connected;

  // Colors and measurements
  uint16_t iconColor = connected ? COLOR_SIGNAL_BLUE : COLOR_WHITE_SMOKE;
  const int16_t scr_w = tft.width();
  const int16_t rightMargin = 6;   // margin to right edge
  const int16_t spacing = 8;       // space between Wi-Fi icon and battery SoC
  const int16_t textY = 2;         // top margin similar to header text
  const int16_t charW = 6 * TEXT_SIZE; // default 6x8 font width scaled

  // Determine icon dimensions
#if __has_include("fa_wifi_icon.h")
  const int16_t iw = FA_WIFI_ICON_WIDTH;
  const int16_t ih = FA_WIFI_ICON_HEIGHT;
#else
  const int16_t iw = 16, ih = 12;
#endif

  // Compose battery text and compute width
  String txt;
  if (s_battPercent >= 0) txt = String(s_battPercent) + '%';
  const int16_t txtW = txt.length() * charW;

  // Clear a safe region on the right where icon + max text may appear
  const int16_t maxChars = 4; // up to "100%"
  const int16_t clearW = iw + spacing + maxChars * charW + rightMargin + 2;
  const int16_t clearX = scr_w - clearW;
  tft.fillRect(clearX, 0, clearW, LINE_HEIGHT, COLOR_BG);

  // Layout: Wi-Fi icon BEFORE battery text (left-to-right)
  // Text is right-aligned to the margin; icon sits to its left.
  int16_t textX = scr_w - rightMargin - txtW;
  int16_t iconX = textX - (txtW > 0 ? spacing : 0) - iw;

  // Draw icon
  const int16_t iconY = -2; // align top with battery SoC text
#if __has_include("fa_wifi_icon.h")
  drawMonoBitmap1BPP(iconX, iconY, iw, ih, FA_WIFI_ICON_BITMAP, iconColor, COLOR_BG);
#else
  drawMonoBitmap16x12(iconX, iconY, WIFI_ICON_16x12, iconColor, COLOR_BG);
#endif

  // Draw text (if available)
  if (txt.length()) {
    tft.setFont(nullptr);
    tft.setTextSize(TEXT_SIZE);
    tft.setTextColor(COLOR_WHITE_SMOKE);
    tft.setCursor(textX, textY);
    tft.print(txt);
  }
}

void clearContentBelowHeader() {
  int16_t y0 = LINE_HEIGHT + 1;
  tft.fillRect(0, y0, tft.width(), tft.height() - y0, COLOR_BG);
}

void printHeader() {
  tft.fillScreen(COLOR_BG);
  tft.setTextWrap(false);
  tft.setTextSize(TEXT_SIZE);
  tft.setTextColor(COLOR_HIVE_YELLOW);
  tft.setCursor(2, 2);
  tft.print("HiveSync");
}

void printLine(int lineIndex1Based, const String &msg, uint16_t color, FontStyle style) {
  const GFXfont* chosen = fontForStyle(style);

  int16_t yTop = (lineIndex1Based - 1) * LINE_HEIGHT + 2;
  tft.fillRect(0, yTop, tft.width(), LINE_HEIGHT, COLOR_BG);
  tft.setTextColor(color);

  if (chosen) {
    tft.setFont(chosen);
    tft.setTextSize(1);
    uint8_t yAdvance = chosen->yAdvance;
    int16_t adv = (int16_t)yAdvance;
    int16_t baseline = yTop + ((adv - 2 < (LINE_HEIGHT - 2)) ? (adv - 2) : (LINE_HEIGHT - 2));
    tft.setCursor(2, baseline);
  } else {
    tft.setFont(nullptr);
    tft.setTextSize(TEXT_SIZE);
    tft.setCursor(2, yTop);
  }

  tft.print(msg);

  // Restore defaults
  tft.setFont(nullptr);
  tft.setTextSize(TEXT_SIZE);
}

void init() {
  // Power up display / I2C rail and backlight
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  delay(10);
  tft.init(135, 240);      // ST7789 240x135
  tft.setRotation(3);      // landscape
  printHeader();
  drawWifiIcon(false);
}

void setBatteryPercent(int percent) {
  if (percent < 0) percent = -1;
  if (percent > 100) percent = 100;
  if (percent == s_battPercent) return; // no change
  s_battPercent = percent;
  // Re-draw the Wi-Fi icon and SoC together so spacing/order stay correct
  drawWifiIcon(s_wifiConnected);
}

static void drawBatteryTextOnly(int16_t, int16_t) {
  // Deprecated: retained for linkage but not used; drawing now handled in drawWifiIcon().
}

} // namespace UI
