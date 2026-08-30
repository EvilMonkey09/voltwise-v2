#include "mqtt_client.h"
#include "network_manager.h"
#include "nvs_config.h"
#include "telemetry.h"
#include "config.h"
#include <PubSubClient.h>
#include <WiFi.h>

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static String topic;
static bool mqttEnabled = false;

bool mqttIsConfigured() {
    return nvsGetMqttBroker().length() > 0;
}

bool mqttIsConnected() {
    return mqttEnabled && mqtt.connected();
}

void mqttInit() {
    String broker = nvsGetMqttBroker();
    mqttEnabled = broker.length() > 0;
    if (!mqttEnabled) {
        Serial.println("MQTT disabled (standalone mode)");
        return;
    }
    mqtt.setServer(broker.c_str(), nvsGetMqttPort());
    topic = "voltwise/telemetry/" + nvsGetDeviceId();
}

void mqttTask(void* param) {
    (void)param;
    unsigned long lastPub = 0;
    unsigned long lastReconnect = 0;
    for (;;) {
        if (!mqttEnabled) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!networkHasUplink()) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (!mqtt.connected()) {
            if (millis() - lastReconnect > 5000) {
                lastReconnect = millis();
                mqtt.connect(nvsGetDeviceId().c_str());
            }
        } else if (millis() - lastPub >= VOLTWISE_MQTT_INTERVAL_MS) {
            lastPub = millis();
            String payload = telemetryBuildJson(
                nvsGetDeviceId().c_str(),
                networkGetIp().c_str(),
                networkGetType().c_str());
            mqtt.publish(topic.c_str(), payload.c_str());
        }
        mqtt.loop();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
