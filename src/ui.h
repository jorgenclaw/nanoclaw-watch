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

// Force an immediate re-read of the BMA423 pedometer + refresh the steps
// button label. Use after resetting the step counter so the user sees the
// new value without waiting for the 30-sec update_battery tick.
void ui_refreshSteps();

// Tap-twice-to-confirm pattern for the Steps button. Returns true on the
// confirming SECOND tap (caller should perform the actual reset). Returns
// false on the first tap (just arms the visual confirm overlay) or when
// the button isn't built yet.
bool ui_handleStepsTap();

// Load the Clock sub-screen (entry to alarm/timer/stopwatch sub-sub-screens).
void ui_showClock();

// Sub-sub-screens accessed via the Clock screen.
void ui_showAlarm();
void ui_showTimer();
void ui_showStopwatch();

// Periodic tick for the clock features (stopwatch display update, timer
// countdown + fire detection, alarm time check). Called from ui_tick().
void ui_clockTick();

// True if a timer is currently counting down. Main loop uses this to skip
// auto-sleep when a timer is running — light sleep would pause the haptic
// fire on completion.
bool ui_timerIsRunning();

// True if an alarm is enabled and the next fire time is within
// `minutes_window` minutes. Main loop uses this to skip auto-sleep when
// the alarm is imminent — alarms can't fire from light sleep on this
// hardware (no wake-from-sleep workaround that doesn't strobe the screen).
bool ui_alarmIsImminent(int minutes_window);

// Hit-test a raw touch coordinate against the speak button. Used by the
// recording loop to distinguish cancel-zone (left 40%) taps from
// send-zone (right 60%) taps. Returns:
//   -1  outside the speak button bounds (ignore)
//    0  cancel zone (left 40% of button width)
//    1  send/stop zone (right 60% of button width)
int ui_speakBtnHitTest(int x, int y);

// Callbacks fired by UI buttons. Defined in main.cpp.
extern void onSpeakButtonPressed();
extern void onQuickPromptPressed(int idx);   // idx = 0..3
extern void onSleepButtonPressed();          // dedicated pinned-bottom Sleep button
extern void onResponseDismissed();
