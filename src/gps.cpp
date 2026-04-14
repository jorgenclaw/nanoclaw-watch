#include "gps.h"
#include "settings.h"
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <LilyGoLib.h>
#include <math.h>

// =============================================================================
// Hardware configuration — VERIFY BEFORE FLASHING A T-WATCH-S3-PLUS
// =============================================================================
//
// These constants are the common LilyGo GPS pinout for S3-Plus variants and
// match the L76K module. Before flashing hardware, confirm against LilyGoLib's
// lilygo_twatch_s3_plus variant header (`pins_arduino.h` in the PlatformIO
// variant folder) or the LilyGo schematic PDF. If LilyGoLib already
// initializes a GPS UART internally via `instance.gps`, this raw HardwareSerial
// path will collide — switch to `instance.gps` in that case (see GPS.h in
// LilyGoLib for the wrapper API).

static constexpr int GPS_UART_NUM = 1;
static constexpr int GPS_RX_PIN   = 33;   // MCU RX <- GPS TX
static constexpr int GPS_TX_PIN   = 34;   // MCU TX -> GPS RX
static constexpr uint32_t GPS_BAUD = 9600;

// UART read throttle. TinyGPSPlus needs a steady byte stream; 200 ms is
// slow enough to keep loop() latency low and fast enough that a 1 Hz NMEA
// burst (~60 bytes) drains in one or two polls.
static constexpr uint32_t GPS_POLL_INTERVAL_MS = 200;

// Fix freshness window — anything older than this counts as "no fix".
static constexpr uint32_t GPS_FIX_STALE_MS = 10000;

// Sentinel returned by gps_hdop() when no fix is present.
static constexpr float GPS_HDOP_NO_FIX = 99.9f;

// =============================================================================
// Module state
// =============================================================================

static HardwareSerial s_gpsSerial(GPS_UART_NUM);
static TinyGPSPlus    s_gps;
static bool     s_enabled    = false;
static uint32_t s_lastPollMs = 0;
static uint32_t s_lastFixMs  = 0;

// =============================================================================
// Power control
// =============================================================================
//
// On the T-Watch-S3-Plus the L76K sits on a switchable power rail. LilyGoLib's
// powerControl() takes a POWER_* enum and a bool — audio uses POWER_SPEAK the
// same way in main.cpp. The GPS enum name is not yet confirmed (LilyGoLib
// 0.1.0 may or may not expose POWER_GPS); until it is, powerOn/Off fall back
// to begin()/end() on the UART, which stops NMEA parsing but leaves the
// receiver drawing standby current. Replace the TODO line with the real
// powerControl() call once the enum is verified — that is what turns "off"
// from a software mute into a hardware kill.

static void gps_powerOn() {
    // TODO: instance.powerControl(POWER_GPS, true);
    s_gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("[gps] power on");
}

static void gps_powerOff() {
    s_gpsSerial.end();
    // TODO: instance.powerControl(POWER_GPS, false);
    s_lastFixMs = 0;
    Serial.println("[gps] power off");
}

// =============================================================================
// Lifecycle
// =============================================================================

void gps_init() {
    s_enabled = settings_gpsEnabled();
    Serial.printf("[gps] init: enabled=%d\n", s_enabled ? 1 : 0);
    if (s_enabled) gps_powerOn();
}

void gps_loop() {
    if (!s_enabled) return;
    if (millis() - s_lastPollMs < GPS_POLL_INTERVAL_MS) return;
    s_lastPollMs = millis();

    while (s_gpsSerial.available()) {
        if (s_gps.encode(s_gpsSerial.read())) {
            if (s_gps.location.isValid() && s_gps.location.isUpdated()) {
                s_lastFixMs = millis();
            }
        }
    }
}

void gps_setEnabled(bool on) {
    if (on == s_enabled) return;
    s_enabled = on;
    settings_setGpsEnabled(on);
    if (on) gps_powerOn();
    else    gps_powerOff();
}

bool gps_enabled() {
    return s_enabled;
}

// =============================================================================
// Fix accessors
// =============================================================================

bool gps_hasFix() {
    if (!s_enabled) return false;
    if (s_lastFixMs == 0) return false;
    return (millis() - s_lastFixMs) < GPS_FIX_STALE_MS;
}

double gps_lat() { return s_gps.location.lat(); }
double gps_lon() { return s_gps.location.lng(); }

// Round to 2 decimal places before handing to any off-device path.
// 2 decimals ≈ 1.11 km at the equator, finer at higher latitudes — enough
// for "which city" context, not enough to place the user on a street.
static inline double fuzz2(double v) {
    return round(v * 100.0) / 100.0;
}

double gps_latFuzzed() { return fuzz2(s_gps.location.lat()); }
double gps_lonFuzzed() { return fuzz2(s_gps.location.lng()); }

float gps_altMeters() { return s_gps.altitude.meters(); }

uint8_t gps_sats() {
    return s_gps.satellites.isValid()
        ? (uint8_t)s_gps.satellites.value()
        : 0;
}

float gps_hdop() {
    return s_gps.hdop.isValid() ? s_gps.hdop.hdop() : GPS_HDOP_NO_FIX;
}

uint32_t gps_lastFixMs() {
    return s_lastFixMs;
}
