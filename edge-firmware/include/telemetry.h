#pragma once
#include <Arduino.h>

struct PhaseReading {
    bool valid = false;
    float voltage = 0;
    float current = 0;
    float power = 0;
    float energy = 0;
    float frequency = 0;
    float powerFactor = 0;
};

struct TelemetrySnapshot {
    PhaseReading phases[3];
    PhaseReading neutral;  // used when profile measures N (addr 4)
    float neutralCurrent = 0;
    bool neutralMeasured = false;
    bool simulation = false;
    unsigned long timestampMs = 0;
};

void telemetryInit();
void telemetryUpdate(const PhaseReading phases[3], const PhaseReading* neutralOrNull, bool simulation);
TelemetrySnapshot telemetryGetSnapshot();
String telemetryBuildJson(const char* deviceId, const char* ip, const char* networkType);
String telemetryBuildUdpJson(const char* deviceId, const char* ip, const char* networkType);
