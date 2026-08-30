#include "ota_update.h"
#include "config.h"
#include "network_manager.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

static unsigned long lastOtaCheckMs = 0;
static bool otaInProgress = false;

static int parseVersionPart(const char*& s) {
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    if (*s == '.') s++;
    return val;
}

static bool isNewerVersion(const char* latestTag, const char* current) {
    const char* la = latestTag;
    const char* ca = current;
    if (*la == 'v' || *la == 'V') la++;
    if (*ca == 'v' || *ca == 'V') ca++;
    for (int i = 0; i < 3; i++) {
        int lv = parseVersionPart(la);
        int cv = parseVersionPart(ca);
        if (lv > cv) return true;
        if (lv < cv) return false;
    }
    return false;
}

static String firmwareAssetName() {
    return String("firmware-") + VOLTWISE_BOARD_PROFILE + ".bin";
}

static bool fetchUpdateUrl(String& outUrl, String& outTag) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String("https://api.github.com/repos/") + VOLTWISE_GITHUB_REPO + "/releases/latest";
    if (!http.begin(client, url)) return false;
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("User-Agent", "VoltWise-Edge-OTA");
    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, http.getStream())) {
        http.end();
        return false;
    }
    http.end();
    outTag = doc["tag_name"].as<String>();
    String want = firmwareAssetName();
    JsonArray assets = doc["assets"].as<JsonArray>();
    for (JsonObject asset : assets) {
        String name = asset["name"].as<String>();
        if (name == want) {
            outUrl = asset["browser_download_url"].as<String>();
            return outUrl.length() > 0;
        }
    }
    return false;
}

static void performOta(const String& url) {
    if (otaInProgress) return;
    otaInProgress = true;
    Serial.printf("OTA: downloading %s\n", url.c_str());
    WiFiClientSecure client;
    client.setInsecure();
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    t_httpUpdate_return ret = httpUpdate.update(client, url);
    if (ret != HTTP_UPDATE_OK) {
        Serial.printf("OTA failed: %s\n", httpUpdate.getLastErrorString().c_str());
    }
    otaInProgress = false;
}

void otaInit() {
    lastOtaCheckMs = millis();
}

void otaCheck() {
    if (otaInProgress || !networkHasUplink()) return;
    unsigned long now = millis();
    if (now - lastOtaCheckMs < VOLTWISE_OTA_CHECK_INTERVAL_MS) return;
    lastOtaCheckMs = now;

    String url;
    String tag;
    if (!fetchUpdateUrl(url, tag)) return;
    if (!isNewerVersion(tag.c_str(), VOLTWISE_FW_VERSION)) return;

    Serial.printf("OTA: %s -> %s\n", VOLTWISE_FW_VERSION, tag.c_str());
    performOta(url);
}
