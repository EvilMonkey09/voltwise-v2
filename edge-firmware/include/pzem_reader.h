#pragma once
#include "telemetry.h"
#include <Arduino.h>

#ifndef VOLTWISE_PZEM_SCAN_MAX
#define VOLTWISE_PZEM_SCAN_MAX 8
#endif

struct PzemFoundDevice {
    uint8_t address = 0;
    float voltage = 0;
};

struct PzemBusStatus {
    uint8_t expectedCount = 3;
    uint8_t expected[4] = {1, 2, 3, 4};
    PzemFoundDevice found[VOLTWISE_PZEM_SCAN_MAX];
    uint8_t foundCount = 0;
    bool ok = false;
    bool scanned = false;
    String profile;
};

void pzemInit();
void pzemTask(void* param);
bool pzemResetEnergy(int address);

void pzemPausePolling();
void pzemResumePolling();

PzemBusStatus pzemScan(uint8_t maxAddr = VOLTWISE_PZEM_SCAN_MAX);
PzemBusStatus pzemGetStatus(bool refresh = false);
bool pzemSetAddress(uint8_t newAddress, bool* verifiedOut = nullptr);
