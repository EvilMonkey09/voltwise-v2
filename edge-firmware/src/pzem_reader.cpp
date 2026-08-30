#include "pzem_reader.h"
#include "telemetry.h"
#include "config.h"
#include <ModbusMaster.h>

static ModbusMaster node;
static PhaseReading latest[3];
static SemaphoreHandle_t readMutex;

#if defined(VOLTWISE_SIMULATION)
static float simEnergy[3] = {0, 0, 0};
static unsigned long simLast = 0;

static PhaseReading simulate(int idx, float dt) {
    PhaseReading r;
    r.valid = true;
    float t = millis() / 1000.0f;
    float phase = idx * 2.094f;
    r.voltage = 230.0f + 4.0f * sinf(t / 42.0f + phase);
    r.current = (1.2f + 0.6f * (idx + 1)) * (0.85f + 0.15f * sinf(t / 55.0f + phase));
    r.current = fmaxf(0.05f, r.current);
    r.powerFactor = 0.95f;
    r.power = r.voltage * r.current * r.powerFactor;
    r.frequency = 50.0f;
    simEnergy[idx] += r.power * (dt / 3600.0f);
    r.energy = simEnergy[idx];
    return r;
}
#endif

static uint16_t modbusCrc(uint8_t* frame, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t pos = 0; pos < len; pos++) {
        crc ^= frame[pos];
        for (int i = 0; i < 8; i++) {
            if (crc & 1) { crc >>= 1; crc ^= 0xA001; }
            else crc >>= 1;
        }
    }
    return crc;
}

void pzemInit() {
    readMutex = xSemaphoreCreateMutex();
    Serial2.begin(VOLTWISE_BAUD, SERIAL_8N1, VOLTWISE_UART_RX, VOLTWISE_UART_TX);
    node.begin(1, Serial2);
}

static bool readPzem(int address, PhaseReading& out) {
    node.begin(address, Serial2);
    uint8_t result = node.readInputRegisters(0x0000, 10);
    if (result != node.ku8MBSuccess) return false;
    uint16_t* v = node.getResponseBuffer();
    uint32_t cur = ((uint32_t)v[2] << 16) | v[1];
    uint32_t pwr = ((uint32_t)v[4] << 16) | v[3];
    uint32_t en = ((uint32_t)v[6] << 16) | v[5];
    out.valid = true;
    out.voltage = v[0] * 0.1f;
    out.current = cur * 0.001f;
    out.power = pwr * 0.1f;
    out.energy = en;
    out.frequency = v[7] * 0.1f;
    out.powerFactor = v[8] * 0.01f;
    return true;
}

bool pzemResetEnergy(int address) {
    uint8_t frame[4] = {(uint8_t)address, 0x42};
    uint16_t crc = modbusCrc(frame, 2);
    frame[2] = crc & 0xFF;
    frame[3] = (crc >> 8) & 0xFF;
    Serial2.write(frame, 4);
    delay(500);
    return true;
}

void pzemTask(void* param) {
    (void)param;
    const int addresses[] = {1, 2, 3};
    for (;;) {
        PhaseReading batch[3];
#if defined(VOLTWISE_SIMULATION)
        float dt = (millis() - simLast) / 1000.0f;
        simLast = millis();
        for (int i = 0; i < 3; i++) batch[i] = simulate(i, dt);
#else
        for (int i = 0; i < 3; i++) {
            batch[i].valid = false;
            if (readPzem(addresses[i], batch[i])) { /* ok */ }
        }
#endif
        if (xSemaphoreTake(readMutex, pdMS_TO_TICKS(50))) {
            for (int i = 0; i < 3; i++) latest[i] = batch[i];
            xSemaphoreGive(readMutex);
        }
        telemetryUpdate(latest, 
#if defined(VOLTWISE_SIMULATION)
            true
#else
            false
#endif
        );
        vTaskDelay(pdMS_TO_TICKS(VOLTWISE_PZEM_INTERVAL_MS));
    }
}
