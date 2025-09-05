// HX711 load cell amplifier module
#pragma once

#include <Arduino.h>

// Defaults to disabled unless pins provided via build_flags or another header
#ifndef HX711_DOUT_PIN
#define HX711_DOUT_PIN -1
#endif
#ifndef HX711_SCK_PIN
#define HX711_SCK_PIN -1
#endif
#ifndef HX711_CALIBRATION
#define HX711_CALIBRATION 1.0f // user should calibrate; units per raw division
#endif

namespace HX711Sensor {

// Initialize HX711 on given pins. If pins are <0, initialization is skipped.
// Sets scale using HX711_CALIBRATION and performs a quick tare.
bool begin(int doutPin = HX711_DOUT_PIN, int sckPin = HX711_SCK_PIN, float calibration = HX711_CALIBRATION);

// Read average units using current calibration. Returns true on success.
// 'samples' is the number of averaged readings (typ 5-15 for stability).
bool readUnits(float &outUnits, int samples = 10);

// Put HX711 into low-power mode.
void powerDown();

// Whether HX711 is configured and available
bool available();

} // namespace HX711Sensor

