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
    float val = (i1 * i1 + i2 * i2 + i3 * i3) - (i1 * i2 + i2 * i3 + i3 * i1);
    return sqrtf(fmaxf(0.0f, val));
}

void telemetryUpdate(const PhaseReading phases[3], const PhaseReading* neutralOrNull, bool simulation) {
    if (xSemaphoreTake(snapMutex, pdMS_TO_TICKS(50))) {
        for (int i = 0; i < 3; i++) snapshot.phases[i] = phases[i];
        snapshot.simulation = simulation;
        snapshot.timestampMs = millis();
        if (neutralOrNull && neutralOrNull->valid) {
            snapshot.neutral = *neutralOrNull;
            snapshot.neutralMeasured = true;
            snapshot.neutralCurrent = neutralOrNull->current;
        } else {
            snapshot.neutral = PhaseReading();
            snapshot.neutralMeasured = false;
            if (phases[0].valid && phases[1].valid && phases[2].valid) {
                snapshot.neutralCurrent = calcNeutral(phases[0].current, phases[1].current, phases[2].current);
            } else {
                snapshot.neutralCurrent = 0;
            }
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
    doc["neutral_source"] = snap.neutralMeasured ? "measured" : "calculated";
    doc["sensor_profile"] = nvsGetSensorProfile();
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
