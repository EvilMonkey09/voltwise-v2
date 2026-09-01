#include "central_telemetry.h"
#include "config.h"
#include "network_manager.h"
#include "nvs_config.h"
#include "telemetry.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#if defined(BOARD_WT32_ETH01)
#include <ETH.h>
#endif

static WiFiUDP udp;
static bool mdnsStarted = false;
static bool lastBroadcastOk = false;
static unsigned long lastBroadcastMs = 0;

static IPAddress subnetBroadcast() {
    IPAddress ip;
    IPAddress mask;
#if defined(BOARD_WT32_ETH01)
    if (ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0)) {
        ip = ETH.localIP();
        mask = ETH.subnetMask();
    } else
#endif
    {
        ip = WiFi.localIP();
        mask = WiFi.subnetMask();
    }
    IPAddress broadcast;
    for (int i = 0; i < 4; i++) {
        broadcast[i] = (ip[i] & mask[i]) | (~mask[i] & 0xFF);
    }
    return broadcast;
}

static void ensureMdns() {
    if (mdnsStarted) return;

    String host = "voltwise-" + nvsGetDeviceId();
    host.replace("vw-", "");
    if (!MDNS.begin(host.c_str())) {
        Serial.println("mDNS start failed");
        return;
    }
    MDNS.addService("voltwise", "tcp", VOLTWISE_WEB_PORT);
    mdnsStarted = true;
    Serial.printf("mDNS: %s.local (_voltwise._tcp port %u)\n", host.c_str(), VOLTWISE_WEB_PORT);
}

void centralTelemetryInit() {
    udp.begin(0);
}

bool centralTelemetryActive() {
    return networkHasUplink() && (mdnsStarted || lastBroadcastOk);
}

void centralTelemetryTask(void* param) {
    (void)param;
    unsigned long lastPub = 0;
    for (;;) {
        if (!networkHasUplink()) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        ensureMdns();

        if (millis() - lastPub >= VOLTWISE_TELEMETRY_INTERVAL_MS) {
            lastPub = millis();
            String payload = telemetryBuildUdpJson(
                nvsGetDeviceId().c_str(),
                networkGetIp().c_str(),
                networkGetType().c_str());

            IPAddress dest = subnetBroadcast();
            if (udp.beginPacket(dest, VOLTWISE_TELEMETRY_UDP_PORT)) {
                udp.write(reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
                lastBroadcastOk = udp.endPacket();
                if (lastBroadcastOk) lastBroadcastMs = millis();
            } else {
                lastBroadcastOk = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
