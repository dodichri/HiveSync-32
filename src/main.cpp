// HiveSync for Adafruit Feather ESP32-S3 Reverse TFT
// - BLE Wi-Fi provisioning with POP and device name derived from MAC
// - Always shows app name
// - Shows device name and POP when not provisioned
// - Long-press BOOT (GPIO0) at boot to clear credentials
// - Shows Wi-Fi icon (top-right) when connected

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiProv.h>
// For PROGMEM access helpers
#include <pgmspace.h>

#if __has_include("fa_wifi_icon.h")
#include "fa_wifi_icon.h"
#endif

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
// Optional GFX fonts for style selection
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>

// Board variant defines for the Reverse TFT (verified in variant headers)
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

// BOOT button on ESP32-S3 is GPIO0; use as reset-provisioning long-press at boot
#ifndef RESET_BUTTON_PIN
#define RESET_BUTTON_PIN 0
#endif

// TFT driver instance
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Custom icon colors per request
static const uint16_t COLOR_SIGNAL_BLUE = 0x4C9C;   // Signal Blue #4A90E2 (RGB565)
static const uint16_t COLOR_WHITE_SMOKE = 0xF7BE; // White Smoke #F5F5F5 (RGB565)

// UI layout
static const uint16_t COLOR_BG = ST77XX_BLACK;
static const uint16_t COLOR_FG = COLOR_WHITE_SMOKE;
static const int TEXT_SIZE = 2;            // GFX default font size multiplier
static const int LINE_HEIGHT = 8 * TEXT_SIZE + 2; // default font is 6x8; use 2px spacing

// State
static volatile bool g_connected = false;
static String g_serviceName;  // HiveSync-<last4>
static String g_pop;          // Hive-<last6>

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
void drawMonoBitmap1BPP(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *data, uint16_t fg, uint16_t bg) {
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
void drawMonoBitmap16x12(int16_t x, int16_t y, const uint16_t *data, uint16_t color, uint16_t bg) {
  for (int16_t row = 0; row < 12; ++row) {
    uint16_t bits = pgm_read_word(&data[row]);
    for (int16_t col = 0; col < 16; ++col) {
      bool on = bits & (1 << (15 - col));
      tft.drawPixel(x + col, y + row, on ? color : bg);
    }
  }
}

void drawWifiIcon(bool connected) {
  // Top-right corner with 4px margin
  uint16_t iconColor = connected ? COLOR_SIGNAL_BLUE : COLOR_WHITE_SMOKE;
  int16_t scr_w = tft.width();
#if __has_include("fa_wifi_icon.h")
  const int16_t iw = FA_WIFI_ICON_WIDTH;
  const int16_t ih = FA_WIFI_ICON_HEIGHT;
  int16_t x = scr_w - iw - 4;
  int16_t y = 4;  // below top margin
  drawMonoBitmap1BPP(x, y, iw, ih, FA_WIFI_ICON_BITMAP, iconColor, COLOR_BG);
#else
  const int16_t iw = 16, ih = 12;
  int16_t x = scr_w - iw - 4;
  int16_t y = 4;  // below top margin
  drawMonoBitmap16x12(x, y, WIFI_ICON_16x12, iconColor, COLOR_BG);
#endif
}

// Simple text helpers
void clearContentBelowHeader() {
  // Clear everything except the header line where the app name sits
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

// Font style selector for printLine
enum class FontStyle {
  Default,      // Legacy built-in bitmap font (scaled by TEXT_SIZE)
  RoundedSans,  // Approximation using FreeSansBold (rounded feel)
  CleanSans     // Clean sans using FreeSans regular
};

static inline const GFXfont* fontForStyle(FontStyle style) {
  switch (style) {
    case FontStyle::RoundedSans: return &FreeSansBold9pt7b;
    case FontStyle::CleanSans:   return &FreeSans9pt7b;
    case FontStyle::Default:
    default:                     return nullptr; // built-in 5x7 font
  }
}

void printLine(int lineIndex1Based, const String &msg, uint16_t color = COLOR_FG, FontStyle style = FontStyle::Default) {
  const GFXfont* chosen = fontForStyle(style);

  // Compute top of the line band (legacy layout uses top-left origin for default font)
  int16_t yTop = (lineIndex1Based - 1) * LINE_HEIGHT + 2;

  // Clear line background area
  tft.fillRect(0, yTop, tft.width(), LINE_HEIGHT, COLOR_BG);

  tft.setTextColor(color);

  if (chosen) {
    // Use GFX font variant; ensure unscaled size
    tft.setFont(chosen);
    tft.setTextSize(1);

    // For GFX fonts, cursor Y is the baseline. Anchor near bottom within our band.
    uint8_t yAdvance = chosen->yAdvance; // typical line height for the font
    int16_t adv = (int16_t)yAdvance;
    int16_t baseline = yTop + ((adv - 2 < (LINE_HEIGHT - 2)) ? (adv - 2) : (LINE_HEIGHT - 2));
    tft.setCursor(2, baseline);
  } else {
    // Default legacy font uses top-left as the reference point
    tft.setFont(nullptr);
    tft.setTextSize(TEXT_SIZE);
    tft.setCursor(2, yTop);
  }

  tft.print(msg);

  // Restore legacy defaults to avoid side effects for subsequent draws
  tft.setFont(nullptr);
  tft.setTextSize(TEXT_SIZE);
}

String macNoColonsUpper() {
  String mac = WiFi.macAddress(); // format: AA:BB:CC:DD:EE:FF
  String out;
  out.reserve(12);
  for (size_t i = 0; i < mac.length(); i++) {
    if (mac[i] != ':') out += (char)toupper((int)mac[i]);
  }
  return out; // 12 hex chars
}

void buildNamesFromMac() {
  String mac = macNoColonsUpper();
  // Guard in case MAC not ready yet
  if (mac.length() != 12) {
    mac = F("000000000000");
  }
  String last4 = mac.substring(8);   // last 4 hex digits
  String last6 = mac.substring(6);   // last 6 hex digits
  g_serviceName = String(F("HiveSync-")) + last4;
  g_pop = String(F("Hive-")) + last6;
}

// WiFi / Provisioning event handler
void SysProvEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_PROV_START:
      clearContentBelowHeader();
      printLine(2, String(F("Name: ")) + g_serviceName);
      printLine(3, String(F("POP:  ")) + g_pop);
      break;

    case ARDUINO_EVENT_PROV_CRED_RECV:
      printLine(4, F("Credentials received"));
      break;

    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      printLine(4, F("Provisioning OK"), ST77XX_GREEN);
      break;

    case ARDUINO_EVENT_PROV_CRED_FAIL:
      printLine(4, F("Provisioning failed"), ST77XX_RED);
      break;

    case ARDUINO_EVENT_PROV_END:
      // Will attempt to connect next
      break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      printLine(4, F("WiFi connected"));
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      g_connected = true;
      IPAddress ip(sys_event->event_info.got_ip.ip_info.ip.addr);
      printLine(5, String(F("IP: ")) + ip.toString());
      drawWifiIcon(true);
      break;
    }

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      g_connected = false;
      printLine(5, F("WiFi disconnected"), ST77XX_YELLOW);
      drawWifiIcon(false);
      break;

    default:
      break;
  }
}

