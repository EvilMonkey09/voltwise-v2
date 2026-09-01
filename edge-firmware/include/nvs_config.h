#pragma once
#include <Arduino.h>

void nvsConfigInit();
String nvsGetDeviceId();
String nvsGetDeviceName();
String nvsGetMqttBroker();
uint16_t nvsGetMqttPort();
void nvsSetMqttBroker(const String& host, uint16_t port);
void nvsSetDeviceName(const String& name);

struct WifiProfile {
    String ssid;
    String password;
    int priority;
};

int nvsGetWifiProfiles(WifiProfile* out, int maxCount);
bool nvsAddWifiProfile(const String& ssid, const String& password);
bool nvsDeleteWifiProfile(const String& ssid);
bool nvsIsUsableMqttBroker(const String& host);
