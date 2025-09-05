// WiFi provisioning and connection handling
#pragma once

#include <Arduino.h>

// forward declaration from Arduino core (remove 'struct' keyword)
typedef arduino_event_t arduino_event_t;

namespace Provisioning {

// Start WiFi or BLE provisioning depending on stored credentials
void beginIfNeeded(const String &serviceName, const String &pop);

// Arduino WiFi event handler
void onEvent(arduino_event_t *sys_event);

// Detect long-press on BOOT (GPIO0) during boot to clear credentials
bool checkResetProvisioningOnBoot(uint32_t holdMs = 2000);

// Connection status useful for UI/LED feedback
bool isConnected();

} // namespace Provisioning

