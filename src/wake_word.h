#pragma once
#include <Arduino.h>

// =============================================================================
// Wake word detection — "Hey Jorgenclaw"
// =============================================================================
//
// Runs an Edge Impulse classifier as a low-priority FreeRTOS task on core 0.
// Continuously reads from instance.mic (the same PDM mic used for voice
// recording) when the watch is idle on the home screen, and classifies
// each 250 ms slice against the model in lib/Jorgenclaw-project-1_inferencing.
//
// Limitation: wake word only works while the ESP32-S3 is AWAKE. Light sleep
// halts the CPU, which also halts this task. Users who want to trigger a
// recording while the watch is asleep must tap the screen first to wake it.
// This is a known gap — see project memory for the planned dim-mode fix.

// Reserve PSRAM for the EI tensor arena. MUST be called as the very first
// line of setup(), before instance.begin() runs — LilyGoLib's display +
// LVGL init consumes most of the 8 MB of PSRAM and leaves only ~130 KB
// free, which is not enough for the 213 KB tensor arena. Calling this
// first grabs the chunk while PSRAM is still empty.
void wake_word_preallocate();

void wake_word_task_start();

// Returns true once per detection event, then clears the flag internally.
// Called from the main loop to dispatch the wake-word trigger.
bool wake_word_triggered();
