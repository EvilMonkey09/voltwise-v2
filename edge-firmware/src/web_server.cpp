#include "web_server.h"
#include "network_manager.h"
#include "nvs_config.h"
#include "pzem_reader.h"
#include "telemetry.h"
#include "config.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static AsyncWebServer server(VOLTWISE_WEB_PORT);

static String portalRedirectHtml() {
    return "<!DOCTYPE html><html><head><meta charset=utf-8>"
           "<meta http-equiv=refresh content=\"0;url=/\">"
           "<title>VoltWise</title></head><body>"
           "<p>VoltWise WLAN-Einrichtung …</p><a href=/ >Weiter</a></body></html>";
}

void webServerInit() {
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", portalRedirectHtml()); });
    server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", portalRedirectHtml()); });
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/html", portalRedirectHtml()); });
    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/plain", "VoltWise captive\n"); });
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "text/plain", "VoltWise captive\n"); });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (networkIsCaptivePortalActive() && LittleFS.exists("/portal.html")) {
            r->send(LittleFS, "/portal.html", "text/html");
            return;
        }
        if (LittleFS.exists("/index.html")) r->send(LittleFS, "/index.html", "text/html");
        else r->send(200, "text/html", "<html><body><h1>VoltWise Edge</h1><p><a href=/api/data>API</a></p></body></html>");
    });

    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (LittleFS.exists("/settings.html")) r->send(LittleFS, "/settings.html", "text/html");
        else r->send(404);
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
        String out; serializeJson(doc, out);
        r->send(200, "application/json", out);
    });

    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument doc;
        doc["device_name"] = nvsGetDeviceName();
        doc["mqtt_broker"] = nvsGetMqttBroker();
        doc["mqtt_port"] = nvsGetMqttPort();
        String out; serializeJson(doc, out);
        r->send(200, "application/json", out);
    });

    server.on("/api/settings", HTTP_PUT, [](AsyncWebServerRequest* r) {}, NULL,
        [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) { r->send(400); return; }
            if (doc["device_name"]) nvsSetDeviceName(doc["device_name"].as<String>());
            if (doc["mqtt_broker"]) nvsSetMqttBroker(doc["mqtt_broker"].as<String>(), doc["mqtt_port"] | 1883);
            r->send(200, "application/json", "{\"ok\":true}");
        });

    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send(200, "application/json", networkScanJson());
    });

    server.on("/api/add-network", HTTP_POST, [](AsyncWebServerRequest* r) {}, NULL,
        [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) { r->send(400); return; }
            String ssid = doc["ssid"] | "";
            String pass = doc["password"] | "";
            bool ok = networkConnectWifi(ssid, pass);
            JsonDocument resp;
            resp["ok"] = ok;
            resp["mode"] = ok ? "connected" : "failed";
            resp["handoff"] = ok;
            String out; serializeJson(resp, out);
            r->send(200, "application/json", out);
        });

    server.on("/api/network/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument doc;
        doc["online"] = networkHasUplink();
        doc["available"] = true;
        doc["captive_portal"] = networkIsCaptivePortalActive();
        doc["ap_ssid"] = networkApSsid();
        doc["ip"] = networkGetIp();
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

    server.on("/api/update/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument doc;
        doc["current"] = VOLTWISE_FW_VERSION;
        doc["update_available"] = false;
        String out; serializeJson(doc, out);
        r->send(200, "application/json", out);
    });

    server.onNotFound([](AsyncWebServerRequest* r) {
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
