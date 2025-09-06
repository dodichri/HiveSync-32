// WiFi provisioning implementation

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <esp_wifi.h>
#include <Adafruit_ST7789.h> // for ST77XX_* color constants used in UI calls

#include "ui.h"
#include "provisioning.h"

// Fallback for BOOT button if not defined by variant
#ifndef RESET_BUTTON_PIN
#define RESET_BUTTON_PIN 0
#endif

namespace Provisioning {

static String s_serviceName;  // HiveSync-<last4>
static String s_pop;          // Hive-<last6>
static volatile bool s_connected = false;

bool isConnected() { return s_connected; }

void onEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_PROV_START:
      UI::clearContentBelowHeader();
      UI::printLine(3, String(F("Name: ")) + s_serviceName);
      UI::printLine(4, String(F("POP:  ")) + s_pop);
      break;

    case ARDUINO_EVENT_PROV_CRED_RECV:
      UI::printLine(5, F("Credentials received"));
      break;

    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      UI::printLine(5, F("Provisioning OK"), ST77XX_GREEN);
      break;

    case ARDUINO_EVENT_PROV_CRED_FAIL:
      UI::printLine(5, F("Provisioning failed"), ST77XX_RED);
      break;

    case ARDUINO_EVENT_PROV_END:
      // Will attempt to connect next
      break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      // Suppress verbose "WiFi connected" text on the display
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      s_connected = true;
      IPAddress ip(sys_event->event_info.got_ip.ip_info.ip.addr);
      // Suppress showing the IP address on the display
      UI::drawWifiIcon(true);
      break;
    }

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      s_connected = false;
      UI::drawWifiIcon(false);
      break;

    default:
      break;
  }
}

bool checkResetProvisioningOnBoot(uint32_t holdMs) {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  uint32_t start = millis();
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

void beginIfNeeded(const String &serviceName, const String &pop) {
  s_serviceName = serviceName;
  s_pop = pop;

  String existing = WiFi.SSID();
  bool hasCreds = existing.length() > 0;

  WiFi.onEvent(onEvent);
  WiFi.begin();

  if (hasCreds) {
    UI::printLine(3, String(F("Connecting to: ")) + existing);
  } else {
    uint8_t uuid[16] = {0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
                        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02 };

    WiFiProv.beginProvision(
      WIFI_PROV_SCHEME_BLE,
      WIFI_PROV_SCHEME_HANDLER_FREE_BLE,
      WIFI_PROV_SECURITY_1,
      pop.c_str(),
      serviceName.c_str(),
      nullptr,
      uuid,
      false
    );
  }
}

} // namespace Provisioning
