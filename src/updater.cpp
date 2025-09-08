// GitHub OTA Updater implementation

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Adafruit_ST7789.h> // for ST77XX_* color constants

#include "ui.h"
#include "provisioning.h"
#include "updater.h"

// Build-time configuration (can be overridden via platformio.ini build_flags)
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0"
#endif
#ifndef GITHUB_OWNER
#define GITHUB_OWNER ""
#endif
#ifndef GITHUB_REPO
#define GITHUB_REPO ""
#endif
#ifndef FIRMWARE_ASSET
#define FIRMWARE_ASSET "firmware.bin"
#endif

namespace Updater {

static bool s_checkedThisBoot = false;

// Debug logging control
#ifndef OTA_DEBUG
#define OTA_DEBUG 1
#endif

#if OTA_DEBUG
#define DBG(fmt, ...) do { Serial.printf("[OTA] " fmt, ##__VA_ARGS__); } while(0)
#define DBGLN(msg)    do { Serial.println(String("[OTA] ") + (msg)); } while(0)
#else
#define DBG(...)      do {} while(0)
#define DBGLN(...)    do {} while(0)
#endif

static void logLine(int line, const String &msg, uint16_t color = UI::COLOR_WHITE_SMOKE) {
  UI::printLine(line, msg, color);
}

const char* currentVersion() {
  return FIRMWARE_VERSION;
}

// Parse a tag/version string into [major, minor, patch].
// Accepts optional leading 'v' and ignores any suffix after '-'.
static void parseSemVer(const String &ver, int &maj, int &min, int &pat) {
  maj = min = pat = 0;
  int start = 0;
  // Skip leading 'v' or 'V'
  if (ver.length() > 0 && (ver[0] == 'v' || ver[0] == 'V')) start = 1;
  // Stop at first '-' (pre-release/build suffix)
  int end = ver.indexOf('-', start);
  String core = (end == -1) ? ver.substring(start) : ver.substring(start, end);

  // Split by '.'
  int p1 = core.indexOf('.');
  int p2 = (p1 == -1) ? -1 : core.indexOf('.', p1 + 1);
  String sMaj = (p1 == -1) ? core : core.substring(0, p1);
  String sMin = (p1 == -1) ? "0" : ((p2 == -1) ? core.substring(p1 + 1) : core.substring(p1 + 1, p2));
  String sPat = (p2 == -1) ? "0" : core.substring(p2 + 1);

  maj = sMaj.toInt();
  min = sMin.toInt();
  pat = sPat.toInt();
}

static int compareSemVer(const String &a, const String &b) {
  int aM, aN, aP, bM, bN, bP;
  parseSemVer(a, aM, aN, aP);
  parseSemVer(b, bM, bN, bP);
  if (aM != bM) return (aM < bM) ? -1 : 1;
  if (aN != bN) return (aN < bN) ? -1 : 1;
  if (aP != bP) return (aP < bP) ? -1 : 1;
  return 0;
}

// Very lightweight JSON scraping (avoid extra lib deps): find value for a key
// Ex: key "tag_name" -> returns the unescaped string value without quotes.
static String jsonFindString(const String &body, const String &key, int from = 0) {
  String needle = String("\"") + key + "\":";
  int k = body.indexOf(needle, from);
  if (k == -1) return String();
  int q1 = body.indexOf('"', k + needle.length());
  if (q1 == -1) return String();
  int q2 = body.indexOf('"', q1 + 1);
  if (q2 == -1) return String();
  return body.substring(q1 + 1, q2);
}

// Attempt to find a browser_download_url for the configured asset name.
static String findAssetUrl(const String &json, const String &assetName) {
  // Strategy 1: locate the asset by name, then get the nearest browser_download_url after it
  String nameKey = String("\"name\":\"") + assetName + "\"";
  int np = json.indexOf(nameKey);
  if (np != -1) {
    // search forward for browser_download_url
    String key = "\"browser_download_url\":";
    int bp = json.indexOf(key, np);
    if (bp != -1) {
      int q1 = json.indexOf('"', bp + key.length());
      if (q1 != -1) {
        int q2 = json.indexOf('"', q1 + 1);
        if (q2 != -1) return json.substring(q1 + 1, q2);
      }
    }
  }

  // Strategy 2: construct the standard GitHub release download URL from tag
  String tag = jsonFindString(json, String("tag_name"));
  if (tag.length()) {
    String url = String("https://github.com/") + GITHUB_OWNER + "/" + GITHUB_REPO + "/releases/download/" + tag + "/" + assetName;
    return url;
  }
  return String();
}

static bool httpsGet(const String &url, String &outBody, int timeoutMs = 15000) {
  outBody = String();
  WiFiClientSecure client;
  client.setInsecure(); // NOTE: for simplicity; consider pinning GitHub cert for production
  HTTPClient http;
  http.setTimeout(timeoutMs);
  DBG("GET %s\n", url.c_str());
  if (!http.begin(client, url)) {
    DBGLN("http.begin failed");
    return false;
  }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "HiveSync-OTA");
  http.addHeader("Accept", "application/vnd.github+json");
  const char* hdrs[] = {"X-RateLimit-Remaining", "X-RateLimit-Used", "X-RateLimit-Reset"};
  http.collectHeaders(hdrs, 3);
  int code = http.GET();
  DBG("HTTP code: %d\n", code);
  if (http.hasHeader("X-RateLimit-Remaining")) {
    DBG("RateLimit remaining=%s used=%s reset=%s\n",
        http.header("X-RateLimit-Remaining").c_str(),
        http.header("X-RateLimit-Used").c_str(),
        http.header("X-RateLimit-Reset").c_str());
  }
  if (code != HTTP_CODE_OK) {
    DBGLN(String("Error: ") + http.errorToString(code));
    // Read body for diagnostics (often JSON with message)
    String errBody = http.getString();
    if (errBody.length()) DBG("Body: %s\n", errBody.substring(0, 200).c_str());
    http.end();
    return false;
  }
  outBody = http.getString();
  DBG("Body size: %d\n", outBody.length());
  http.end();
  return true;
}

