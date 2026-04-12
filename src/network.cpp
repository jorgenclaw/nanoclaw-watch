#include "network.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static uint32_t s_lastReconnectAttempt = 0;
static const uint32_t RECONNECT_INTERVAL_MS = 10000;

// WiFiManager instance — kept alive so we can trigger the config portal
// on demand (long-press WiFi icon) without reconstructing.
static WiFiManager wm;

void net_begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // WiFiManager checks for saved credentials in NVS. If found, it
    // connects. If not (or connection fails after the timeout), it
    // starts a captive portal AP so the user can configure WiFi from
    // their phone — no USB or reflash required.
    wm.setConfigPortalTimeout(SETUP_PORTAL_TIMEOUT_SEC);
    wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_SEC);
    // Minimal portal UI — no scan list (saves RAM + time).
    wm.setMinimumSignalQuality(8);
    // Dark theme for the tiny watch screen isn't relevant — the portal
    // is viewed on the user's phone browser, not the watch.
    wm.setTitle("NanoClaw Watch");

    Serial.println("[net] WiFiManager autoConnect starting...");
    bool connected = wm.autoConnect(SETUP_AP_NAME);
    if (connected) {
        Serial.printf("[net] connected to %s\n", WiFi.SSID().c_str());
    } else {
        Serial.println("[net] WiFiManager timed out — rebooting");
        ESP.restart();
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
    Serial.println("[net] reconnecting...");
    WiFi.disconnect();
    WiFi.reconnect();
    s_lastReconnectAttempt = millis();
}

void net_startConfigPortal() {
    Serial.println("[net] starting config portal on demand");
    wm.setConfigPortalTimeout(SETUP_PORTAL_TIMEOUT_SEC);
    // resetSettings clears saved credentials so the portal opens fresh.
    wm.resetSettings();
    // startConfigPortal blocks until the user submits credentials or
    // the timeout elapses. On success WiFi connects automatically.
    bool ok = wm.startConfigPortal(SETUP_AP_NAME);
    if (ok) {
        Serial.printf("[net] reconfigured — connected to %s\n", WiFi.SSID().c_str());
    } else {
        Serial.println("[net] portal timed out — rebooting");
        ESP.restart();
    }
}

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
