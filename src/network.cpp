#include "network.h"
#include "config.h"
#include "settings.h"

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static uint32_t s_lastReconnectAttempt = 0;
static const uint32_t RECONNECT_INTERVAL_MS = 10000;
static WiFiMulti wifiMulti;

// --- Non-blocking portal state ---
// WiFiManager must outlive the start call since main loop pumps its
// process() between ticks. Keep it heap-allocated so the destructor only
// runs when we're actually done with it.
static WiFiManager* s_portalWm = nullptr;
static bool s_portalActive = false;
static bool s_portalSaved  = false;

// WiFiManager save callback — fires when the user submits credentials
// via the portal web UI. Persists them to our NVS slot storage so the
// next boot can reconnect without the portal.
static void onPortalSaveCredentials() {
    String ssid = WiFi.SSID();
    String pass = s_portalWm ? s_portalWm->getWiFiPass() : String("");
    if (ssid.length() > 0) {
        settings_addWifi(ssid.c_str(), pass.c_str());
        Serial.printf("[net] portal saved network: %s\n", ssid.c_str());
        s_portalSaved = true;
    }
}

// (Re)populate WiFiMulti from the NVS-backed credential slots.
static void loadMultiCredentials() {
    const WiFiCred* creds = settings_getWifiCreds();
    int count = 0;
    for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
        if (creds[i].valid) {
            wifiMulti.addAP(creds[i].ssid, creds[i].pass);
            Serial.printf("[net] slot %d: %s\n", i, creds[i].ssid);
            count++;
        }
    }
    if (count == 0) Serial.println("[net] no saved networks");
}

// Run the WiFiManager captive portal. On success, saves the new
// credentials to NVS via settings_addWifi (which handles dedup and
// slot rotation). Returns true if the user configured a network.
static bool runConfigPortal() {
    WiFiManager wm;
    wm.setConfigPortalTimeout(SETUP_PORTAL_TIMEOUT_SEC);
    wm.setTitle("NanoClaw Watch");

    Serial.println("[net] starting captive portal — AP: " SETUP_AP_NAME);
    bool ok = wm.startConfigPortal(SETUP_AP_NAME);
    if (ok) {
        // WiFiManager connected successfully. Save the credentials to
        // our own NVS storage so WiFiMulti can use them on next boot.
        String ssid = WiFi.SSID();
        String pass = wm.getWiFiPass();
        if (ssid.length() > 0) {
            settings_addWifi(ssid.c_str(), pass.c_str());
            Serial.printf("[net] saved new network: %s\n", ssid.c_str());
        }
    }
    return ok;
}

void net_begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // Load saved networks into WiFiMulti.
    loadMultiCredentials();

    // Check if we have any saved networks to try.
    const WiFiCred* creds = settings_getWifiCreds();
    bool hasNetworks = false;
    for (int i = 0; i < WIFI_MAX_NETWORKS; i++) {
        if (creds[i].valid) { hasNetworks = true; break; }
    }

    if (!hasNetworks) {
        // No credentials in our NVS slots. Check if the old WiFiManager
        // left credentials in the ESP32 WiFi NVS (from a previous firmware).
        // WiFi.begin() with no args uses whatever is in NVS from the last
        // successful connection.
        Serial.println("[net] no saved networks — trying ESP32 NVS fallback");
        WiFi.begin();
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED &&
               millis() - start < WIFI_CONNECT_TIMEOUT_SEC * 1000) {
            delay(500);
        }
        if (WiFi.status() == WL_CONNECTED) {
            // Migrate the working credentials into our slot storage.
            String ssid = WiFi.SSID();
            String psk  = WiFi.psk();
            if (ssid.length() > 0) {
                settings_addWifi(ssid.c_str(), psk.c_str());
                Serial.printf("[net] migrated from ESP32 NVS: %s\n", ssid.c_str());
                hasNetworks = true;
            }
        }
    }

    if (hasNetworks) {
        if (WiFi.status() != WL_CONNECTED) {
            // Try connecting to any of the saved networks.
            Serial.println("[net] trying saved networks...");
            uint32_t start = millis();
            while (wifiMulti.run() != WL_CONNECTED &&
                   millis() - start < WIFI_CONNECT_TIMEOUT_SEC * 1000) {
                delay(500);
            }
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[net] connected to %s\n", WiFi.SSID().c_str());
    } else {
        // No saved networks, or none in range — open the config portal.
        Serial.println("[net] no saved network available — opening portal");
        bool ok = runConfigPortal();
        if (!ok) {
            Serial.println("[net] portal timed out — rebooting");
            ESP.restart();
        }
    }
    s_lastReconnectAttempt = millis();
}

