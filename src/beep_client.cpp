// Beep API client implementation

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
#include <ctype.h>

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

// (removed httpsPostJson in favor of httpsSendJson)

static bool httpsGet(const String &url, String &outBody, int &outCode, const String &bearer = String(), int timeoutMs = 15000) {
  outBody = String();
  outCode = 0;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(timeoutMs);
  LOGF("GET %s\n", url.c_str());
  if (!http.begin(client, url)) {
    LOGLN("http.begin failed");
    return false;
  }
  http.addHeader("Accept", "application/json");
  if (bearer.length()) {
    http.addHeader("Authorization", String("Bearer ") + bearer);
  }
  outCode = http.GET();
  LOGF("HTTP %d\n", outCode);
  outBody = http.getString();
  if (outBody.length()) {
    LOGF("Body(%d): %s\n", outBody.length(), outBody.substring(0, 200).c_str());
  }
  http.end();
  return (outCode > 0);
}

static bool httpsSendJson(const String &method, const String &url, const String &payload, String &outBody, int &outCode, const String &bearer = String(), int timeoutMs = 15000) {
  outBody = String();
  outCode = 0;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(timeoutMs);
  LOGF("%s %s\n", method.c_str(), url.c_str());
  if (!http.begin(client, url)) {
    LOGLN("http.begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  if (bearer.length()) {
    http.addHeader("Authorization", String("Bearer ") + bearer);
  }
  outCode = http.sendRequest(method.c_str(), (uint8_t*)payload.c_str(), payload.length());
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
  if (!httpsSendJson("POST", url, payload, body, code)) {
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

// Ensure we have an API token cached (logs in if needed)
static bool ensureLoggedIn(String &err) {
  if (s_apiToken.length()) return true;
  String tok; String lerr;
  if (!login(tok, lerr)) { err = lerr.length() ? lerr : String(F("Login failed")); return false; }
  s_apiToken = tok;
  LOGLN("Login OK; token cached");
  return true;
}

// Authenticated JSON request helper that ensures login and attaches Bearer token
static bool sendJsonAuth(const char* method, const String &url, const String &payload, String &outBody, int &outCode, String &err, int timeoutMs = 15000) {
  if (!ensureLoggedIn(err)) return false;
  if (!httpsSendJson(method, url, payload, outBody, outCode, s_apiToken, timeoutMs)) {
    if (String(method) == "POST") err = F("HTTP error during POST");
    else if (String(method) == "PATCH") err = F("HTTP error during PATCH");
    else err = F("HTTP error during request");
    return false;
  }
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
  if (!ensureLoggedIn(err)) { return false; }

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
  if (!sendJsonAuth("POST", url, payload, body, code, err)) {
    return false;
  }
  if (code < 200 || code >= 300) {
    err = String(F("Upload failed: ")) + code;
    return false;
  }
  LOGLN("Upload OK");
  return true;
}

// Try to locate the device id in a /api/devices response body by matching device key
static long findDeviceIdByKey(const String &body, const String &devKey) {
  String keyNeedle = String("\"key\":\"") + devKey + "\"";
  int kp = body.indexOf(keyNeedle);
  if (kp == -1) return -1;
  // Prefer to search backward for id close to the key occurrence
  int idPrev = body.lastIndexOf("\"id\":", kp);
  if (idPrev != -1 && (kp - idPrev) < 400) {
    int i = idPrev + 5; // after "id":
    while (i < (int)body.length() && (body[i] == ' ')) i++;
    int j = i;
    while (j < (int)body.length() && isdigit((unsigned char)body[j])) j++;
    if (j > i) {
      return atol(body.substring(i, j).c_str());
    }
  }
  // Otherwise, search forward
  int idNext = body.indexOf("\"id\":", kp);
  if (idNext != -1 && (idNext - kp) < 200) {
    int i = idNext + 5;
    while (i < (int)body.length() && (body[i] == ' ')) i++;
    int j = i;
    while (j < (int)body.length() && isdigit((unsigned char)body[j])) j++;
    if (j > i) {
      return atol(body.substring(i, j).c_str());
    }
  }
  return -1;
}

bool updateFirmwareVersion(const char* version, String &err) {
  err = String();
  if (!version || !*version) { err = F("Empty version"); return false; }
  if (!Provisioning::isConnected()) { err = F("WiFi not connected"); return false; }
  if (!ensureConfigLoaded()) { err = F("BEEP config missing"); return false; }

  if (!ensureLoggedIn(err)) { return false; }

  // 1) Fetch devices for this account
  String listUrl = (s_baseUrl.length() ? s_baseUrl : String(kDefaultBeepBaseUrl)) + "/api/devices";
  String body; int code = 0;
  if (!httpsGet(listUrl, body, code, s_apiToken)) {
    err = F("HTTP error listing devices");
    return false;
  }
  if (code < 200 || code >= 300) {
    err = String(F("List devices failed: ")) + code;
    return false;
  }

  long devId = findDeviceIdByKey(body, s_deviceKey);
  if (devId <= 0) {
    err = F("Device key not found");
    return false;
  }

  // 2) Attempt to PATCH firmware version (try a few common field names)
  String devUrl = (s_baseUrl.length() ? s_baseUrl : String(kDefaultBeepBaseUrl)) + "/api/devices/" + String(devId);
  const char* fields[] = { "firmware_version", "fw_version", "firmware" };
  for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
    String payload = String("{") + "\"" + fields[i] + "\":\"" + version + "\"}";
    String rbody; int rcode = 0; String reqErr;
    if (!sendJsonAuth("PATCH", devUrl, payload, rbody, rcode, reqErr)) {
      // try next field name
      continue;
    }
    if (rcode >= 200 && rcode < 300) {
      LOGLN("Firmware version updated on BEEP");
      return true;
    }
  }
  err = F("Device update failed");
  return false;
}

} // namespace BeepClient
