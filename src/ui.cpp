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
static const uint16_t COLOR_SIGNAL_BLUE = 0x4C9C;   // #4A90E2 in RGB565
static const uint16_t COLOR_WHITE_SMOKE = 0xF7BE;   // #F5F5F5 in RGB565
static const uint16_t COLOR_BG = ST77XX_BLACK;
static const uint16_t COLOR_FG = COLOR_WHITE_SMOKE;
static const int TEXT_SIZE = 2;
static const int LINE_HEIGHT = 8 * TEXT_SIZE + 2; // GFX default font is 6x8

// Cached battery percent for status bar
static int s_battPercent = -1;

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
  uint16_t iconColor = connected ? COLOR_SIGNAL_BLUE : COLOR_WHITE_SMOKE;
  int16_t scr_w = tft.width();
#if __has_include("fa_wifi_icon.h")
  const int16_t iw = FA_WIFI_ICON_WIDTH;
  const int16_t ih = FA_WIFI_ICON_HEIGHT;
  int16_t x = scr_w - iw - 4;
  int16_t y = 4;
  drawMonoBitmap1BPP(x, y, iw, ih, FA_WIFI_ICON_BITMAP, iconColor, COLOR_BG);
#else
  const int16_t iw = 16, ih = 12;
  int16_t x = scr_w - iw - 4;
  int16_t y = 4;
  drawMonoBitmap16x12(x, y, WIFI_ICON_16x12, iconColor, COLOR_BG);
#endif
  drawBatteryTextOnly(x, iw);
}

void clearContentBelowHeader() {
  int16_t y0 = LINE_HEIGHT + 1;
  tft.fillRect(0, y0, tft.width(), tft.height() - y0, COLOR_BG);
}

void printHeader() {
  tft.fillScreen(COLOR_BG);
  tft.setTextWrap(false);
  tft.setTextSize(TEXT_SIZE);
  tft.setTextColor(COLOR_FG);
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
  tft.setRotation(1);      // landscape
  printHeader();
  drawWifiIcon(false);
}

void setBatteryPercent(int percent) {
  if (percent < 0) percent = -1;
  if (percent > 100) percent = 100;
  if (percent == s_battPercent) return; // no change
  s_battPercent = percent;
  // Draw only the battery text area without touching the Wi-Fi icon
  int16_t scr_w = tft.width();
#if __has_include("fa_wifi_icon.h")
  const int16_t iw = FA_WIFI_ICON_WIDTH;
#else
  const int16_t iw = 16;
#endif
  int16_t iconX = scr_w - iw - 4;
  drawBatteryTextOnly(iconX, iw);
}

static void drawBatteryTextOnly(int16_t iconX, int16_t iconW) {
  // Draw battery percentage text to the left of the Wi-Fi icon
  String txt;
  if (s_battPercent >= 0) {
    txt = String(s_battPercent) + '%';
  } else {
    txt = F("");
  }

  int16_t scr_w = tft.width();
  int16_t charW = 6 * TEXT_SIZE; // default 6x8 font width
  int16_t txtW = txt.length() * charW;
  int16_t spacing = 4; // pixels between text and icon
  int16_t textY = 2;   // top margin similar to header text
  int16_t textX = scr_w - (/*icon*/ iconW + 4) - spacing - txtW;

  // Clear the area where the text goes (a bit taller than the font)
  int16_t clearW = (txtW > (charW * 3)) ? txtW : (charW * 3); // clear reasonable area for 0..100%
  int16_t clearX = scr_w - (iconW + 4) - spacing - clearW;
  tft.fillRect(clearX, 0, clearW, LINE_HEIGHT, COLOR_BG);

  if (txt.length()) {
    tft.setFont(nullptr);
    tft.setTextSize(TEXT_SIZE);
    tft.setTextColor(COLOR_FG);
    tft.setCursor(textX, textY);
    tft.print(txt);
  }
}

} // namespace UI
