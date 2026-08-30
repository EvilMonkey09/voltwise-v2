#pragma once
#include <Arduino.h>

void networkInit();
void networkTask(void* param);
bool networkHasUplink();
String networkGetIp();
String networkGetType();
bool networkIsCaptivePortalActive();
bool networkConnectWifi(const String& ssid, const String& password);
void networkForceSetupMode();
String networkScanJson();
String networkApSsid();
