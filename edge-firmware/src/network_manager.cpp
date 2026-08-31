#include "network_manager.h"
#include "nvs_config.h"
#include "config.h"
#include <WiFi.h>
#include <DNSServer.h>

#if defined(BOARD_WT32_ETH01)
#include <ETH.h>
#endif

static DNSServer dnsServer;
static bool apActive = false;
static bool hasUplink = false;
static unsigned long bootMs = 0;
static unsigned long offlineSince = 0;

static volatile bool scanRequested = false;
static volatile bool scanInProgress = false;
static unsigned long lastScanFinishedMs = 0;
static String scanResultJson = "{\"networks\":[],\"scanning\":false}";
static SemaphoreHandle_t scanMutex = nullptr;

static const unsigned long SCAN_MIN_INTERVAL_MS = 8000;

static String apSsid() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[32];
    snprintf(buf, sizeof(buf), VOLTWISE_AP_PREFIX "%02X%02X", mac[4], mac[5]);
    return String(buf);
}

static int wifiProfileCount() {
    WifiProfile profiles[VOLTWISE_MAX_WIFI_PROFILES];
    return nvsGetWifiProfiles(profiles, VOLTWISE_MAX_WIFI_PROFILES);
}

static unsigned long netWaitMs() {
    return wifiProfileCount() == 0 ? VOLTWISE_NET_WAIT_NO_PROFILE_MS : VOLTWISE_NET_WAIT_MS;
}

static bool ethConnected() {
#if defined(BOARD_WT32_ETH01)
    return ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0);
#else
    return false;
#endif
}

