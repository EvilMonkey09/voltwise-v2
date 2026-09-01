#include "telemetry.h"
#include "nvs_config.h"
#include <ArduinoJson.h>
#include <math.h>

static TelemetrySnapshot snapshot;
static SemaphoreHandle_t snapMutex;

void telemetryInit() {
    snapMutex = xSemaphoreCreateMutex();
}

static float calcNeutral(float i1, float i2, float i3) {
    float val = (i1*i1 + i2*i2 + i3*i3) - (i1*i2 + i2*i3 + i3*i1);
    return sqrtf(fmaxf(0.0f, val));
}

void telemetryUpdate(const PhaseReading readings[3], bool simulation) {
    if (xSemaphoreTake(snapMutex, pdMS_TO_TICKS(50))) {
        for (int i = 0; i < 3; i++) snapshot.phases[i] = readings[i];
        snapshot.simulation = simulation;
        snapshot.timestampMs = millis();
        if (readings[0].valid && readings[1].valid && readings[2].valid) {
            snapshot.neutralCurrent = calcNeutral(readings[0].current, readings[1].current, readings[2].current);
        }
        xSemaphoreGive(snapMutex);
    }
}

TelemetrySnapshot telemetryGetSnapshot() {
    TelemetrySnapshot copy;
    if (xSemaphoreTake(snapMutex, pdMS_TO_TICKS(50))) {
        copy = snapshot;
        xSemaphoreGive(snapMutex);
    }
    return copy;
}

String telemetryBuildJson(const char* deviceId, const char* ip, const char* networkType) {
    TelemetrySnapshot snap = telemetryGetSnapshot();
    JsonDocument doc;
    doc["device_id"] = deviceId;
    doc["timestamp"] = millis() / 1000.0;
    JsonArray phases = doc["phases"].to<JsonArray>();
    const char* labels[] = {"L1", "L2", "L3"};
    for (int i = 0; i < 3; i++) {
        if (!snap.phases[i].valid) continue;
        JsonObject p = phases.add<JsonObject>();
        p["label"] = labels[i];
        p["voltage"] = roundf(snap.phases[i].voltage * 10) / 10.0;
        p["current"] = roundf(snap.phases[i].current * 1000) / 1000.0;
        p["power"] = roundf(snap.phases[i].power * 10) / 10.0;
        p["energy"] = snap.phases[i].energy;
        p["frequency"] = snap.phases[i].frequency;
        p["power_factor"] = snap.phases[i].powerFactor;
    }
    doc["neutral_current_a"] = roundf(snap.neutralCurrent * 1000) / 1000.0;
    JsonObject sys = doc["system"].to<JsonObject>();
    sys["uptime_s"] = millis() / 1000;
    sys["ip"] = ip;
    sys["network_type"] = networkType;
    sys["simulation"] = snap.simulation;
    String deviceName = nvsGetDeviceName();
    if (deviceName.length() > 0) {
        sys["device_name"] = deviceName;
    }
    String out;
    serializeJson(doc, out);
    return out;
}

String telemetryBuildUdpJson(const char* deviceId, const char* ip, const char* networkType) {
    String base = telemetryBuildJson(deviceId, ip, networkType);
    JsonDocument doc;
    if (deserializeJson(doc, base)) return base;
    doc["magic"] = "voltwise";
    String out;
    serializeJson(doc, out);
    return out;
}
