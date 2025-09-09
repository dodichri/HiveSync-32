// Sensors module implementation (DS18B20 support)

#include <Arduino.h>

#include "sensors.h"

#define HS_LOG_PREFIX "SNSR"
#include "debug.h"
#include "config.h"

#include <OneWire.h>
#include <DallasTemperature.h>

static OneWire* s_oneWire = nullptr;
static DallasTemperature* s_dt = nullptr;
static DeviceAddress s_addr = {0};
static bool s_dtFound = false;

// Format a DS18B20 8-byte address as hex string
static String addrToString(const DeviceAddress &addr) {
  char buf[3 * 8]; // "AA:" * 7 + "AA" + NUL (actually 23, allocate a bit extra)
  size_t pos = 0;
  for (int i = 0; i < 8; ++i) {
    uint8_t b = addr[i];
    uint8_t hi = (b >> 4) & 0xF;
    uint8_t lo = b & 0xF;
    buf[pos++] = hi < 10 ? ('0' + hi) : ('A' + (hi - 10));
    buf[pos++] = lo < 10 ? ('0' + lo) : ('A' + (lo - 10));
    if (i != 7) buf[pos++] = ':';
  }
  buf[pos] = '\0';
  return String(buf);
}

static uint32_t s_lastSample = 0;
static float s_lastTempC = NAN;

namespace Sensors {

void begin() {
  const int pin = Config::ds18b20Pin();
  const unsigned long interval = Config::sampleIntervalMs();
  if (pin < 0) {
    LOGLN("DS18B20 disabled (pin < 0)");
    return;
  }

  LOGF("Sensors.begin: DS18B20 enabled on GPIO %d, interval=%lu ms (~%.1f min)\n",
       pin,
       (unsigned long)interval,
       (double)interval / 60000.0);
  s_oneWire = new OneWire(pin);
  s_dt = new DallasTemperature(s_oneWire);
  s_dt->begin();

  uint8_t count = s_dt->getDeviceCount();
  LOGF("OneWire devices detected: %u\n", (unsigned)count);
  if (count > 0 && s_dt->getAddress(s_addr, 0)) {
    s_dtFound = true;
    LOGF("Using device[0] addr=%s\n", addrToString(s_addr).c_str());
    bool parasite = s_dt->isParasitePowerMode();
    LOGF("Power mode: %s\n", parasite ? "parasite" : "external");
    s_dt->setResolution(s_addr, 12); // 12-bit for best precision
    LOGF("Resolution set to %d-bit\n", s_dt->getResolution(s_addr));
    // Initial read on boot
    uint32_t t0 = millis();
    s_dt->requestTemperaturesByAddress(s_addr);
    float t = s_dt->getTempC(s_addr);
    uint32_t dt = millis() - t0;
    LOGF("Initial conversion time: %lu ms\n", (unsigned long)dt);
    if (t > -127.0f && t < 125.0f) {
      s_lastTempC = t;
      s_lastSample = millis();
      LOGF("DS18B20 initial: %.3f C\n", t);
    } else {
      LOGF("DS18B20 initial read invalid (%.3f C)\n", t);
    }
  } else {
    if (count > 0) {
      LOGLN("DS18B20 getAddress(0) failed");
    } else {
      LOGLN("DS18B20 not found");
    }
  }
}

void loop() {
  const unsigned long interval = Config::sampleIntervalMs();
  uint32_t now = millis();
  if (now - s_lastSample < interval) return;

  if (!s_dtFound)  {
    LOGF("No DS18B20 found");
    return;
  }

  LOGF("Sampling DS18B20 (elapsed=%lu ms since last)\n", (unsigned long)(now - s_lastSample));
  uint32_t t0 = millis();
  s_dt->requestTemperaturesByAddress(s_addr);
  float t = s_dt->getTempC(s_addr);
  s_lastSample = now;
  uint32_t conv = millis() - t0;
  if (t > -127.0f && t < 125.0f && isfinite(t)) {
    s_lastTempC = t;
    LOGF("DS18B20: %.3f C (conv=%lu ms). Next in %lu s\n",
         t,
         (unsigned long)conv,
         (unsigned long)(interval / 1000UL));
  } else {
    LOGF("DS18B20 read invalid (%.3f C, conv=%lu ms)\n", t, (unsigned long)conv);
  }
}

bool ds18b20Available() { return s_dtFound; }

float ds18b20LastTempC() { return s_lastTempC; }

uint32_t lastSampleMillis() { return s_lastSample; }

} // namespace Sensors
