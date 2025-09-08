// HiveSync modular refactor for Adafruit Feather ESP32-S3 Reverse TFT
// - BLE Wi-Fi provisioning with POP and device name derived from MAC
// - UI routines moved to UI module
// - Provisioning logic encapsulated in Provisioning module
// - Device info helpers in DeviceInfo module

#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_ST7789.h> // for ST77XX_* color constants

#include "ui.h"
#include "provisioning.h"
#include "device_info.h"
#include "battery.h"
#include "updater.h"
#include "sensors.h"

#define HS_LOG_PREFIX "MAIN"
#include "debug.h"

void setup() {
  Serial.begin(115200);
  delay(50);
  LOGLN("Booting HiveSync");

  // Initialize display and show header
  UI::init();

  // Initialize battery gauge (if present)
  if (Battery::begin()) {
    UI::setBatteryPercent(Battery::percent());
    LOGF("Battery gauge OK: %d%%\n", Battery::percent());
  } else {
    UI::setBatteryPercent(-1); // hide if not detected
    LOGLN("Battery gauge not detected");
  }

  // Derive BLE service name and POP from MAC
  String serviceName, pop;
  DeviceInfo::deriveNames(serviceName, pop);
  LOGF("BLE name=%s POP=%s\n", serviceName.c_str(), pop.c_str());

  // Long-press BOOT to clear credentials
  if (Provisioning::checkResetProvisioningOnBoot()) {
    UI::clearContentBelowHeader();
    UI::printLine(2, F("Clearing WiFi credentials..."), ST77XX_YELLOW);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(200);
    UI::printLine(3, F("Restarting..."));
    delay(500);
    ESP.restart();
  }

  // Proceed with normal flow
  UI::clearContentBelowHeader();
  UI::printHeader();
  Provisioning::beginIfNeeded(serviceName, pop);
  LOGLN("Provisioning begun (or connecting with stored creds)");

  // Delay sensor initialization until WiFi connected and OTA check completes
}

void loop() {
  // Blink LED based on connection state
  static uint32_t lastBlink = 0;
  static bool led = false;
  if (millis() - lastBlink > (Provisioning::isConnected() ? 800 : 250)) {
    lastBlink = millis();
    led = !led;
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, led ? HIGH : LOW);
  }

  // Periodically update battery percentage and reflect in UI
  Battery::update();
  static int lastShown = -2; // force first update
  int p = Battery::percent();
  if (p != lastShown) {
    lastShown = p;
    UI::setBatteryPercent(p);
  }

  // After Wi-Fi connects, perform a one-time OTA check
  Updater::loop();

  // Initialize sensors once after OTA check completes, then sample periodically
  static bool sensorsStarted = false;
  if (!sensorsStarted && Provisioning::isConnected() && Updater::checkCompleted()) {
    LOGLN("Starting sensors after OTA check complete");
    Sensors::begin();
    sensorsStarted = true;
  }
  if (sensorsStarted) {
    Sensors::loop();
    // Show latest temperature on line 3 in deep teal when updated
    static uint32_t lastTempShownAt = 0;
    uint32_t sampleAt = Sensors::lastSampleMillis();
    if (Sensors::ds18b20Available() && sampleAt != 0 && sampleAt != lastTempShownAt) {
      float tC = Sensors::ds18b20LastTempC();
      if (isfinite(tC)) {
        char buf[24];
        snprintf(buf, sizeof(buf), "Temp: %.1f C", tC);
        UI::printLine(3, String(buf), UI::COLOR_DEEP_TEAL);
        lastTempShownAt = sampleAt;
      }
    }
  }
}
