#pragma once
#include <Arduino.h>

// =============================================================================
// GPS module — T-Watch-S3-Plus L76K receiver
// =============================================================================
//
// The base T-Watch-S3 has no GPS hardware; this module is a no-op there.
// The T-Watch-S3-Plus adds an L76K receiver on a dedicated UART (NMEA @ 9600
// baud). Default state is OFF — the user must explicitly opt in from the UI,
// and the enabled/disabled flag persists to NVS via settings_gpsEnabled().
//
// Privacy posture:
//   - GPS is receive-only; the watch never transmits coordinates to satellites.
//     Leaks can only happen downstream, when a fix is read out of this module
//     and handed to another subsystem.
//   - gps_latFuzzed() / gps_lonFuzzed() round to 2 decimals (~1.1 km). Any path
//     that sends location off-device (notifications, memos, weather requests)
//     should prefer the fuzzed getters. The raw getters exist for on-device
//     features (geofence matching, fitness tracking) that must not leak.
//   - gps_setEnabled(false) cuts power to the receiver on boards where
//     LilyGoLib exposes a POWER_GPS rail, not just "stop reading NMEA". When
//     off, the module draws zero current and is provably inert.

// Call once from setup(), after settings_load(). Honors the persisted
// enabled flag — if the user previously turned GPS on, this re-powers it.
void gps_init();

// Call every loop() tick. Throttled internally (polls UART every 200 ms).
// Safe to call when disabled — returns immediately.
void gps_loop();

// Runtime on/off. Persists to NVS via settings_setGpsEnabled() and applies
// the power-rail change immediately. No-op if the new state matches current.
void gps_setEnabled(bool on);
bool gps_enabled();

// Returns true if a valid fix was received in the last 10 seconds.
// Stale fixes (older than 10 s) count as "no fix" so callers don't act on
// a location the user may have walked away from.
bool gps_hasFix();

// Raw coordinates — 6 decimal places of precision (~11 cm). Use only for
// on-device features. Undefined if gps_hasFix() is false.
double gps_lat();
double gps_lon();

// Fuzzed coordinates — rounded to 2 decimal places (~1.1 km). Safe to send
// off-device (host notifications, cloud prompts). Undefined if no fix.
double gps_latFuzzed();
double gps_lonFuzzed();

// Altitude above mean sea level, in meters. Undefined if no fix.
float gps_altMeters();

// Number of satellites currently tracked. Zero before first fix.
uint8_t gps_sats();

// Horizontal dilution of precision. Lower is better; < 2.0 is a good fix,
// > 5.0 is garbage. Returns a large sentinel value when no fix.
float gps_hdop();

// millis() timestamp of the most recent valid fix, or 0 if never seen.
// Useful for UI ("last fix: 12 s ago").
uint32_t gps_lastFixMs();
