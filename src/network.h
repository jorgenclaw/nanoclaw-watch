#pragma once
#include <Arduino.h>

// =============================================================================
// Network — WiFi connection management + host HTTP API
// =============================================================================

void net_begin();              // start WiFi connect via WiFiManager (blocks on first boot)
bool net_isConnected();        // current WiFi link status
void net_loop();               // call periodically; handles reconnect

// Call after settings_addWifi / settings_removeWifi to resync the live
// WiFi stack with the updated credential list. Rebuilds WiFiMulti; if
// the current connection was the one just forgotten, disconnects,
// wipes the ESP32 internal WiFi NVS cache (so auto-reconnect can't
// bring it back), and kicks a reconnect to a remaining saved network.
void net_onWifiListChanged();

// --- Non-blocking config portal ---
// Used when the watch is already running and the user wants to add/change
// WiFi without rebooting. Launch with net_startPortalAsync(), then pump
// net_portalProcess() from the main loop every tick. Check state via
// net_portalIsActive(); when it returns false, the portal has finished
// (either the user saved credentials, the timeout expired, or it was
// cancelled via net_portalStop()). After completion, call net_portalDidSave()
// to know whether new credentials were persisted.
bool net_startPortalAsync();   // begin non-blocking portal; true if started
void net_portalProcess();      // call every main loop tick while STATE_PORTAL
void net_portalStop();         // user cancelled via Close button
bool net_portalIsActive();     // true between startPortalAsync and completion
bool net_portalDidSave();      // true if the most recent portal saved creds

// Send a text-only prompt to the host. Blocks for up to HTTP_TIMEOUT_MS.
// Writes response into reply_buf (must be at least 512 bytes). Returns true on success.
bool net_postText(const char* prompt, char* reply_buf, size_t reply_buf_size);

// Send a recorded WAV blob to the host. Same blocking semantics as postText.
// audio_buf is the raw WAV data including the header.
bool net_postAudio(const uint8_t* audio_buf, size_t audio_size,
                   char* reply_buf, size_t reply_buf_size);

// Voice memo: POST to /api/watch/memo instead of /api/watch/message.
// Host transcribes, files to groups/<folder>/memos/<date>.md, returns a
// short confirmation string (e.g. "Saved (42 chars)"). No agent round-trip
// — the intent is "just capture this, don't chat."
bool net_postMemoAudio(const uint8_t* audio_buf, size_t audio_size,
                       char* reply_buf, size_t reply_buf_size);

// Voice reminder: POST to /api/watch/reminder. Host transcribes, parses
// "remind me at/in X to ..." phrasings, schedules a notification to fire
// at the parsed time, returns a confirmation string. Falls back to an
// error message if the time can't be parsed.
bool net_postReminderAudio(const uint8_t* audio_buf, size_t audio_size,
                           char* reply_buf, size_t reply_buf_size);

// Poll for any new responses queued at the host. Returns true if a new response
// was retrieved into reply_buf. Non-blocking-ish (uses HTTP_TIMEOUT_MS internally).
bool net_pollForResponse(char* reply_buf, size_t reply_buf_size);

// NTP time sync (call after WiFi connects)
void net_syncTime();

// Weather data fetched from wttr.in. All values populated together by
// net_fetchWeather() — both imperial and metric so unit toggles don't
// require a refetch. Strings are short and null-terminated.
struct WeatherData {
    int  temp_f;
    int  temp_c;
    int  uv_index;
    int  wind_mph;
    int  wind_kph;
    char wind_dir[8];           // "N", "NW", "ESE", etc.
    int  humidity_pct;          // current_condition[0].humidity (0-100)
    int  chance_of_rain_pct;    // max of weather[0].hourly[*].chanceofrain (0-100)
    int  today_max_f;
    int  today_max_c;
    int  tomorrow_max_f;
    int  tomorrow_max_c;
    int  tonight_min_f;          // = weather[1].mintempF (the coming morning low)
    int  tonight_min_c;          // = weather[1].mintempC
    char sunrise[12];            // "06:32 AM"
    char sunset[12];             // "07:48 PM"
};

// --- Proactive notifications ---

struct WatchNotification {
    char id[48];
    char type[8];           // "email" or "signal"
    char from[32];
    char preview[64];
    char full_text[1024];
    char timestamp[32];
};

// Poll for new notifications since the given ISO timestamp. Fills `out`
// array (up to `max_count` items). Returns the number of notifications.
int net_pollNotifications(const char* since_iso,
                          WatchNotification* out, int max_count);

// Fetch current weather from wttr.in for the location in WEATHER_LOCATION
// (configured in config.h). Populates `out` on success. Returns true on
// success, false on network failure or parse failure. Writes a short
// human-readable failure reason into `err_buf` on failure (e.g. "no WiFi",
// "HTTP 0", "parse fail"). Blocks for up to ~15 seconds.
bool net_fetchWeather(WeatherData* out, char* err_buf, size_t err_buf_size);
