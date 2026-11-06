#include "web_server.h"
#include "network_manager.h"
#include "nvs_config.h"
#include "pzem_reader.h"
#include "telemetry.h"
#include "central_telemetry.h"
#include "config.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <math.h>
#include <esp_system.h>

static AsyncWebServer server(VOLTWISE_WEB_PORT);

static String portalRedirectHtml() {
    return "<!DOCTYPE html><html><head><meta charset=utf-8>"
           "<meta http-equiv=refresh content=\"0;url=/\">"
           "<title>VoltWise</title></head><body>"
           "<p>VoltWise WLAN-Einrichtung …</p><a href=/ >Weiter</a></body></html>";
}

static bool sendLittleFs(AsyncWebServerRequest* r, const char* path, const char* contentType) {
    if (!LittleFS.exists(path)) return false;
    r->send(LittleFS, path, contentType);
    return true;
}

static bool isStaticAssetPath(const String& url) {
    return url.endsWith(".css") || url.endsWith(".js") || url.endsWith(".ico")
        || url.endsWith(".svg") || url.endsWith(".png") || url.endsWith(".woff2");
}

static String pzemStatusJson(const PzemBusStatus& st) {
    JsonDocument doc;
    doc["ok"] = st.ok;
    doc["scanned"] = st.scanned;
    doc["profile"] = st.profile.length() ? st.profile : nvsGetSensorProfile();
    doc["sensor_count"] = st.expectedCount;
    JsonArray expected = doc["expected"].to<JsonArray>();
    for (uint8_t i = 0; i < st.expectedCount; i++) expected.add(st.expected[i]);
    JsonArray found = doc["found"].to<JsonArray>();
    for (uint8_t i = 0; i < st.foundCount; i++) {
        JsonObject e = found.add<JsonObject>();
        e["address"] = st.found[i].address;
        e["voltage"] = roundf(st.found[i].voltage * 10) / 10.0;
    }
    String out;
    serializeJson(doc, out);
    return out;
}

