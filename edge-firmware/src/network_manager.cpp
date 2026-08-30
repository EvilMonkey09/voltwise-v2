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
            delay(200);
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

void networkInit() {
    bootMs = millis();
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
        vTaskDelay(pdMS_TO_TICKS(VOLTWISE_NET_POLL_MS));
    }
}

bool networkHasUplink() { return hasUplink; }
bool networkIsCaptivePortalActive() { return apActive; }

String networkGetIp() {
    if (ethConnected()) return ETH.localIP().toString();
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
}

String networkApSsid() { return apActive ? apSsid() : String(); }

bool networkConnectWifi(const String& ssid, const String& password) {
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
        delay(200);
    }
    return false;
}

String networkScanJson() {
    int n = WiFi.scanNetworks();
    String out = "{\"networks\":[";
    for (int i = 0; i < n; i++) {
        if (i) out += ',';
        out += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"signal\":" + String(WiFi.RSSI(i)) + "}";
    }
    out += "]}";
    return out;
}
