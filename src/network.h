#pragma once
#include <Arduino.h>

// =============================================================================
// Network — WiFi connection management + host HTTP API
// =============================================================================

void net_begin();              // start WiFi connect (non-blocking after first call)
bool net_isConnected();        // current WiFi link status
void net_loop();               // call periodically; handles reconnect

// Send a text-only prompt to the host. Blocks for up to HTTP_TIMEOUT_MS.
// Writes response into reply_buf (must be at least 512 bytes). Returns true on success.
bool net_postText(const char* prompt, char* reply_buf, size_t reply_buf_size);

// Send a recorded WAV blob to the host. Same blocking semantics as postText.
// audio_buf is the raw WAV data including the header.
bool net_postAudio(const uint8_t* audio_buf, size_t audio_size,
                   char* reply_buf, size_t reply_buf_size);

// Poll for any new responses queued at the host. Returns true if a new response
// was retrieved into reply_buf. Non-blocking-ish (uses HTTP_TIMEOUT_MS internally).
bool net_pollForResponse(char* reply_buf, size_t reply_buf_size);

// NTP time sync (call after WiFi connects)
void net_syncTime();
