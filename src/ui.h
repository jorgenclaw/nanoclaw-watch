#pragma once
#include <Arduino.h>

// =============================================================================
// UI — LVGL screens for the NanoClaw Watch
// =============================================================================

// One-time setup. Call after instance.begin() and beginLvglHelper(instance).
void ui_init();

// Refresh state-driven content (clock, battery, screen contents). Call from loop.
void ui_tick();

// Force a redraw of the home screen (e.g. after returning from response screen)
void ui_showHome();
void ui_showRecording();
void ui_showSending();
void ui_showSent();                 // brief "✓ Sent" confirmation
void ui_showNoSpeech();             // "No speech detected — try again"
void ui_showResponse(const char* text);
void ui_showError(const char* text);

// Update the speak-button label during an active recording with elapsed
// seconds. Cheap — only touches the label text, not the bg color.
void ui_setRecordingElapsed(uint32_t seconds);

// Callbacks fired by UI buttons. Defined in main.cpp.
extern void onSpeakButtonPressed();
extern void onQuickPromptPressed(int idx);   // idx = 0..3
extern void onResponseDismissed();
