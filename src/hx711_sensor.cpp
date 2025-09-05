// HX711 load cell amplifier module implementation

#include <Arduino.h>
#include <HX711.h>

#include "hx711_sensor.h"

namespace HX711Sensor {

static HX711 s_hx;
static bool s_found = false;
static float s_cal = HX711_CALIBRATION;
static int s_dout = -1, s_sck = -1;

bool begin(int doutPin, int sckPin, float calibration) {
  s_found = false;
  s_dout = doutPin;
  s_sck = sckPin;
  s_cal = calibration;
  if (doutPin < 0 || sckPin < 0) {
    return false; // disabled by config
  }

  s_hx.begin(doutPin, sckPin);
  delay(10);
  // Basic sanity: try is_ready a few times
  uint32_t start = millis();
  while (!s_hx.is_ready() && (millis() - start) < 500) {
    delay(5);
  }
  if (!s_hx.is_ready()) {
    return false;
  }

  s_hx.set_scale(s_cal);
  s_hx.tare(10); // quick tare at startup
  s_found = true;
  return true;
}

bool available() { return s_found; }

bool readUnits(float &outUnits, int samples) {
  outUnits = NAN;
  if (!s_found) return false;
  // The HX711 library blocks internally while collecting 'samples'
  float v = s_hx.get_units(samples);
  if (!isfinite(v)) return false;
  outUnits = v;
  return true;
}

void powerDown() {
  if (!s_found) return;
  s_hx.power_down();
}

} // namespace HX711Sensor

