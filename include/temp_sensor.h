// DS18B20 temperature sensor module
#pragma once

#include <Arduino.h>

// Default to disabled unless defined via build_flags or another header
#ifndef DS18B20_PIN
#define DS18B20_PIN -1
#endif

namespace TempSensor {

// Initialize the OneWire + DallasTemperature driver on a given pin.
// If no pin provided, uses DS18B20_PIN. Returns true if a device was found.
bool begin(int pin = DS18B20_PIN);

// Read Celsius temperature from the first sensor on the bus.
// Returns true on success; 'outC' is set to the temperature.
// 'timeoutMs' bounds how long we wait for conversion (typ. 750ms @12-bit).
bool readCelsius(float &outC, uint32_t timeoutMs = 1000);

// Whether the module detected a DS18B20 on begin().
bool available();

} // namespace TempSensor

