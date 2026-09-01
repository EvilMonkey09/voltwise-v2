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
static volatile bool connectInProgress = false;
static volatile bool connectPending = false;
static String pendingConnectSsid;
static String pendingConnectPassword;
static unsigned long lastScanFinishedMs = 0;
static String scanResultJson = "{\"networks\":[],\"scanning\":false}";
static SemaphoreHandle_t scanMutex = nullptr;
static unsigned long apGraceUntilMs = 0;

static const unsigned long SCAN_MIN_INTERVAL_MS = 8000;

static void processWifiConnect();

void networkRequestWifiScan();

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
    networkRequestWifiScan();
}

static void stopCaptiveAp() {
    if (!apActive) return;
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    apActive = false;
    Serial.println("AP stopped");
}

static bool shouldKeepApOpen() {
    return apGraceUntilMs != 0 && millis() < apGraceUntilMs;
}

static void scheduleApGracePeriod() {
    apGraceUntilMs = millis() + VOLTWISE_AP_GRACE_AFTER_CONNECT_MS;
    Serial.printf("AP grace period: %lu s\n", VOLTWISE_AP_GRACE_AFTER_CONNECT_MS / 1000UL);
}

static void maybeStopCaptiveAp() {
    if (!apActive) return;
    if (shouldKeepApOpen()) return;
    stopCaptiveAp();
    apGraceUntilMs = 0;
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

static int runWifiScanWithWait() {
    const int maxAttempts = 3;
    for (int attempt = 0; attempt < maxAttempts; attempt++) {
        if (attempt > 0) {
            Serial.printf("WiFi scan retry %d\n", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        WiFi.scanDelete();
        vTaskDelay(pdMS_TO_TICKS(300));

        wifi_mode_t mode = WiFi.getMode();
        if (mode != WIFI_AP_STA && mode != WIFI_STA) {
            WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        // Sync scan in network task — safe (does not block async_tcp / web server).
        Serial.println("WiFi scan started");
        int count = WiFi.scanNetworks(false, false);
        if (count >= 0) {
            Serial.printf("WiFi scan done: %d networks\n", count);
            return count;
        }

        Serial.printf("WiFi scan failed (attempt %d): %d\n", attempt + 1, count);

        // Fallback: async without hidden SSIDs, keep DNS alive while waiting.
        int started = WiFi.scanNetworks(true, false);
        if (started == WIFI_SCAN_RUNNING) {
            unsigned long deadline = millis() + 12000;
            while (millis() < deadline) {
                if (apActive) dnsServer.processNextRequest();
                count = WiFi.scanComplete();
                if (count != WIFI_SCAN_RUNNING) break;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            if (count >= 0) {
                Serial.printf("WiFi scan done: %d networks\n", count);
                return count;
            }
            Serial.printf("WiFi async scan failed: %d\n", count);
        } else if (started >= 0) {
            Serial.printf("WiFi scan done: %d networks\n", started);
            return started;
        }
        WiFi.scanDelete();
    }
    return WIFI_SCAN_FAILED;
}

static void processWifiScan() {
    if (!scanRequested || scanInProgress) return;

    unsigned long sinceLast = millis() - lastScanFinishedMs;
    if (lastScanFinishedMs != 0 && sinceLast < SCAN_MIN_INTERVAL_MS) {
        scanRequested = false;
        return;
    }

    scanRequested = false;
    scanInProgress = true;
    setScanJson("{\"networks\":[],\"scanning\":true}", true);

    int count = runWifiScanWithWait();
    if (count >= 0) {
        setScanJson(buildScanJson(count), false);
    } else {
        Serial.println("WiFi scan failed");
        setScanJson("{\"networks\":[],\"scanning\":false,\"error\":\"scan_failed\"}", false);
    }

    WiFi.scanDelete();
    lastScanFinishedMs = millis();
    scanInProgress = false;
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
            maybeStopCaptiveAp();
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
        processWifiConnect();
        if (!connectInProgress && !connectPending) processWifiScan();
        vTaskDelay(pdMS_TO_TICKS(VOLTWISE_NET_POLL_MS));
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

bool networkIsWifiConnecting() { return connectInProgress || connectPending; }

bool networkIsWifiConnected() { return wifiStaConnected(); }

String networkGetStaIp() {
    if (wifiStaConnected()) return WiFi.localIP().toString();
    return "";
}

unsigned long networkApRemainingSeconds() {
    if (!shouldKeepApOpen()) return 0;
    return (apGraceUntilMs - millis() + 999) / 1000;
}

static void processWifiConnect() {
    if (!connectPending || connectInProgress || scanInProgress) return;

    connectPending = false;
    connectInProgress = true;

    const String ssid = pendingConnectSsid;
    const String password = pendingConnectPassword;
    nvsAddWifiProfile(ssid, password);

    if (!apActive) {
        WiFi.mode(WIFI_AP_STA);
        startCaptiveAp();
    } else {
        WiFi.mode(WIFI_AP_STA);
    }

    WiFi.disconnect(false, false);
    vTaskDelay(pdMS_TO_TICKS(200));
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.printf("WiFi connecting to '%s'…\n", ssid.c_str());

    unsigned long start = millis();
    bool connected = false;
    while (millis() - start < 20000) {
        if (wifiStaConnected()) {
            hasUplink = true;
            offlineSince = 0;
            connected = true;
            Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
            scheduleApGracePeriod();
            break;
        }
        if (apActive) dnsServer.processNextRequest();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (!connected) {
        hasUplink = false;
        Serial.println("WiFi connect failed — hotspot stays active");
        if (!apActive) startCaptiveAp();
        networkRequestWifiScan();
    }

    connectInProgress = false;
}

void networkQueueWifiConnect(const String& ssid, const String& password) {
    pendingConnectSsid = ssid;
    pendingConnectPassword = password;
    connectPending = true;
}

void networkRequestWifiScan() {
    if (scanInProgress || connectInProgress || connectPending) return;
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
