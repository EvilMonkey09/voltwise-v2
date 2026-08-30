#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "nvs_config.h"
#include "pzem_reader.h"
#include "mqtt_client.h"
#include "network_manager.h"
#include "web_server.h"
#include "telemetry.h"
#include "ota_update.h"

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.printf("VoltWise Edge %s\n", VOLTWISE_FW_VERSION);

    nvsConfigInit();
    telemetryInit();
    pzemInit();
    networkInit();
    webServerInit();
    webServerStart();
    mqttInit();
    otaInit();

    xTaskCreatePinnedToCore(pzemTask, "pzem", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(mqttTask, "mqtt", 6144, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(networkTask, "network", 6144, NULL, 2, NULL, 0);
}

void loop() {
    otaCheck();
    delay(1000);
}
