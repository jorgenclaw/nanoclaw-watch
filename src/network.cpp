#include "network.h"
#include "config.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static uint32_t s_lastReconnectAttempt = 0;
static const uint32_t RECONNECT_INTERVAL_MS = 10000;

void net_begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[net] connecting to %s\n", WIFI_SSID);
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
    printed_ip = false;  // reset so we re-print after reconnect
    if (millis() - s_lastReconnectAttempt < RECONNECT_INTERVAL_MS) return;
    Serial.println("[net] reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    s_lastReconnectAttempt = millis();
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
