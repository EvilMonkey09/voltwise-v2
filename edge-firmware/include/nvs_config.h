#pragma once
#include <Arduino.h>

#define VOLTWISE_PROFILE_SENSORS_3 "sensors_3"
#define VOLTWISE_PROFILE_SENSORS_4 "sensors_4"

void nvsConfigInit();
String nvsGetDeviceId();
String nvsGetDeviceName();
void nvsSetDeviceName(const String& name);

bool nvsIsSetupDone();
void nvsSetSetupDone(bool done);

/** sensors_3 | sensors_4 */
String nvsGetSensorProfile();
void nvsSetSensorProfile(const String& profile);
uint8_t nvsSensorCount();
bool nvsProfileMeasuresNeutral();

struct WifiProfile {
    String ssid;
    String password;
    int priority;
};

int nvsGetWifiProfiles(WifiProfile* out, int maxCount);
bool nvsAddWifiProfile(const String& ssid, const String& password);
bool nvsDeleteWifiProfile(const String& ssid);

/** Clears WiFi, name, setup_done, profile; keeps device_id. Caller should restart. */
void nvsFactoryReset();