bool net_isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void net_loop() {
    static bool printed_ip = false;
    if (WiFi.status() == WL_CONNECTED) {
        if (!printed_ip) {
            Serial.printf("[net] local=%s  gw=%s  mask=%s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(),
                WiFi.subnetMask().toString().c_str());
            printed_ip = true;
        }
        return;
    }
    printed_ip = false;
    if (millis() - s_lastReconnectAttempt < RECONNECT_INTERVAL_MS) return;
    // Try all saved networks on reconnect.
    Serial.println("[net] reconnecting...");
    wifiMulti.run();
    s_lastReconnectAttempt = millis();
}

// --- Non-blocking portal ---

bool net_startPortalAsync() {
    if (s_portalActive) return false;
    if (s_portalWm) { delete s_portalWm; s_portalWm = nullptr; }
    s_portalWm = new WiFiManager();
    if (!s_portalWm) {
        Serial.println("[net] portal: alloc failed");
        return false;
    }
    s_portalSaved = false;
    s_portalWm->setTitle("NanoClaw Watch");
    // Timeout 0 = no auto-close. Captive-portal setup can take a while,
    // especially if the user is troubleshooting their phone's WiFi
    // settings. User dismisses via the Close button or by completing the
    // web flow (which triggers onPortalSaveCredentials + breakAfterConfig).
    s_portalWm->setConfigPortalTimeout(0);
    s_portalWm->setConfigPortalBlocking(false);          // critical
    s_portalWm->setBreakAfterConfig(true);               // return from process() when saved
    s_portalWm->setSaveConfigCallback(onPortalSaveCredentials);

    Serial.println("[net] starting non-blocking portal — AP: " SETUP_AP_NAME);
    // In non-blocking mode, startConfigPortal() ALWAYS returns false
    // because `result` is initialized to false and the non-blocking
    // return path skips the loop that would set it to true on success.
    // (See WiFiManager.cpp startConfigPortal body.) So the return value
    // tells us nothing — we just assume the portal is up if the library
    // didn't crash. Timeout / user save / user cancel are all detected
    // later via process() in the main loop.
    s_portalWm->startConfigPortal(SETUP_AP_NAME);
    s_portalActive = true;
    return true;
}

void net_portalProcess() {
    if (!s_portalActive || !s_portalWm) return;
    // process() returns true when the portal is "done" — either the user
    // saved credentials (setBreakAfterConfig) or the timeout expired.
    bool done = s_portalWm->process();
    if (done) {
        Serial.println("[net] portal: process() reported done");
        // Teardown happens via net_portalStop() from the main loop's
        // completion path. Mark it inactive here so the main loop can
        // observe the transition.
        s_portalActive = false;
    }
}

void net_portalStop() {
    if (!s_portalWm) { s_portalActive = false; return; }
    Serial.println("[net] portal: stopping");
    s_portalWm->stopConfigPortal();
    delete s_portalWm;
    s_portalWm = nullptr;
    s_portalActive = false;
    // Rebuild WiFiMulti — if credentials were saved, it'll pick them up;
    // if not, this is a no-op refresh.
    wifiMulti = WiFiMulti();
    loadMultiCredentials();
}

bool net_portalIsActive() { return s_portalActive; }
bool net_portalDidSave()  { return s_portalSaved; }

void net_syncTime() {
    if (!net_isConnected()) return;
    Serial.println("[net] syncing time via NTP...");
    configTime(TZ_OFFSET_SECONDS, 0, NTP_SERVER);
    // Don't block — let the NTP daemon update time in the background.
}

