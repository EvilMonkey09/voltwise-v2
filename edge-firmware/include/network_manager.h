#pragma once
#include <Arduino.h>

void networkInit();
void networkTask(void* param);
bool networkHasUplink();
String networkGetIp();
String networkGetType();
bool networkIsCaptivePortalActive();
bool networkIsWifiConnecting();
bool networkIsWifiConnected();
String networkGetStaIp();
unsigned long networkApRemainingSeconds();
bool networkIsWifiConnecting();
bool networkIsWifiConnected();
String networkGetStaIp();
unsigned long networkApRemainingSeconds();
void networkQueueWifiConnect(const String& ssid, const String& password);
void networkForceSetupMode();
void networkRequestWifiScan();
String networkScanJson();
String networkApSsid();
