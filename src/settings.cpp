#include "settings.h"
#include <Preferences.h>

static bool s_metric = false;  // imperial default
static WiFiCred s_wifi[WIFI_MAX_NETWORKS] = {};
static char s_notifTs[32] = "1970-01-01T00:00:00.000Z";

static const char* PREFS_NAMESPACE = "watch";
static const char* KEY_METRIC      = "metric";
static const char* KEY_NOTIF_TS    = "notif_ts";

// NVS keys for WiFi: ssid0..ssid9, pass0..pass9. Buffer is 8 bytes which
// fits a 4-char prefix + 2-digit index + null — good up to 99 slots if we
// ever need more.
static void wifiKey(const char* prefix, int idx, char* out) {
    snprintf(out, 8, "%s%d", prefix, idx);
}

void settings_load() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);  // read-only
    s_metric = prefs.getBool(KEY_METRIC, false);

    // Load last notification timestamp
    String ts = prefs.getString(KEY_NOTIF_TS, "");
    if (ts.length() > 0 && ts.length() < sizeof(s_notifTs)) {
        strncpy(s_notifTs, ts.c_str(), sizeof(s_notifTs) - 1);
        s_notifTs[sizeof(s_notifTs) - 1] = '\0';
    }

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
    Serial.printf("[settings] loaded: metric=%d  wifi_networks=%d  notif_ts=%s\n",
                  s_metric ? 1 : 0, count, s_notifTs);
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
    // All slots full — shift down (drop oldest at slot 0) and use the
    // last slot for the new entry. Wrapped in a block so the `last`
    // declaration doesn't get jumped over by earlier `goto save` paths
    // (C++ forbids jumping over initializations).
    {
        for (int i = 0; i < WIFI_MAX_NETWORKS - 1; i++) {
            s_wifi[i] = s_wifi[i + 1];
        }
        int last = WIFI_MAX_NETWORKS - 1;
        strncpy(s_wifi[last].ssid, ssid, sizeof(s_wifi[last].ssid) - 1);
        strncpy(s_wifi[last].pass, password, sizeof(s_wifi[last].pass) - 1);
        s_wifi[last].valid = true;
    }

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

void settings_removeWifi(int slot) {
    if (slot < 0 || slot >= WIFI_MAX_NETWORKS) return;
    if (!s_wifi[slot].valid) return;
    Serial.printf("[settings] forgetting wifi slot %d: %s\n",
                  slot, s_wifi[slot].ssid);
    s_wifi[slot].valid = false;
    s_wifi[slot].ssid[0] = '\0';
    s_wifi[slot].pass[0] = '\0';
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    char kssid[8], kpass[8];
    wifiKey("ssid", slot, kssid);
    wifiKey("pass", slot, kpass);
    prefs.remove(kssid);
    prefs.remove(kpass);
    prefs.end();
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

const char* settings_getLastNotifTimestamp() {
    return s_notifTs;
}

void settings_setLastNotifTimestamp(const char* ts) {
    if (!ts || !ts[0]) return;
    strncpy(s_notifTs, ts, sizeof(s_notifTs) - 1);
    s_notifTs[sizeof(s_notifTs) - 1] = '\0';
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString(KEY_NOTIF_TS, s_notifTs);
    prefs.end();
    Serial.printf("[settings] saved notif_ts=%s\n", s_notifTs);
}