static bool performOta(const String &url) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(30000);
  http.addHeader("User-Agent", "HiveSync-OTA");

  if (!http.begin(client, url)) {
    logLine(4, F("OTA: begin failed"), ST77XX_RED);
    DBGLN("OTA begin failed");
    return false;
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = http.GET();
  DBG("OTA GET code: %d\n", httpCode);
  if (httpCode != HTTP_CODE_OK) {
    logLine(4, String(F("HTTP ")) + httpCode, ST77XX_RED);
    DBGLN(String("OTA HTTP error: ") + http.errorToString(httpCode));
    http.end();
    return false;
  }

  int contentLen = http.getSize();
  if (contentLen <= 0) {
    logLine(4, F("No Content-Length"), ST77XX_RED);
    DBGLN("Missing or invalid Content-Length");
    http.end();
    return false;
  }

  // Setup progress callback to update display
  Update.onProgress([](size_t done, size_t total) {
    if (total == 0) return;
    int pct = (int)((done * 100) / total);
    UI::printLine(4, String(F("Updating: ")) + pct + "%", UI::COLOR_DEEP_TEAL);
  });

  DBG("Starting Update: size=%d bytes\n", contentLen);
  if (!Update.begin(contentLen)) {
    logLine(4, F("Update.begin failed"), ST77XX_RED);
    DBGLN(String("Update.begin error: ") + Update.errorString());
    http.end();
    return false;
  }

  WiFiClient * stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  DBG("Update.writeStream wrote=%u bytes\n", (unsigned)written);
  bool ok = true;
  if (written != (size_t)contentLen) {
    logLine(4, F("Write incomplete"), ST77XX_RED);
    DBG("Expected %d but wrote %u\n", contentLen, (unsigned)written);
    ok = false;
  }
  if (!Update.end()) {
    logLine(4, String(F("End err: ")) + Update.errorString(), ST77XX_RED);
    DBGLN(String("Update.end error: ") + Update.errorString());
    ok = false;
  }
  http.end();

  if (ok && Update.isFinished()) {
    logLine(5, F("Update OK, rebooting"), ST77XX_GREEN);
    delay(500);
    ESP.restart();
  } else if (ok) {
    logLine(5, F("Update not finished"), ST77XX_RED);
  }
  return ok;
}

static void checkAndUpdateOnce() {
  if (s_checkedThisBoot) return;
  s_checkedThisBoot = true;

  // Ensure configuration present
  if (String(GITHUB_OWNER).length() == 0 || String(GITHUB_REPO).length() == 0) {
    // Not configured; nothing to do
    DBGLN("GITHUB_OWNER/REPO not configured; skipping");
    return;
  }

  DBG("Current version: %s\n", FIRMWARE_VERSION);
  DBG("WiFi status=%d IP=%s RSSI=%d\n", (int)WiFi.status(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());

  String latestJson;
  String apiUrl = String("https://api.github.com/repos/") + GITHUB_OWNER + "/" + GITHUB_REPO + "/releases/latest";
  DBG("API URL: %s\n", apiUrl.c_str());
  if (!httpsGet(apiUrl, latestJson)) {
    DBGLN("Latest check failed");
    return;
  }

  String latestTag = jsonFindString(latestJson, String("tag_name"));
  if (latestTag.length() == 0) {
    DBGLN("JSON missing tag_name; body preview:");
    DBG("%s\n", latestJson.substring(0, 200).c_str());
    return;
  }

  String current = FIRMWARE_VERSION;
  int cmp = compareSemVer(current, latestTag);
  DBG("Compare: current=%s latest=%s -> %d\n", current.c_str(), latestTag.c_str(), cmp);
  if (cmp >= 0) {
    return;
  }

  String assetUrl = findAssetUrl(latestJson, FIRMWARE_ASSET);
  if (assetUrl.length() == 0) {
    DBGLN("Could not determine asset URL from JSON");
    return;
  }

  DBG("Asset URL: %s\n", assetUrl.c_str());
  performOta(assetUrl);
}

void loop() {
  // Only proceed if WiFi is connected
  if (!Provisioning::isConnected()) return;
  checkAndUpdateOnce();
}

} // namespace Updater
