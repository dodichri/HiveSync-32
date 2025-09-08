// Lightweight Serial debug macros with per-module prefix
#pragma once

#include <Arduino.h>

#ifndef HS_DEBUG
#define HS_DEBUG 0
#endif

#ifndef HS_LOG_PREFIX
#define HS_LOG_PREFIX "APP"
#endif

#if HS_DEBUG
  #define LOGF(fmt, ...) do { Serial.printf("[" HS_LOG_PREFIX "] " fmt, ##__VA_ARGS__); } while (0)
  #define LOGLN(msg)     do { Serial.println(String("[" HS_LOG_PREFIX "] ") + (msg)); } while (0)
#else
  #define LOGF(...)      do {} while (0)
  #define LOGLN(...)     do {} while (0)
#endif

