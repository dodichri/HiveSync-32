// DS18B20 temperature sensor module implementation

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "temp_sensor.h"

namespace TempSensor {

static OneWire *s_ow = nullptr;
static DallasTemperature *s_dt = nullptr;
static bool s_found = false;
static int s_pin = -1;

bool begin(int pin) {
  s_found = false;
  s_pin = pin;
  if (pin < 0) {
    return false; // disabled by config
  }
  if (s_ow) { delete s_ow; s_ow = nullptr; }
  if (s_dt) { delete s_dt; s_dt = nullptr; }

  s_ow = new OneWire(pin);
  s_dt = new DallasTemperature(s_ow);
  s_dt->begin();

  // Probe for at least one device
  DeviceAddress addr;
  if (s_dt->getAddress(addr, 0)) {
    // Configure typical 12-bit resolution
    s_dt->setResolution(addr, 12);
    s_dt->setWaitForConversion(true);
    s_found = true;
  }
  return s_found;
}

bool available() { return s_found; }

bool readCelsius(float &outC, uint32_t timeoutMs) {
  outC = NAN;
  if (!s_dt || !s_found) return false;

  // Request conversion and wait (Dallas lib blocks if setWaitForConversion(true))
  uint32_t start = millis();
  s_dt->requestTemperatures();
  // Safety: user-supplied timeout on top of blocking wait
  while (millis() - start < timeoutMs) {
    // First sensor on bus
    float t = s_dt->getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t > -100.0f && t < 125.0f) {
      outC = t;
      return true;
    }
    delay(10);
  }
  return false;
}

} // namespace TempSensor

