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

// --- Multi-network WiFi credentials (up to 3 saved networks) ----------------

static const int WIFI_MAX_NETWORKS = 3;

struct WiFiCred {
    char ssid[64];
    char pass[64];
    bool valid;   // true if this slot has credentials
};

// Returns the array of 3 credential slots (loaded from NVS by settings_load).
const WiFiCred* settings_getWifiCreds();

// Save a new network. Writes to the first empty slot; if all 3 are full,
// overwrites the oldest (slot 0, shifting the others down).
void settings_addWifi(const char* ssid, const char* password);

// Clear all saved WiFi networks.
void settings_clearAllWifi();

// --- Last notification timestamp (NVS-persisted across reboots) ---------------

// Returns the last-polled notification timestamp (ISO 8601). Falls back to
// "1970-01-01T00:00:00.000Z" on first boot / missing key.
const char* settings_getLastNotifTimestamp();

// Persist a new high-water mark for notification polling.
void settings_setLastNotifTimestamp(const char* ts);