// Internal: parse a JSON response from the host into reply_buf.
// Expected shape: { "reply": "<text>", "full_id": "<uuid>" }
//
// Use DynamicJsonDocument so the parse buffer is heap-allocated rather than
// stack-allocated. Replies from Jorgenclaw routinely run several KB; the
// previous StaticJsonDocument<1024> silently failed on anything larger.
static bool parseHostJson(const String& body, char* reply_buf, size_t reply_buf_size) {
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[net] JSON parse error: %s (body len %u)\n",
                      err.c_str(), (unsigned)body.length());
        return false;
    }
    const char* reply = doc["reply"] | "";
    strncpy(reply_buf, reply, reply_buf_size - 1);
    reply_buf[reply_buf_size - 1] = '\0';
    return true;
}

bool net_postText(const char* prompt, char* reply_buf, size_t reply_buf_size) {
    if (!net_isConnected()) {
        strncpy(reply_buf, "No WiFi", reply_buf_size - 1);
        return false;
    }

    HTTPClient http;
    String url = String(NANOCLAW_HOST_URL) + "/api/watch/message";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Watch-Token", WATCH_AUTH_TOKEN);
    http.setTimeout(HTTP_TIMEOUT_MS);

    StaticJsonDocument<512> req;
    req["text"] = prompt;
    req["device_id"] = WATCH_DEVICE_ID;
    String body;
    serializeJson(req, body);

    int code = http.POST(body);
    Serial.printf("[net] postText -> %d\n", code);

    bool ok = false;
    if (code == 200) {
        String resp = http.getString();
        ok = parseHostJson(resp, reply_buf, reply_buf_size);
    } else {
        snprintf(reply_buf, reply_buf_size, "HTTP %d", code);
    }
    http.end();
    return ok;
}

bool net_postAudio(const uint8_t* audio_buf, size_t audio_size,
                   char* reply_buf, size_t reply_buf_size) {
    if (!net_isConnected()) {
        strncpy(reply_buf, "No WiFi", reply_buf_size - 1);
        return false;
    }
    if (!audio_buf || audio_size == 0) {
        strncpy(reply_buf, "Empty audio", reply_buf_size - 1);
        return false;
    }

    HTTPClient http;
    String url = String(NANOCLAW_HOST_URL) + "/api/watch/message";
    http.begin(url);
    http.addHeader("Content-Type", "audio/wav");
    http.addHeader("X-Watch-Token", WATCH_AUTH_TOKEN);
    http.addHeader("X-Device-Id", WATCH_DEVICE_ID);
    http.setTimeout(HTTP_TIMEOUT_MS);

    int code = http.POST(const_cast<uint8_t*>(audio_buf), audio_size);
    Serial.printf("[net] postAudio (%u bytes) -> %d\n", (unsigned)audio_size, code);

    bool ok = false;
    if (code == 200) {
        String resp = http.getString();
        ok = parseHostJson(resp, reply_buf, reply_buf_size);
    } else {
        snprintf(reply_buf, reply_buf_size, "HTTP %d", code);
    }
    http.end();
    return ok;
}

bool net_pollForResponse(char* reply_buf, size_t reply_buf_size) {
    if (!net_isConnected()) return false;

    HTTPClient http;
    String url = String(NANOCLAW_HOST_URL) + "/api/watch/poll?device_id=" WATCH_DEVICE_ID;
    http.begin(url);
    http.addHeader("X-Watch-Token", WATCH_AUTH_TOKEN);
    http.setTimeout(HTTP_TIMEOUT_MS);

    int code = http.GET();
    bool hasNew = false;
    if (code == 200) {
        String resp = http.getString();
        StaticJsonDocument<1024> doc;
        if (deserializeJson(doc, resp) == DeserializationError::Ok) {
            if (doc["has_new"] | false) {
                const char* reply = doc["reply"] | "";
                strncpy(reply_buf, reply, reply_buf_size - 1);
                reply_buf[reply_buf_size - 1] = '\0';
                hasNew = true;
            }
        }
    }
    http.end();
    return hasNew;
}

