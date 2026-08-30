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

void mqttInit() {
    String broker = nvsGetMqttBroker();
    if (broker.length() == 0) broker = "192.168.1.1";
    mqtt.setServer(broker.c_str(), nvsGetMqttPort());
    topic = "voltwise/telemetry/" + nvsGetDeviceId();
}

void mqttTask(void* param) {
    (void)param;
    unsigned long lastPub = 0;
    unsigned long lastReconnect = 0;
    for (;;) {
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