static bool wifiStaConnected() {
    return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

static bool tryWifiProfiles() {
    WifiProfile profiles[VOLTWISE_MAX_WIFI_PROFILES];
    int count = nvsGetWifiProfiles(profiles, VOLTWISE_MAX_WIFI_PROFILES);
    for (int i = 0; i < count; i++) {
        WiFi.begin(profiles[i].ssid.c_str(), profiles[i].password.c_str());
        unsigned long start = millis();
        while (millis() - start < 12000) {
            if (WiFi.status() == WL_CONNECTED) return true;
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    return false;
}

static void startCaptiveAp() {
    if (apActive) return;
    WiFi.mode(WIFI_AP_STA);
    String ssid = apSsid();
    WiFi.softAP(ssid.c_str());
    IPAddress gw = WiFi.softAPIP();
    dnsServer.start(53, "*", gw);
    apActive = true;
    Serial.printf("AP started: %s\n", ssid.c_str());
}

static void stopCaptiveAp() {
    if (!apActive) return;
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    apActive = false;
}

static void setScanJson(const String& json, bool scanning) {
    if (!scanMutex) return;
    if (xSemaphoreTake(scanMutex, pdMS_TO_TICKS(100))) {
        scanResultJson = json;
        scanInProgress = scanning;
        xSemaphoreGive(scanMutex);
    }
}

static String buildScanJson(int count) {
    String out = "{\"networks\":[";
    for (int i = 0; i < count; i++) {
        if (i) out += ',';
        String ssid = WiFi.SSID(i);
        ssid.replace("\\", "\\\\");
        ssid.replace("\"", "\\\"");
        out += "{\"ssid\":\"" + ssid + "\",\"signal\":" + String(WiFi.RSSI(i)) + "}";
    }
    out += "],\"scanning\":false}";
    return out;
}

static void processWifiScan() {
    if (scanRequested && !scanInProgress) {
        unsigned long sinceLast = millis() - lastScanFinishedMs;
        if (lastScanFinishedMs != 0 && sinceLast < SCAN_MIN_INTERVAL_MS) {
            scanRequested = false;
            return;
        }
        scanRequested = false;
        scanInProgress = true;
        setScanJson("{\"networks\":[],\"scanning\":true}", true);
        if (apActive) WiFi.mode(WIFI_AP_STA);
        WiFi.scanDelete();
        Serial.println("WiFi scan started");
        int started = WiFi.scanNetworks(true, true);
        if (started != WIFI_SCAN_RUNNING) {
            Serial.printf("WiFi scan finished immediately: %d\n", started);
            setScanJson(buildScanJson(started < 0 ? 0 : started), false);
            lastScanFinishedMs = millis();
            WiFi.scanDelete();
        }
        return;
    }

    if (!scanInProgress) return;

    int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return;
    if (count == WIFI_SCAN_FAILED) {
        Serial.println("WiFi scan failed");
        setScanJson("{\"networks\":[],\"scanning\":false}", false);
        lastScanFinishedMs = millis();
        WiFi.scanDelete();
        return;
    }
    Serial.printf("WiFi scan done: %d networks\n", count);
    setScanJson(buildScanJson(count), false);
    lastScanFinishedMs = millis();
    WiFi.scanDelete();
}

void networkInit() {
    bootMs = millis();
    scanMutex = xSemaphoreCreateMutex();
#if defined(BOARD_WT32_ETH01)
    ETH.begin();
#endif
    WiFi.mode(WIFI_STA);
    if (!ethConnected()) tryWifiProfiles();
    hasUplink = ethConnected() || wifiStaConnected();
}

void networkTask(void* param) {
    (void)param;
    for (;;) {
        bool uplink = ethConnected() || wifiStaConnected();
        if (uplink) {
            hasUplink = true;
            offlineSince = 0;
            stopCaptiveAp();
        } else {
            hasUplink = false;
            if (offlineSince == 0) offlineSince = millis();
            unsigned long offlineDur = millis() - offlineSince;
            unsigned long sinceBoot = millis() - bootMs;
            if (sinceBoot > netWaitMs() && offlineDur > VOLTWISE_OFFLINE_BEFORE_AP_MS) {
                startCaptiveAp();
            }
        }
        if (apActive) dnsServer.processNextRequest();
        processWifiScan();
        unsigned long delayMs = scanInProgress ? 200 : VOLTWISE_NET_POLL_MS;
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }
}

bool networkHasUplink() { return hasUplink; }
bool networkIsCaptivePortalActive() { return apActive; }

String networkGetIp() {
#if defined(BOARD_WT32_ETH01)
    if (ethConnected()) return ETH.localIP().toString();
#endif
    if (wifiStaConnected()) return WiFi.localIP().toString();
    if (apActive) return WiFi.softAPIP().toString();
    return "0.0.0.0";
}

String networkGetType() {
    if (ethConnected()) return "ethernet";
    if (wifiStaConnected()) return "wifi";
    return "wifi";
}

void networkForceSetupMode() {
    WiFi.disconnect(true);
    hasUplink = false;
    offlineSince = millis();
    startCaptiveAp();
    networkRequestWifiScan();
}

String networkApSsid() { return apActive ? apSsid() : String(); }

bool networkConnectWifiSync(const String& ssid, const String& password) {
    nvsAddWifiProfile(ssid, password);
    stopCaptiveAp();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    unsigned long start = millis();
    while (millis() - start < 15000) {
        if (WiFi.status() == WL_CONNECTED) {
            hasUplink = true;
            offlineSince = 0;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    startCaptiveAp();
    networkRequestWifiScan();
    return false;
}

void networkRequestWifiScan() {
    if (scanInProgress) return;
    if (lastScanFinishedMs != 0 && (millis() - lastScanFinishedMs) < SCAN_MIN_INTERVAL_MS) return;
    scanRequested = true;
}

String networkScanJson() {
    if (!scanMutex) return "{\"networks\":[],\"scanning\":true}";
    if (xSemaphoreTake(scanMutex, pdMS_TO_TICKS(100))) {
        String out = scanResultJson;
        bool scanning = scanInProgress;
        xSemaphoreGive(scanMutex);
        if (scanning && out.indexOf("\"scanning\":true") < 0) {
            return "{\"networks\":[],\"scanning\":true}";
        }
        return out;
    }
    return "{\"networks\":[],\"scanning\":true}";
}