// Helper — fill the err_buf if provided, otherwise no-op.
static void wx_set_err(char* err_buf, size_t err_buf_size, const char* msg) {
    if (!err_buf || err_buf_size == 0) return;
    strncpy(err_buf, msg, err_buf_size - 1);
    err_buf[err_buf_size - 1] = '\0';
}

bool net_fetchWeather(WeatherData* out, char* err_buf, size_t err_buf_size) {
    if (!out) {
        wx_set_err(err_buf, err_buf_size, "null out");
        return false;
    }
    if (!net_isConnected()) {
        Serial.println("[weather] no WiFi");
        wx_set_err(err_buf, err_buf_size, "no WiFi");
        return false;
    }

    HTTPClient http;
    String url = String("http://wttr.in/") + WEATHER_LOCATION + "?format=j1";
    http.begin(url);
    // wttr.in serves much faster + more reliably with a real User-Agent
    // than the default ESP HTTPClient string, which sometimes gets the
    // ASCII fallback page instead of JSON. Connection: close keeps the
    // socket from hanging open after the response.
    http.addHeader("User-Agent", "curl/7.85.0");
    http.addHeader("Accept", "application/json");
    http.addHeader("Connection", "close");
    // 15 sec instead of HTTP_TIMEOUT_MS (60 sec) — the watch loop blocks
    // here, so a 60-sec freeze on weather failure is unacceptable. wttr.in
    // is normally quick (1-3 sec) when reachable.
    http.setTimeout(15000);

    int code = http.GET();
    Serial.printf("[weather] GET %s -> %d\n", url.c_str(), code);
    if (code != 200) {
        http.end();
        char msg[16];
        snprintf(msg, sizeof(msg), "HTTP %d", code);
        wx_set_err(err_buf, err_buf_size, msg);
        return false;
    }

    // Filter mirrors the structure of the response. Subscript syntax —
    // ArduinoJson treats `filter[key][0]` as the template applied to all
    // elements of the matching input array. We need today (weather[0])
    // for sunrise/sunset/today's max, and tomorrow (weather[1]) for
    // tomorrow's max + the "tonight low".
    DynamicJsonDocument filter(1024);
    filter["current_condition"][0]["temp_F"]         = true;
    filter["current_condition"][0]["temp_C"]         = true;
    filter["current_condition"][0]["uvIndex"]        = true;
    filter["current_condition"][0]["windspeedMiles"] = true;
    filter["current_condition"][0]["windspeedKmph"]  = true;
    filter["current_condition"][0]["winddir16Point"] = true;
    filter["weather"][0]["maxtempF"]                 = true;
    filter["weather"][0]["maxtempC"]                 = true;
    filter["weather"][0]["mintempF"]                 = true;
    filter["weather"][0]["mintempC"]                 = true;
    filter["weather"][0]["astronomy"][0]["sunrise"]  = true;
    filter["weather"][0]["astronomy"][0]["sunset"]   = true;

    // Buffer the full response body via getString() instead of stream-
    // parsing. The first attempt used http.getStream() + deserializeJson
    // directly, which appeared to interact badly with chunked transfer
    // encoding from wttr.in — the parser succeeded but produced an empty
    // current_condition. Buffering the body sidesteps that entirely at
    // the cost of ~40 KB of heap during the parse (well within the
    // ESP32-S3's 320 KB DRAM budget).
    String body = http.getString();
    http.end();
    Serial.printf("[weather] body length = %d\n", body.length());

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(
        doc, body, DeserializationOption::Filter(filter));

    if (err) {
        Serial.printf("[weather] JSON parse error: %s\n", err.c_str());
        wx_set_err(err_buf, err_buf_size, "parse fail");
        return false;
    }

    // current_condition (note: wttr.in returns ALL these as strings, so
    // ArduinoJson's "| 0" / "| def" syntax does the conversion for us via
    // implicit cast on the | default).
    JsonObject cc = doc["current_condition"][0];
    if (cc.isNull()) {
        Serial.println("[weather] no current_condition[0]");
        wx_set_err(err_buf, err_buf_size, "no cc");
        return false;
    }
    out->temp_f   = atoi(cc["temp_F"]         | "0");
    out->temp_c   = atoi(cc["temp_C"]         | "0");
    out->uv_index = atoi(cc["uvIndex"]        | "0");
    out->wind_mph = atoi(cc["windspeedMiles"] | "0");
    out->wind_kph = atoi(cc["windspeedKmph"]  | "0");
    const char* dir = cc["winddir16Point"] | "--";
    strncpy(out->wind_dir, dir, sizeof(out->wind_dir) - 1);
    out->wind_dir[sizeof(out->wind_dir) - 1] = '\0';

    // weather[0] = today (for max + sunrise/sunset)
    JsonArray weather = doc["weather"];
    if (weather.size() < 2) {
        Serial.println("[weather] not enough weather days in response");
        wx_set_err(err_buf, err_buf_size, "no fcst");
        return false;
    }
    JsonObject today = weather[0];
    out->today_max_f = atoi(today["maxtempF"] | "0");
    out->today_max_c = atoi(today["maxtempC"] | "0");

    JsonObject astro = today["astronomy"][0];
    const char* sr = astro["sunrise"] | "--:--";
    const char* ss = astro["sunset"]  | "--:--";
    strncpy(out->sunrise, sr, sizeof(out->sunrise) - 1);
    out->sunrise[sizeof(out->sunrise) - 1] = '\0';
    strncpy(out->sunset,  ss, sizeof(out->sunset)  - 1);
    out->sunset[sizeof(out->sunset) - 1] = '\0';

    // weather[1] = tomorrow (for tomorrow max + the "tonight low" — i.e.
    // the coming morning's minimum, which is the daily min for tomorrow's
    // calendar day in wttr.in's data model)
    JsonObject tomorrow = weather[1];
    out->tomorrow_max_f = atoi(tomorrow["maxtempF"] | "0");
    out->tomorrow_max_c = atoi(tomorrow["maxtempC"] | "0");
    out->tonight_min_f  = atoi(tomorrow["mintempF"] | "0");
    out->tonight_min_c  = atoi(tomorrow["mintempC"] | "0");

    Serial.printf("[weather] %dF/%dC UV%d wind %d mph %s | "
                  "today %dF tomorrow %dF tonight %dF | sun %s-%s\n",
                  out->temp_f, out->temp_c, out->uv_index,
                  out->wind_mph, out->wind_dir,
                  out->today_max_f, out->tomorrow_max_f, out->tonight_min_f,
                  out->sunrise, out->sunset);
    return true;
}

