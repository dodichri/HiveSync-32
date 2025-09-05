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
#include "temp_sensor.h"
#include "hx711_sensor.h"

#include <esp_sleep.h>

// Optional: enable Wi-Fi provisioning during sensor cycle
#ifndef ENABLE_WIFI_PROVISIONING
#define ENABLE_WIFI_PROVISIONING 0
#endif

// Sleep interval in minutes
#ifndef SLEEP_MINUTES
#define SLEEP_MINUTES 15
#endif

void setup() {
  Serial.begin(115200);
  delay(50);

  // Initialize display and show header
  UI::init();

  // Initialize battery gauge (if present)
  if (Battery::begin()) {
    UI::setBatteryPercent(Battery::percent());
  } else {
    UI::setBatteryPercent(-1); // hide if not detected
  }

  // Derive BLE service name and POP from MAC
  String serviceName, pop;
  DeviceInfo::deriveNames(serviceName, pop);

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

#if ENABLE_WIFI_PROVISIONING
  Provisioning::beginIfNeeded(serviceName, pop);
#endif

  // Sensor read cycle
  UI::printLine(2, F("Reading sensors..."));

  // Temperature (DS18B20)
  bool tempOk = TempSensor::begin();
  float tempC = NAN;
  if (tempOk && TempSensor::readCelsius(tempC)) {
    char buf[48];
    snprintf(buf, sizeof(buf), "Temp: %.2f C", tempC);
    UI::printLine(3, String(buf));
    Serial.println(buf);
  } else {
    UI::printLine(3, F("Temp: not detected"), ST77XX_YELLOW);
    Serial.println(F("TempSensor not detected or read failed"));
  }

  // Weight (HX711)
  bool hxOk = HX711Sensor::begin();
  float weight = NAN;
  if (hxOk && HX711Sensor::readUnits(weight, 10)) {
    char buf[48];
    snprintf(buf, sizeof(buf), "Weight: %.2f", weight);
    UI::printLine(4, String(buf));
    Serial.println(buf);
  } else {
    UI::printLine(4, F("Weight: not detected"), ST77XX_YELLOW);
    Serial.println(F("HX711 not detected or read failed"));
  }

  // Short hold to view/update UI then deep sleep
  const uint32_t showMs = 1500;
  delay(showMs);

  // Power down HX711 before sleep
  HX711Sensor::powerDown();

  // Turn off Wi-Fi radio to save power then sleep
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  UI::printLine(5, String(F("Sleeping ")) + SLEEP_MINUTES + F(" min"));
  Serial.printf("Entering deep sleep for %d minutes\n", (int)SLEEP_MINUTES);

  uint64_t sleep_us = (uint64_t)SLEEP_MINUTES * 60ULL * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleep_us);
  delay(50);
  esp_deep_sleep_start();
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
}
