// Beep API client implementation

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>

#include "beep_client.h"
#include "provisioning.h"

#define HS_LOG_PREFIX "BEEP"
#include "debug.h"

// Default base URL (used if config.json omits beep_base_url)
static const char* kDefaultBeepBaseUrl = "https://api.beep.nl";

namespace BeepClient {

static String s_apiToken; // cached during current boot
static bool   s_cfgLoaded = false;
static String s_email, s_password, s_deviceKey, s_baseUrl;

// Minimal JSON string value finder: finds "key":"value" and returns value
static String jsonFindString(const String &body, const String &key, int from = 0) {
  String needle = String("\"") + key + "\":";
  int k = body.indexOf(needle, from);
  if (k == -1) return String();
  // allow optional spaces after ':'
  int pos = k + needle.length();
  while (pos < (int)body.length() && (body[pos] == ' ')) pos++;
  if (pos >= (int)body.length() || body[pos] != '"') return String();
  int q1 = pos;
  int q2 = body.indexOf('"', q1 + 1);
  if (q2 == -1) return String();
  return body.substring(q1 + 1, q2);
}

static bool ensureConfigLoaded() {
  if (s_cfgLoaded) return true;
  // Try to mount LittleFS (do not auto-format to avoid erasing data unexpectedly)
  if (!LittleFS.begin(false)) {
    LOGLN("LittleFS mount failed");
  } else {
    const char* path = "/config.json";
    if (LittleFS.exists(path)) {
      File f = LittleFS.open(path, FILE_READ);
      if (f) {
        String json;
        while (f.available()) {
          json += f.readString();
          if (json.length() > 4096) break; // safety cap
        }
        f.close();
        // Extract values
        String e  = jsonFindString(json, String("beep_email"));
        String p  = jsonFindString(json, String("beep_password"));
        String dk = jsonFindString(json, String("beep_device_key"));
        String bu = jsonFindString(json, String("beep_base_url"));
        if (e.length() && p.length() && dk.length()) {
          s_email = e; s_password = p; s_deviceKey = dk;
          s_baseUrl = bu.length() ? bu : String(kDefaultBeepBaseUrl);
          s_cfgLoaded = true;
          LOGLN("Loaded Beep config from /config.json");
        } else {
          LOGLN("config.json missing required keys");
        }
      } else {
        LOGLN("Failed to open /config.json");
      }
    } else {
      LOGLN("/config.json not found");
    }
  }
  return s_cfgLoaded;
}

bool isConfigured() {
  return ensureConfigLoaded();
}

static bool httpsPostJson(const String &url, const String &payload, String &outBody, int &outCode, const String &bearer = String(), int timeoutMs = 15000) {
  outBody = String();
  outCode = 0;
  WiFiClientSecure client;
  client.setInsecure(); // simplify TLS (consider cert pinning for production)
  HTTPClient http;
  http.setTimeout(timeoutMs);
  LOGF("POST %s\n", url.c_str());
  if (!http.begin(client, url)) {
    LOGLN("http.begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  if (bearer.length()) {
    http.addHeader("Authorization", String("Bearer ") + bearer);
  }
  outCode = http.POST(payload);
  LOGF("HTTP %d\n", outCode);
  outBody = http.getString();
  if (outBody.length()) {
    LOGF("Body(%d): %s\n", outBody.length(), outBody.substring(0, 200).c_str());
  }
  http.end();
  return (outCode > 0);
}

static bool login(String &tokenOut, String &err) {
  tokenOut = String();
  err = String();
  if (!Provisioning::isConnected()) { err = F("WiFi not connected"); return false; }
  if (!ensureConfigLoaded()) { err = F("BEEP config missing"); return false; }

  String url = (s_baseUrl.length() ? s_baseUrl : String(kDefaultBeepBaseUrl)) + "/api/login";
  // Build JSON payload safely
  String payload = String("{") +
                   "\"email\":\"" + s_email + "\"," +
                   "\"password\":\"" + s_password + "\"}";

  String body; int code = 0;
  if (!httpsPostJson(url, payload, body, code)) {
    err = F("HTTP error during login");
    return false;
  }
  if (code != 200 && code != 201) {
    err = String(F("Login failed: ")) + code;
    return false;
  }

  String token = jsonFindString(body, String("api_token"));
  if (!token.length()) {
    err = F("Login response missing api_token");
    return false;
  }
  tokenOut = token;
  return true;
}

bool ensureTimeSynced(uint32_t timeoutMs) {
  time_t now = time(nullptr);
  if (now > 1609459200) { // 2021-01-01
    return true;
  }
  // Configure NTP servers (UTC)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    delay(100);
    now = time(nullptr);
    if (now > 1609459200) return true;
  }
  LOGLN("NTP time sync failed or timed out");
  return false;
}

bool uploadReadings(const KV* items, size_t count, uint32_t sampleMillis, String &err) {
  err = String();
  if (!items || count == 0) { err = F("No readings"); return false; }
  for (size_t i = 0; i < count; ++i) {
    if (!items[i].key || !isfinite(items[i].value)) { err = F("Invalid reading"); return false; }
  }
  if (!Provisioning::isConnected()) { err = F("WiFi not connected"); return false; }
  if (!ensureConfigLoaded()) { err = F("BEEP config missing"); return false; }

  // Obtain token once per boot
  if (!s_apiToken.length()) {
    String tok; String lerr;
    if (!login(tok, lerr)) { err = lerr; return false; }
    s_apiToken = tok;
    LOGLN("Login OK; token cached");
  }

  // Sync time; compute reading epoch = now - elapsed since sample
  uint32_t nowMs = millis();
  uint32_t elapsedMs = (nowMs >= sampleMillis) ? (nowMs - sampleMillis) : 0;
  time_t nowEpoch = 0;
  if (ensureTimeSynced(7000)) {
    nowEpoch = time(nullptr);
  }
  long readingEpoch = 0;
  if (nowEpoch > 0) {
    long elapsedSec = (long)(elapsedMs / 1000UL);
    readingEpoch = (long)nowEpoch - elapsedSec;
  }

  // Build payload: {"key":"...", "time": <epochSec>, <k1>:<v1>, <k2>:<v2>, ...}
  String payload;
  payload.reserve(64 + count * 24);
  payload += '{';
  payload += "\"key\":\""; payload += s_deviceKey; payload += "\",";
  payload += "\"time\":"; payload += String(readingEpoch > 0 ? readingEpoch : 0); 

  for (size_t i = 0; i < count; ++i) {
    payload += ',';
    payload += '"'; payload += items[i].key; payload += '"';
    payload += ':';
    char num[16];
    dtostrf(items[i].value, 0, 2, num);
    payload += num;
  }
  payload += '}';

  String url = (s_baseUrl.length() ? s_baseUrl : String(kDefaultBeepBaseUrl)) + "/api/sensors";
  String body; int code = 0;
  if (!httpsPostJson(url, payload, body, code, s_apiToken)) {
    err = F("HTTP error during upload");
    return false;
  }
  if (code < 200 || code >= 300) {
    err = String(F("Upload failed: ")) + code;
    return false;
  }
  LOGLN("Upload OK");
  return true;
}

} // namespace BeepClient
