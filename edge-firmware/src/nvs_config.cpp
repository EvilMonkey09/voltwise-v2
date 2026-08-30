#include "nvs_config.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>

static Preferences prefs;

void nvsConfigInit() {
    prefs.begin("voltwise", false);
    if (!prefs.isKey("device_id")) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char buf[40];
        snprintf(buf, sizeof(buf), "vw-%02x%02x%02x%02x-%lu",
                 mac[2], mac[3], mac[4], mac[5], millis());
        prefs.putString("device_id", buf);
    }
}

String nvsGetDeviceId() { return prefs.getString("device_id", "unknown"); }
String nvsGetDeviceName() { return prefs.getString("device_name", ""); }
String nvsGetMqttBroker() { return prefs.getString("mqtt_host", ""); }
uint16_t nvsGetMqttPort() { return prefs.getUShort("mqtt_port", 1883); }

void nvsSetMqttBroker(const String& host, uint16_t port) {
    prefs.putString("mqtt_host", host);
    prefs.putUShort("mqtt_port", port);
}

void nvsSetDeviceName(const String& name) { prefs.putString("device_name", name); }

int nvsGetWifiProfiles(WifiProfile* out, int maxCount) {
    int count = prefs.getInt("wifi_count", 0);
    if (count > maxCount) count = maxCount;
    for (int i = 0; i < count; i++) {
        String key = "wifi_" + String(i);
        String val = prefs.getString(key.c_str(), "");
        int sep = val.indexOf('\n');
        if (sep > 0) {
            out[i].ssid = val.substring(0, sep);
            out[i].password = val.substring(sep + 1);
            out[i].priority = i;
        }
    }
    return count;
}

bool nvsAddWifiProfile(const String& ssid, const String& password) {
    WifiProfile profiles[VOLTWISE_MAX_WIFI_PROFILES];
    int count = nvsGetWifiProfiles(profiles, VOLTWISE_MAX_WIFI_PROFILES);
    for (int i = 0; i < count; i++) {
        if (profiles[i].ssid == ssid) {
            String key = "wifi_" + String(i);
            prefs.putString(key.c_str(), ssid + "\n" + password);
            return true;
        }
    }
    if (count >= VOLTWISE_MAX_WIFI_PROFILES) return false;
    String key = "wifi_" + String(count);
    prefs.putString(key.c_str(), ssid + "\n" + password);
    prefs.putInt("wifi_count", count + 1);
    return true;
}

bool nvsDeleteWifiProfile(const String& ssid) {
    WifiProfile profiles[VOLTWISE_MAX_WIFI_PROFILES];
    int count = nvsGetWifiProfiles(profiles, VOLTWISE_MAX_WIFI_PROFILES);
    int writeIdx = 0;
    bool removed = false;
    for (int i = 0; i < count; i++) {
        if (profiles[i].ssid == ssid) { removed = true; continue; }
        String key = "wifi_" + String(writeIdx);
        prefs.putString(key.c_str(), profiles[i].ssid + "\n" + profiles[i].password);
        writeIdx++;
    }
    if (removed) prefs.putInt("wifi_count", writeIdx);
    return removed;
}
