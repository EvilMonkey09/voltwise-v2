#include "pzem_reader.h"
#include "telemetry.h"
#include "nvs_config.h"
#include "config.h"
#include <ModbusMaster.h>

static ModbusMaster node;
static PhaseReading latestPhases[3];
static PhaseReading latestNeutral;
static SemaphoreHandle_t readMutex;
static SemaphoreHandle_t busMutex;
static volatile bool pollPaused = false;
static PzemBusStatus cachedStatus;

#if defined(VOLTWISE_SIMULATION)
static float simEnergy[4] = {0, 0, 0, 0};
static unsigned long simLast = 0;

static PhaseReading simulate(int idx, float dt) {
    PhaseReading r;
    r.valid = true;
    float t = millis() / 1000.0f;
    float phase = idx * 2.094f;
    r.voltage = 230.0f + 4.0f * sinf(t / 42.0f + phase);
    if (idx == 3) {
        r.current = 0.4f + 0.2f * sinf(t / 40.0f);
    } else {
        r.current = (1.2f + 0.6f * (idx + 1)) * (0.85f + 0.15f * sinf(t / 55.0f + phase));
    }
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

static void flushSerial2() {
    while (Serial2.available()) Serial2.read();
}

static bool takeBus(TickType_t ticks = pdMS_TO_TICKS(2000)) {
    if (!busMutex) return false;
    return xSemaphoreTake(busMutex, ticks) == pdTRUE;
}

static void giveBus() {
    if (busMutex) xSemaphoreGive(busMutex);
}

static void fillExpected(PzemBusStatus& st) {
    st.profile = nvsGetSensorProfile();
    st.expectedCount = nvsSensorCount();
    for (uint8_t i = 0; i < st.expectedCount; i++) st.expected[i] = i + 1;
}

static bool expectedOk(const PzemBusStatus& st) {
    bool seen[5] = {false, false, false, false, false};
    for (uint8_t i = 0; i < st.foundCount; i++) {
        uint8_t a = st.found[i].address;
        if (a >= 1 && a <= 4) {
            if (seen[a]) return false;
            seen[a] = true;
        }
    }
    for (uint8_t i = 0; i < st.expectedCount; i++) {
        if (!seen[st.expected[i]]) return false;
    }
    // No unexpected addresses in 1..4 beyond profile
    for (uint8_t a = 1; a <= 4; a++) {
        bool wanted = false;
        for (uint8_t i = 0; i < st.expectedCount; i++) {
            if (st.expected[i] == a) { wanted = true; break; }
        }
        if (seen[a] && !wanted) return false;
    }
    return st.foundCount == st.expectedCount;
}

void pzemInit() {
    readMutex = xSemaphoreCreateMutex();
    busMutex = xSemaphoreCreateMutex();
    cachedStatus = PzemBusStatus();
    Serial2.begin(VOLTWISE_BAUD, SERIAL_8N1, VOLTWISE_UART_RX, VOLTWISE_UART_TX);
    node.begin(1, Serial2);
}

void pzemPausePolling() { pollPaused = true; }
void pzemResumePolling() { pollPaused = false; }

static bool readPzem(int address, PhaseReading& out) {
    node.begin(address, Serial2);
    uint8_t result = node.readInputRegisters(0x0000, 10);
    if (result != node.ku8MBSuccess) return false;
    uint32_t cur = ((uint32_t)node.getResponseBuffer(2) << 16) | node.getResponseBuffer(1);
    uint32_t pwr = ((uint32_t)node.getResponseBuffer(4) << 16) | node.getResponseBuffer(3);
    uint32_t en = ((uint32_t)node.getResponseBuffer(6) << 16) | node.getResponseBuffer(5);
    out.valid = true;
    out.voltage = node.getResponseBuffer(0) * 0.1f;
    out.current = cur * 0.001f;
    out.power = pwr * 0.1f;
    out.energy = en;
    out.frequency = node.getResponseBuffer(7) * 0.1f;
    out.powerFactor = node.getResponseBuffer(8) * 0.01f;
    return true;
}

bool pzemResetEnergy(int address) {
    pzemPausePolling();
    if (!takeBus()) {
        pzemResumePolling();
        return false;
    }
    uint8_t frame[4] = {(uint8_t)address, 0x42};
    uint16_t crc = modbusCrc(frame, 2);
    frame[2] = crc & 0xFF;
    frame[3] = (crc >> 8) & 0xFF;
    flushSerial2();
    Serial2.write(frame, 4);
    delay(500);
    giveBus();
    pzemResumePolling();
    return true;
}

static PzemBusStatus runScan(uint8_t maxAddr) {
    PzemBusStatus st;
    fillExpected(st);
    if (maxAddr < 1) maxAddr = 1;
    if (maxAddr > VOLTWISE_PZEM_SCAN_MAX) maxAddr = VOLTWISE_PZEM_SCAN_MAX;

#if defined(VOLTWISE_SIMULATION)
    for (uint8_t a = 1; a <= st.expectedCount && a <= maxAddr; a++) {
        st.found[st.foundCount].address = a;
        st.found[st.foundCount].voltage = 230.0f + a;
        st.foundCount++;
    }
#else
    pzemPausePolling();
    if (!takeBus(pdMS_TO_TICKS(3000))) {
        pzemResumePolling();
        return cachedStatus;
    }
    for (uint8_t a = 1; a <= maxAddr; a++) {
        PhaseReading r;
        r.valid = false;
        if (readPzem(a, r)) {
            if (st.foundCount < VOLTWISE_PZEM_SCAN_MAX) {
                st.found[st.foundCount].address = a;
                st.found[st.foundCount].voltage = r.voltage;
                st.foundCount++;
            }
        }
        delay(20);
    }
    giveBus();
    pzemResumePolling();
#endif
    st.scanned = true;
    st.ok = expectedOk(st);
    cachedStatus = st;
    return st;
}

PzemBusStatus pzemScan(uint8_t maxAddr) {
    return runScan(maxAddr);
}

PzemBusStatus pzemGetStatus(bool refresh) {
    if (refresh || !cachedStatus.scanned) {
        uint8_t need = nvsSensorCount();
        return runScan(need > 4 ? need : 4);
    }
    fillExpected(cachedStatus);
    cachedStatus.ok = expectedOk(cachedStatus);
    return cachedStatus;
}

bool pzemSetAddress(uint8_t newAddress, bool* verifiedOut) {
    if (verifiedOut) *verifiedOut = false;
    if (newAddress < 1 || newAddress > 247) return false;

#if defined(VOLTWISE_SIMULATION)
    if (verifiedOut) *verifiedOut = true;
    cachedStatus.scanned = false;
    return true;
#else
    pzemPausePolling();
    if (!takeBus(pdMS_TO_TICKS(3000))) {
        pzemResumePolling();
        return false;
    }

    uint8_t frame[8] = {
        0xF8, 0x06, 0x00, 0x02, 0x00, newAddress, 0x00, 0x00
    };
    uint16_t crc = modbusCrc(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;

    flushSerial2();
    Serial2.write(frame, 8);

    uint8_t resp[8];
    size_t got = 0;
    unsigned long start = millis();
    while (got < 8 && millis() - start < 800) {
        if (Serial2.available()) {
            resp[got++] = (uint8_t)Serial2.read();
        } else {
            delay(1);
        }
    }

    bool wroteOk = false;
    if (got >= 8) {
        if ((resp[0] == 0xF8 || resp[0] == newAddress) && resp[1] == 0x06
            && resp[2] == 0x00 && resp[3] == 0x02 && resp[5] == newAddress) {
            wroteOk = true;
        }
    }

    delay(200);
    PhaseReading verify;
    verify.valid = false;
    bool verified = readPzem(newAddress, verify);
    if (verifiedOut) *verifiedOut = verified;

    giveBus();
    pzemResumePolling();
    cachedStatus.scanned = false;
    return verified || wroteOk;
#endif
}

void pzemTask(void* param) {
    (void)param;
    for (;;) {
        if (pollPaused) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        PhaseReading batch[3];
        PhaseReading neutral;
        neutral.valid = false;
        bool measureN = nvsProfileMeasuresNeutral();

#if defined(VOLTWISE_SIMULATION)
        float dt = (millis() - simLast) / 1000.0f;
        simLast = millis();
        for (int i = 0; i < 3; i++) batch[i] = simulate(i, dt);
        if (measureN) neutral = simulate(3, dt);
#else
        if (!takeBus(pdMS_TO_TICKS(50))) {
            vTaskDelay(pdMS_TO_TICKS(VOLTWISE_PZEM_INTERVAL_MS));
            continue;
        }
        for (int i = 0; i < 3; i++) {
            batch[i].valid = false;
            readPzem(i + 1, batch[i]);
        }
        if (measureN) {
            readPzem(4, neutral);
        }
        giveBus();
#endif
        if (xSemaphoreTake(readMutex, pdMS_TO_TICKS(50))) {
            for (int i = 0; i < 3; i++) latestPhases[i] = batch[i];
            latestNeutral = neutral;
            xSemaphoreGive(readMutex);
        }
        telemetryUpdate(batch, measureN ? &neutral : nullptr,
#if defined(VOLTWISE_SIMULATION)
            true
#else
            false
#endif
        );
        vTaskDelay(pdMS_TO_TICKS(VOLTWISE_PZEM_INTERVAL_MS));
    }
}
