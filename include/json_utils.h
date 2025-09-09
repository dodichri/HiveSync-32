// Minimal JSON helper utilities (string search) to avoid full JSON lib
#pragma once

#include <Arduino.h>

// Find a simple JSON string value by key in a flat JSON object/array text.
// Returns empty String if not found.
String jsonFindString(const String &body, const String &key, int from = 0);

