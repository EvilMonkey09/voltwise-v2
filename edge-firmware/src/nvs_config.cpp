#include "nvs_config.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>

static Preferences prefs;

static bool validProfile(const String& p) {
    return p == VOLTWISE_PROFILE_SENSORS_3 || p == VOLTWISE_PROFILE_SENSORS_4;
}

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
void nvsSetDeviceName(const String& name) { prefs.putString("device_name", name); }

bool nvsIsSetupDone() { return prefs.getBool("setup_done", false); }
void nvsSetSetupDone(bool done) { prefs.putBool("setup_done", done); }

String nvsGetSensorProfile() {
    String p = prefs.getString("sensor_profile", VOLTWISE_PROFILE_SENSORS_3);
    if (!validProfile(p)) return VOLTWISE_PROFILE_SENSORS_3;
    return p;
}

void nvsSetSensorProfile(const String& profile) {
    if (validProfile(profile)) prefs.putString("sensor_profile", profile);
}

uint8_t nvsSensorCount() {
    return nvsGetSensorProfile() == VOLTWISE_PROFILE_SENSORS_4 ? 4 : 3;
}

bool nvsProfileMeasuresNeutral() {
    return nvsGetSensorProfile() == VOLTWISE_PROFILE_SENSORS_4;
}

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

void nvsFactoryReset() {
    String id = nvsGetDeviceId();
    prefs.clear();
    prefs.putString("device_id", id);
    prefs.putBool("setup_done", false);
    prefs.putString("sensor_profile", VOLTWISE_PROFILE_SENSORS_3);
    prefs.putInt("wifi_count", 0);
}