// =============================================================================
// Notification polling
// =============================================================================

int net_pollNotifications(const char* since_iso,
                          WatchNotification* out, int max_count) {
    if (!net_isConnected()) return 0;

    String url = String(NANOCLAW_HOST_URL)
        + "/api/watch/notifications?since="
        + String(since_iso);

    HTTPClient http;
    http.begin(url);
    http.addHeader("X-Watch-Token", WATCH_AUTH_TOKEN);
    http.setTimeout(NOTIF_POLL_HTTP_TIMEOUT);

    int code = http.GET();
    if (code != 200) {
        if (code > 0) {
            Serial.printf("[notif] poll got HTTP %d\n", code);
        }
        http.end();
        return 0;
    }

    String body = http.getString();
    http.end();

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[notif] JSON parse error: %s\n", err.c_str());
        return 0;
    }

    JsonArray arr = doc["notifications"];
    int count = 0;
    for (JsonObject item : arr) {
        if (count >= max_count) break;
        WatchNotification& n = out[count];
        strncpy(n.id,        item["id"]        | "", sizeof(n.id) - 1);
        strncpy(n.type,      item["type"]      | "", sizeof(n.type) - 1);
        strncpy(n.from,      item["from"]      | "", sizeof(n.from) - 1);
        strncpy(n.preview,   item["preview"]   | "", sizeof(n.preview) - 1);
        strncpy(n.full_text, item["full_text"] | "", sizeof(n.full_text) - 1);
        strncpy(n.timestamp, item["timestamp"] | "", sizeof(n.timestamp) - 1);
        count++;
    }

    if (count > 0) {
        Serial.printf("[notif] received %d notification(s)\n", count);
    }
    return count;
}
