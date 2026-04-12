#include "settings.h"
#include <Preferences.h>

static bool s_metric = false;  // imperial default
static WiFiCred s_wifi[WIFI_MAX_NETWORKS] = {};

static const char* PREFS_NAMESPACE = "watch";
static const char* KEY_METRIC      = "metric";

// NVS keys for WiFi: ssid0/pass0, ssid1/pass1, ssid2/pass2
static void wifiKey(const char* prefix, int idx, char* out) {
    snprintf(out, 8, "%s%d", prefix, idx);
}

void settings_load() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);  // read-only
    s_metric = prefs.getBool(KEY_METRIC, false);

    // Load WiFi credentials
    for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
        char kssid[8], kpass[8];
        wifiKey("ssid", i, kssid);
        wifiKey("pass", i, kpass);
        String ssid = prefs.getString(kssid, "");
        String pass = prefs.getString(kpass, "");
        if (ssid.length() > 0) {
            strncpy(s_wifi[i].ssid, ssid.c_str(), sizeof(s_wifi[i].ssid) - 1);
            strncpy(s_wifi[i].pass, pass.c_str(), sizeof(s_wifi[i].pass) - 1);
            s_wifi[i].valid = true;
        } else {
            s_wifi[i].valid = false;
        }
    }
    prefs.end();

    int count = 0;
    for (int i = 0; i < WIFI_MAX_NETWORKS; i++) if (s_wifi[i].valid) count++;
    Serial.printf("[settings] loaded: metric=%d  wifi_networks=%d\n",
                  s_metric ? 1 : 0, count);
}

bool settings_isMetric() {
    return s_metric;
}

void settings_setMetric(bool metric) {
    if (s_metric == metric) return;
    s_metric = metric;
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putBool(KEY_METRIC, metric);
    prefs.end();
    Serial.printf("[settings] saved: metric=%d\n", metric ? 1 : 0);
}

const WiFiCred* settings_getWifiCreds() {
    return s_wifi;
}

void settings_addWifi(const char* ssid, const char* password) {
    // Check if this SSID already exists — update in place if so.
    for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
        if (s_wifi[i].valid && strcmp(s_wifi[i].ssid, ssid) == 0) {
            strncpy(s_wifi[i].pass, password, sizeof(s_wifi[i].pass) - 1);
            goto save;
        }
    }
    // Find the first empty slot.
    for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
        if (!s_wifi[i].valid) {
            strncpy(s_wifi[i].ssid, ssid, sizeof(s_wifi[i].ssid) - 1);
            strncpy(s_wifi[i].pass, password, sizeof(s_wifi[i].pass) - 1);
            s_wifi[i].valid = true;
            goto save;
        }
    }
    // All slots full — shift down (drop oldest at slot 0) and use slot 2.
    s_wifi[0] = s_wifi[1];
    s_wifi[1] = s_wifi[2];
    strncpy(s_wifi[2].ssid, ssid, sizeof(s_wifi[2].ssid) - 1);
    strncpy(s_wifi[2].pass, password, sizeof(s_wifi[2].pass) - 1);
    s_wifi[2].valid = true;

save:
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
        char kssid[8], kpass[8];
        wifiKey("ssid", i, kssid);
        wifiKey("pass", i, kpass);
        if (s_wifi[i].valid) {
            prefs.putString(kssid, s_wifi[i].ssid);
            prefs.putString(kpass, s_wifi[i].pass);
        } else {
            prefs.remove(kssid);
            prefs.remove(kpass);
        }
    }
    prefs.end();
    Serial.printf("[settings] saved wifi: %s\n", ssid);
}

void settings_clearAllWifi() {
    for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
        s_wifi[i].valid = false;
        s_wifi[i].ssid[0] = '\0';
        s_wifi[i].pass[0] = '\0';
    }
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
        char kssid[8], kpass[8];
        wifiKey("ssid", i, kssid);
        wifiKey("pass", i, kpass);
        prefs.remove(kssid);
        prefs.remove(kpass);
    }
    prefs.end();
    Serial.println("[settings] cleared all wifi credentials");
}
