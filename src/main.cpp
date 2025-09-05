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

void setup() {
  Serial.begin(115200);
  delay(50);

  // Initialize display and show header
  UI::init();

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
  UI::printLine(1, F("HiveSync"));
  Provisioning::beginIfNeeded(serviceName, pop);
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
}
