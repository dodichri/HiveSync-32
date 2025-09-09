// HiveSync single-shot sampling with deep sleep
// - Take a sensor reading, show briefly, then deep sleep
// - Wakes every sample_interval_ms from config.json (default 15 minutes)

#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_ST7789.h> // for ST77XX_* color constants
#include <esp_sleep.h>

#include "ui.h"
#include "provisioning.h"
#include "device_info.h"
#include "battery.h"
#include "updater.h"
#include "sensors.h"
#include "config.h"

#define HS_LOG_PREFIX "MAIN"
#include "debug.h"

void setup() {
  Serial.begin(115200);
  while(!Serial) {
    delay(50); // wait for serial port to connect. Needed for native USB
  }
  
  LOGLN("Booting HiveSync");
  // Initialize display and show header
  UI::init();

  // Load configuration from LittleFS (creates defaults if missing)
  if (!Config::begin()) {
    UI::printLine(2, F("Config load failed"), ST77XX_RED);
  } else {
    Config::dumpToLog();
  }

  // Initialize battery gauge (if present)
  if (Battery::begin()) {
    UI::setBatteryPercent(Battery::percent());
    LOGF("Battery gauge OK: %d%%\n", Battery::percent());
  } else {
    UI::setBatteryPercent(-1); // hide if not detected
    LOGLN("Battery gauge not detected");
  }
  UI::clearContentBelowHeader();
  UI::printHeader();

  // Derive BLE provisioning service name and POP
  String serviceName, pop;
  DeviceInfo::deriveNames(serviceName, pop);
  LOGF("BLE name=%s POP=%s\n", serviceName.c_str(), pop.c_str());

  // Determine if credentials existed at boot (to decide sleep policy)
  bool hadCreds = WiFi.SSID().length() > 0;

  // Option to clear Wi‑Fi credentials via BOOT long-press
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

  // Start provisioning or connect to stored credentials
  Provisioning::beginIfNeeded(serviceName, pop);

  // Wait for Wi‑Fi connection (with timeout)
#ifndef HS_WIFI_CONNECT_TIMEOUT_MS
#define HS_WIFI_CONNECT_TIMEOUT_MS 60000UL
#endif
  uint32_t wifiStart = millis();
  while (!Provisioning::isConnected() && (millis() - wifiStart) < HS_WIFI_CONNECT_TIMEOUT_MS) {
    Battery::update();
    int p = Battery::percent();
    if (p >= 0) UI::setBatteryPercent(p);
    delay(50);
  }

  if (Provisioning::isConnected()) {
    // Perform a one-time firmware check/update
#ifndef HS_OTA_CHECK_TIMEOUT_MS
#define HS_OTA_CHECK_TIMEOUT_MS 60000UL
#endif
    uint32_t otaStart = millis();
    while (!Updater::checkCompleted() && (millis() - otaStart) < HS_OTA_CHECK_TIMEOUT_MS) {
      Updater::loop();
      delay(50);
    }
  } else {
    if (!hadCreds) {
      // First-time provisioning: stay awake to allow BLE provisioning, then proceed
      UI::printLine(3, F("Provisioning (BLE) active"), ST77XX_YELLOW);
      UI::printLine(4, F("Waiting for WiFi..."), UI::COLOR_WHITE_SMOKE);
      LOGLN("No saved WiFi; waiting indefinitely for provisioning");
      while (!Provisioning::isConnected()) {
        Battery::update();
        int p = Battery::percent();
        if (p >= 0) UI::setBatteryPercent(p);
        delay(100);
      }
      while (!Updater::checkCompleted()) {
        Updater::loop();
        delay(50);
      }
    } else {
      UI::printLine(3, F("WiFi not connected"), ST77XX_YELLOW);
      UI::printLine(4, F("Skipping firmware check"), ST77XX_YELLOW);
    }
  }

  // Start sensors and perform a single read
  Sensors::begin();
  if (Sensors::ds18b20Available()) {
    float tC = Sensors::ds18b20LastTempC();
    if (isfinite(tC)) {
      char buf[32];
      snprintf(buf, sizeof(buf), "DS18B20: %.1f C", tC);
      UI::printLine(3, String(buf), UI::COLOR_DEEP_TEAL);
    } else {
      UI::printLine(3, F("DS18B20: no valid reading"), ST77XX_RED);
    }
  } else {
    UI::printLine(3, F("DS18B20 not found"), ST77XX_YELLOW);
  }

  // Brief pause to show readings, then deep sleep
  UI::printLine(7, F("Sleeping..."), UI::COLOR_WHITE_SMOKE);
  delay(2500);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  UI::powerDown();

  // Prepare deep sleep for the configured interval (ms -> us)
  const unsigned long intervalMs = Config::sampleIntervalMs();
  const uint64_t sleepUs = (uint64_t)intervalMs * 1000ULL;
  LOGF("Deep sleep for %lu ms (%.1f min)\n",
       (unsigned long)intervalMs,
       (double)intervalMs / 60000.0);
  esp_sleep_enable_timer_wakeup(sleepUs);
  esp_deep_sleep_start();
}

void loop() {
  // Not used; we deep sleep at the end of setup().
}
