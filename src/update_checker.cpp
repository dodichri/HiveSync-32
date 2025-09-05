// Periodic GitHub firmware update check for ESP32 (Arduino)

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "update_checker.h"

// Build-time configuration (see platformio.ini build_flags)
#ifndef FW_VERSION
#define FW_VERSION "0.0.0"
#endif

#ifndef GITHUB_OWNER
#define GITHUB_OWNER ""
#endif

#ifndef GITHUB_REPO
#define GITHUB_REPO ""
#endif

#ifndef GITHUB_CHECK_INTERVAL_MS
#define GITHUB_CHECK_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL) // 24h
#endif

namespace UpdateChecker {

static TaskHandle_t s_task = nullptr;
static String s_currentVersion = String(FW_VERSION);
static String s_latestVersion;
static volatile uint32_t s_lastCheckMs = 0;
static volatile Status s_status = Status::Unknown;

static String normalizeVersion(const String &v) {
  String out = v;
  out.trim();
  if (out.length() && (out[0] == 'v' || out[0] == 'V')) {
    out.remove(0, 1);
  }
  return out;
}

static int parseIntPrefix(const String &s) {
  int val = 0;
  bool any = false;
  for (size_t i = 0; i < s.length(); ++i) {
    if (s[i] >= '0' && s[i] <= '9') {
      any = true;
      val = val * 10 + (s[i] - '0');
    } else {
      break;
    }
  }
  return any ? val : 0;
}

static int compareSemver(const String &aRaw, const String &bRaw) {
  // return 1 if a>b, -1 if a<b, 0 if equal
  String a = normalizeVersion(aRaw);
  String b = normalizeVersion(bRaw);

  int partsA[3] = {0, 0, 0};
  int partsB[3] = {0, 0, 0};

  int idx = 0;
  int start = 0;
  for (size_t i = 0; i <= a.length() && idx < 3; ++i) {
    if (i == a.length() || a[i] == '.') {
      partsA[idx++] = parseIntPrefix(a.substring(start, i));
      start = i + 1;
    }
  }

  idx = 0;
  start = 0;
  for (size_t i = 0; i <= b.length() && idx < 3; ++i) {
    if (i == b.length() || b[i] == '.') {
      partsB[idx++] = parseIntPrefix(b.substring(start, i));
      start = i + 1;
    }
  }

  for (int i = 0; i < 3; ++i) {
    if (partsA[i] > partsB[i]) return 1;
    if (partsA[i] < partsB[i]) return -1;
  }
  return 0;
}

static String jsonExtractString(const String &json, const char *key) {
  String needle = String('"') + key + '"';
  int p = json.indexOf(needle);
  if (p < 0) return String();
  p = json.indexOf(':', p + needle.length());
  if (p < 0) return String();
  // skip whitespace
  while (p < (int)json.length() && (json[p] == ':' || json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n')) {
    ++p;
  }
  if (p >= (int)json.length() || json[p] != '"') return String();
  ++p; // past opening quote
  String out;
  while (p < (int)json.length()) {
    char c = json[p++];
    if (c == '"') break;
    if (c == '\\') { // skip simple escape
      if (p < (int)json.length()) {
        out += json[p++];
      }
    } else {
      out += c;
    }
  }
  return out;
}

static void doCheck() {
  const char *owner = GITHUB_OWNER;
  const char *repo = GITHUB_REPO;
  const char *fw    = FW_VERSION;

  if (!owner || !repo || owner[0] == '\0' || repo[0] == '\0') {
    Serial.println(F("[Update] Disabled: GITHUB_OWNER/REPO not set"));
    s_status = Status::Error;
    return;
  }

  String url = String(F("https://api.github.com/repos/")) + owner + '/' + repo + F("/releases/latest");

  WiFiClientSecure client;
  client.setInsecure(); // NOTE: for simplicity; consider using a root CA in production
  HTTPClient http;

  Serial.print(F("[Update] Checking "));
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println(F("[Update] HTTP begin failed"));
    s_status = Status::Error;
    return;
  }
  http.addHeader("User-Agent", "HiveSync-32");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.print(F("[Update] HTTP error: "));
    Serial.println(code);
    http.end();
    s_status = Status::Error;
    return;
  }

  String body = http.getString();
  http.end();

  String latest = jsonExtractString(body, "tag_name");
  if (latest.length() == 0) {
    Serial.println(F("[Update] Failed to parse tag_name"));
    s_status = Status::Error;
    return;
  }

  String latestNorm = normalizeVersion(latest);
  String currentNorm = normalizeVersion(String(FW_VERSION));

  int cmp = compareSemver(latestNorm, currentNorm);
  s_latestVersion = latestNorm;
  s_lastCheckMs = millis();
  if (cmp > 0) {
    Serial.print(F("[Update] New firmware available: v"));
    Serial.print(latestNorm);
    Serial.print(F(" (current v"));
    Serial.print(currentNorm);
    Serial.println(')');
    s_status = Status::UpdateAvailable;
  } else if (cmp == 0) {
    Serial.print(F("[Update] Up to date (v"));
    Serial.print(currentNorm);
    Serial.println(')');
    s_status = Status::UpToDate;
  } else {
    Serial.print(F("[Update] Local firmware newer than latest (local v"));
    Serial.print(currentNorm);
    Serial.print(F(", remote v"));
    Serial.print(latestNorm);
    Serial.println(')');
    s_status = Status::LocalNewer;
  }
}

static void task(void *arg) {
  // Little initial delay to allow boot logs and Wi-Fi init
  vTaskDelay(pdMS_TO_TICKS(5000));

  for (;;) {
    // Ensure Wi-Fi is connected
    uint32_t waited = 0;
    while (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      waited += 2000;
      if (waited > 30000) break; // don't wait forever; try again next cycle
    }

    if (WiFi.status() == WL_CONNECTED) {
      doCheck();
    } else {
      Serial.println(F("[Update] Skipping check: no Wi-Fi"));
    }

    vTaskDelay(pdMS_TO_TICKS(GITHUB_CHECK_INTERVAL_MS));
  }
}

void begin() {
  if (s_task) return;
  xTaskCreate(task, "UpdateCheck", 8192, nullptr, 1, &s_task);
}

Status status() {
  return s_status;
}

String currentVersion() {
  return s_currentVersion;
}

String latestVersion() {
  return s_latestVersion;
}

bool updateAvailable() {
  return s_status == Status::UpdateAvailable;
}

uint32_t lastCheckMillis() {
  return s_lastCheckMs;
}

} // namespace UpdateChecker
