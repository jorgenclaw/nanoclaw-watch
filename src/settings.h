#pragma once
#include <Arduino.h>

// =============================================================================
// Persistent app settings (NVS-backed via Arduino Preferences library)
// =============================================================================
//
// Tiny key/value store backed by ESP32 NVS flash. Survives reboots and
// reflashes. Used so user-toggleable preferences (like the weather unit
// system) don't need to live in `config.h` — the user can flip them from
// the watch UI without ever touching a terminal.
//
// Add new prefs by extending the loader and adding setter/getter pairs;
// keep the namespace short ("watch") because NVS namespace names are
// limited to 15 chars.

// Call once from setup() to populate the in-memory cache from flash.
void settings_load();

// Imperial vs metric units (affects weather temps + wind speed).
//   true  = metric (°C + km/h)
//   false = imperial (°F + mph)
// Default on first boot is imperial.
bool settings_isMetric();
void settings_setMetric(bool metric);

// --- Multi-network WiFi credentials (up to 10 saved networks) ---------------
//
// Privacy note: every saved network is actively probed by the ESP32 on
// reconnect, which broadcasts the SSID over the air in the clear. A wrist
// watch can't hide behind a VPN — anyone within radio range of the watch can
// passively log its probe list. So we expose a "forget" button in the UI
// (see ui_showWifiManager) to let users purge networks they no longer visit.

static const int WIFI_MAX_NETWORKS = 10;

struct WiFiCred {
    char ssid[64];
    char pass[64];
    bool valid;   // true if this slot has credentials
};

// Returns the array of WIFI_MAX_NETWORKS credential slots (loaded from NVS).
const WiFiCred* settings_getWifiCreds();

// Save a new network. Writes to the first empty slot; if all slots are full,
// overwrites the oldest (slot 0, shifting the others down).
void settings_addWifi(const char* ssid, const char* password);

// Forget a single saved network by slot index. Invalidates the slot and
// persists to NVS. Other slots are left in place — the gap will be reused
// by the next settings_addWifi() call (which prefers empty slots over
// the shift-down path).
void settings_removeWifi(int slot);

// Clear all saved WiFi networks.
void settings_clearAllWifi();

// --- GPS enabled flag (T-Watch-S3-Plus only) --------------------------------
//
// Persists the user's opt-in for the GPS receiver. Default on first boot is
// FALSE — the watch never turns on GPS until the user explicitly enables it
// from the UI. See src/gps.h for the privacy rationale.

bool settings_gpsEnabled();
void settings_setGpsEnabled(bool on);

// --- Last notification timestamp (NVS-persisted across reboots) ---------------

// Returns the last-polled notification timestamp (ISO 8601). Falls back to
// "1970-01-01T00:00:00.000Z" on first boot / missing key.
const char* settings_getLastNotifTimestamp();

// Persist a new high-water mark for notification polling.
void settings_setLastNotifTimestamp(const char* ts);
