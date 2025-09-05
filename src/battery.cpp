// Battery fuel gauge (MAX17048/49) module

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MAX1704X.h>

#include "battery.h"

namespace Battery {

static Adafruit_MAX17048 s_gauge;
static bool s_found = false;
static int s_percent = -1; // last known percent
static uint32_t s_lastUpdate = 0;

bool begin() {
  // Ensure I2C is initialized; use default pins from the board variant
  Wire.begin();
  // Try to initialize the gauge at default address 0x36
  s_found = s_gauge.begin();
  // First read if available
  if (s_found) {
    float p = s_gauge.cellPercent();
    if (isfinite(p)) {
      int ip = (int)(p + 0.5f);
      if (ip < 0) ip = 0; if (ip > 100) ip = 100;
      s_percent = ip;
    }
  }
  return s_found;
}

void update() {
  if (!s_found) return;
  uint32_t now = millis();
  if (now - s_lastUpdate < 2000) return; // rate-limit
  s_lastUpdate = now;

  float p = s_gauge.cellPercent();
  if (!isfinite(p)) return;
  int ip = (int)(p + 0.5f);
  if (ip < 0) ip = 0; if (ip > 100) ip = 100;
  s_percent = ip;
}

int percent() {
  return s_percent;
}

} // namespace Battery