void webServerInit() {
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", portalRedirectHtml()); });
    server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", portalRedirectHtml()); });
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", portalRedirectHtml()); });
    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/plain", "VoltWise captive\n"); });
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/plain", "VoltWise captive\n"); });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        // Captive portal: WiFi only
        if (networkIsCaptivePortalActive() && !networkHasUplink()) {
            if (LittleFS.exists("/portal.html")) {
                r->send(LittleFS, "/portal.html", "text/html");
                return;
            }
        }
        // First-time device setup after network is up
        if (!nvsIsSetupDone() && LittleFS.exists("/setup.html")) {
            r->send(LittleFS, "/setup.html", "text/html");
            return;
        }
        if (LittleFS.exists("/index.html")) r->send(LittleFS, "/index.html", "text/html");
        else r->send(200, "text/html", "<html><body><h1>VoltWise Edge</h1><p><a href=/api/data>API</a></p></body></html>");
    });

    server.on("/setup", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (LittleFS.exists("/setup.html")) r->send(LittleFS, "/setup.html", "text/html");
        else r->send(404);
    });

    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (LittleFS.exists("/settings.html")) r->send(LittleFS, "/settings.html", "text/html");
        else r->send(404);
    });

    server.on("/voltwise.css", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (!sendLittleFs(r, "/voltwise.css", "text/css")) r->send(404);
    });

    server.on("/portal.html", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (!sendLittleFs(r, "/portal.html", "text/html")) r->send(404);
    });

    server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest* r) {
        String json = telemetryBuildJson(nvsGetDeviceId().c_str(), networkGetIp().c_str(), networkGetType().c_str());
        r->send(200, "application/json", json);
    });

    server.on("/api/node/info", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument doc;
        doc["node_id"] = nvsGetDeviceId();
        doc["node_name"] = nvsGetDeviceName();
        doc["display_name"] = nvsGetDeviceName();
        doc["version"] = VOLTWISE_FW_VERSION;
        doc["ip"] = networkGetIp();
        doc["simulation"] = false;
        doc["setup_done"] = nvsIsSetupDone();
        doc["sensor_profile"] = nvsGetSensorProfile();
        String out; serializeJson(doc, out);
        r->send(200, "application/json", out);
    });

    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument doc;
        doc["device_name"] = nvsGetDeviceName();
        doc["sensor_profile"] = nvsGetSensorProfile();
        doc["setup_done"] = nvsIsSetupDone();
        String out; serializeJson(doc, out);
        r->send(200, "application/json", out);
    });

    server.on("/api/settings", HTTP_PUT, [](AsyncWebServerRequest* r) {}, NULL,
        [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) { r->send(400); return; }
            if (doc["device_name"]) nvsSetDeviceName(doc["device_name"].as<String>());
            if (doc["sensor_profile"]) {
                String p = doc["sensor_profile"].as<String>();
                nvsSetSensorProfile(p);
            }
            r->send(200, "application/json", "{\"ok\":true}");
        });

    server.on("/api/setup/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        PzemBusStatus st = pzemGetStatus(false);
        JsonDocument doc;
        doc["setup_done"] = nvsIsSetupDone();
        doc["device_name"] = nvsGetDeviceName();
        doc["sensor_profile"] = nvsGetSensorProfile();
        doc["has_uplink"] = networkHasUplink();
        doc["network_type"] = networkGetType();
        doc["ip"] = networkGetIp();
        doc["pzem_ok"] = st.ok;
        doc["pzem_scanned"] = st.scanned;
        String out; serializeJson(doc, out);
        r->send(200, "application/json", out);
    });

    server.on("/api/setup/complete", HTTP_POST, [](AsyncWebServerRequest* r) {
        PzemBusStatus st = pzemGetStatus(true);
        if (!st.ok) {
            r->send(400, "application/json", "{\"ok\":false,\"error\":\"pzem_incomplete\"}");
            return;
        }
        nvsSetSetupDone(true);
        r->send(200, "application/json", "{\"ok\":true,\"setup_done\":true}");
    });

    server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest* r) {
        nvsFactoryReset();
        r->send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
        delay(300);
        ESP.restart();
    });

    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (r->hasParam("refresh")) networkRequestWifiScan();
        r->send(200, "application/json", networkScanJson());
    });

    server.on("/api/add-network", HTTP_POST, [](AsyncWebServerRequest* r) {}, NULL,
        [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) { r->send(400); return; }
            String ssid = doc["ssid"] | "";
            String pass = doc["password"] | "";
            if (ssid.isEmpty()) {
                r->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_ssid\"}");
                return;
            }
            r->send(200, "application/json", "{\"ok\":true,\"connecting\":true,\"handoff\":true}");
            networkQueueWifiConnect(ssid, pass);
        });

    server.on("/api/network/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument doc;
        doc["online"] = networkHasUplink();
        doc["available"] = true;
        doc["captive_portal"] = networkIsCaptivePortalActive();
        doc["ap_ssid"] = networkApSsid();
        doc["ip"] = networkGetIp();
        doc["sta_ip"] = networkGetStaIp();
        doc["wifi_connected"] = networkIsWifiConnected();
        doc["wifi_connecting"] = networkIsWifiConnecting();
        doc["ap_remaining_s"] = networkApRemainingSeconds();
        doc["setup_complete"] = nvsIsSetupDone();
        doc["central_active"] = centralTelemetryActive();
        doc["mode"] = centralTelemetryActive() ? "central" : "standalone";
        String out; serializeJson(doc, out);
        r->send(200, "application/json", out);
    });

    server.on("/api/network/setup-mode", HTTP_POST, [](AsyncWebServerRequest* r) {
        networkForceSetupMode();
        JsonDocument doc;
        doc["ok"] = true;
        doc["ap_ssid"] = networkApSsid();
        doc["ip"] = networkGetIp();
        String out; serializeJson(doc, out);
        r->send(200, "application/json", out);
    });

    server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest* r) {}, NULL,
        [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            int addr = 1;
            if (!deserializeJson(doc, data, len)) addr = doc["address"] | 1;
            pzemResetEnergy(addr);
            r->send(200, "application/json", "{\"ok\":true}");
        });

    server.on("/api/pzem/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        bool refresh = r->hasParam("refresh");
        r->send(200, "application/json", pzemStatusJson(pzemGetStatus(refresh)));
    });

    server.on("/api/pzem/scan", HTTP_POST, [](AsyncWebServerRequest* r) {
        uint8_t maxAddr = VOLTWISE_PZEM_SCAN_MAX;
        if (r->hasParam("max")) {
            int v = r->getParam("max")->value().toInt();
            if (v > 0 && v <= VOLTWISE_PZEM_SCAN_MAX) maxAddr = (uint8_t)v;
        }
        r->send(200, "application/json", pzemStatusJson(pzemScan(maxAddr)));
    });

    server.on("/api/pzem/set-address", HTTP_POST, [](AsyncWebServerRequest* r) {}, NULL,
        [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                r->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_json\"}");
                return;
            }
            int addr = doc["address"] | 0;
            if (addr < 1 || addr > 247) {
                r->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_address\"}");
                return;
            }
            bool verified = false;
            bool ok = pzemSetAddress((uint8_t)addr, &verified);
            JsonDocument out;
            out["ok"] = ok;
            out["address"] = addr;
            out["verified"] = verified;
            if (!ok) out["error"] = "set_failed";
            String s; serializeJson(out, s);
            r->send(ok ? 200 : 500, "application/json", s);
        });

    server.on("/api/update/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument doc;
        doc["current"] = VOLTWISE_FW_VERSION;
        doc["update_available"] = false;
        String out; serializeJson(doc, out);
        r->send(200, "application/json", out);
    });

    server.onNotFound([](AsyncWebServerRequest* r) {
        String url = r->url();
        if (isStaticAssetPath(url) && LittleFS.exists(url)) {
            r->send(LittleFS, url);
            return;
        }
        if (networkIsCaptivePortalActive()) {
            r->redirect("/");
            return;
        }
        r->send(404, "text/plain", "Not found");
    });

    server.serveStatic("/ui/", LittleFS, "/").setDefaultFile("index.html");
}

void webServerStart() {
    if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
    server.begin();
}
