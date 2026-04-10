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