// Detect long-press on BOOT (GPIO0) for ~2 seconds during boot to clear credentials
bool checkResetProvisioningOnBoot(uint32_t holdMs = 2000) {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  uint32_t start = millis();
  // If button is already pressed on boot, ensure it's held low for the duration
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    while (millis() - start < holdMs) {
      if (digitalRead(RESET_BUTTON_PIN) != LOW) {
        return false; // released early
      }
      delay(10);
    }
    return true; // held long enough
  }
  return false;
}

void setupDisplay() {
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

void beginProvisionIfNeeded() {
  // Determine if device already has credentials stored
  String existing = WiFi.SSID();
  bool hasCreds = existing.length() > 0;

  WiFi.onEvent(SysProvEvent);
  WiFi.mode(WIFI_STA);

  if (hasCreds) {
    // Attempt to connect using stored credentials
    printLine(2, String(F("Connecting to: ")) + existing);
    WiFi.begin();
  } else {
    // Start BLE provisioning with derived service name and POP
    uint8_t uuid[16] = {0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
                        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02 };
    WiFiProv.beginProvision(
      WIFI_PROV_SCHEME_BLE,
      WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
      WIFI_PROV_SECURITY_1,
      g_pop.c_str(),
      g_serviceName.c_str(),
      nullptr,
      uuid,
      false // do not reset already-provisioned credentials automatically
    );
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);

  setupDisplay();
  buildNamesFromMac();

  // Show header immediately
  printHeader();

  // Long-press BOOT to clear credentials
  if (checkResetProvisioningOnBoot()) {
    clearContentBelowHeader();
    printLine(2, F("Clearing WiFi credentials..."), ST77XX_YELLOW);
    // Erase WiFi config from NVS so device is "not provisioned"
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true); // erase config and persistent storage
    delay(200);
    printLine(3, F("Restarting..."));
    delay(500);
    ESP.restart();
  }

  // If not clearing, proceed
  clearContentBelowHeader();
  printLine(1, F("HiveSync")); // ensure app name visible

  beginProvisionIfNeeded();
}

void loop() {
  // Nothing heavy here; events and provisioning handle networking
  // Optionally, blink onboard LED to indicate status
  static uint32_t lastBlink = 0;
  static bool led = false;
  if (millis() - lastBlink > (g_connected ? 800 : 250)) {
    lastBlink = millis();
    led = !led;
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, led ? HIGH : LOW);
  }
}
