#pragma once
#include <Arduino.h>

// =============================================================================
// State machine — single source of truth for what the watch is doing.
// UI screens render based on currentState(); transitions are explicit.
// =============================================================================

enum WatchState {
    STATE_BOOT,        // initial state during setup()
    STATE_HOME,        // home screen (clock, battery, speak button, presets)
    STATE_RECORDING,   // capturing voice (mic active, "Listening..." screen)
    STATE_SENDING,     // POST in flight ("Sending..." screen)
    STATE_RESPONSE,    // displaying agent response ("Reply" screen)
    STATE_CLOCK,       // clock sub-screen (alarm/timer/stopwatch)
    STATE_WEATHER,     // weather sub-screen (current + forecast + units)
    STATE_NOTIFICATION,// notification detail (full text, scrollable)
    STATE_DND,         // Do Not Disturb submenu
    STATE_DND_CUSTOM,  // DND custom duration picker (sub-sub-screen)
    STATE_BATTERY,     // battery/system info detail screen
    STATE_POMODORO,    // Pomodoro timer (work/rest cycles)
    STATE_WIFI_MANAGER,// saved-networks list with tap-to-forget
    STATE_PORTAL,      // WiFi captive portal running (main loop pumps wm.process())
    STATE_ERROR        // generic error display
};

void state_init();
WatchState currentState();
void setState(WatchState s);

// Time of last user interaction (millis()), used for idle/sleep countdown
uint32_t lastInteractionMs();
void touchInteraction();

// Last received response text (max ~500 chars from host); ui reads this
const char* lastResponseText();
void setLastResponse(const char* text);

// Last error message
const char* lastErrorText();
void setLastError(const char* text);
