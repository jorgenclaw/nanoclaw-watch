// =============================================================================
// NanoClaw Watch — main entry point
//
// LilyGo T-Watch S3 firmware that turns the watch into a Jorgenclaw terminal.
// Capabilities:
//   - LVGL home screen with clock, battery, WiFi indicator, speak button,
//     4 quick-tap preset prompts
//   - Voice recording (PDM mic, 5 sec) -> POST WAV to NanoClaw host
//   - Quick-tap presets POST text prompts
//   - Polls host every 60 sec for incoming responses; haptic + screen wake
//   - Tilt-to-wake from light sleep, idle sleep after 30 sec
//
// Edit src/config.h to fill in WiFi credentials and host URL before flashing.
// =============================================================================

#include <Arduino.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>

#include "config.h"
#include "state.h"
#include "network.h"
#include "ui.h"

// Reply buffer for HTTP responses. Sized to fit typical Jorgenclaw replies
// (multi-paragraph responses can run several KB). Must be at least as large
// as state.cpp's g_responseText to avoid double truncation.
static char g_replyBuf[4096];
static uint32_t g_lastPollMs = 0;
static bool g_powerKeyPressed = false;
// Set to true by the recording loop's direct touch poll (or as a fallback
// by onSpeakButtonPressed via the LVGL CLICK path) when the user taps the
// SEND zone of the speak button while recording. doVoiceCapture() breaks
// out and proceeds to RMS gate + attenuate + POST.
static volatile bool g_stopRecording = false;
// Set to true by the recording loop's direct touch poll when the user taps
// the CANCEL zone (left 40%) of the speak button. doVoiceCapture() breaks
// out, frees the audio buffer, and returns to home WITHOUT POSTing.
static volatile bool g_cancelRecording = false;

// Forward declarations of work routines
static void doVoiceCapture();
static void doQuickPrompt(int idx);
static void doPoll();
static void enterLightSleep();

// =============================================================================
// Device event handler — fires from instance.loop() on hardware interrupts
// =============================================================================

static void device_event_cb(DeviceEvent_t event, void* user_data) {
    switch (event) {
    case PMU_EVENT_KEY_CLICKED:
        // Side power button (IO0 — the only physical button on this watch).
        // Queue a sleep request handled in main loop.
        Serial.println("[evt] PMU_EVENT_KEY_CLICKED");
        g_powerKeyPressed = true;
        break;
    case SENSOR_DOUBLE_TAP_DETECTED:
        // Deliberate wrist double-tap — counts as a real interaction.
        Serial.println("[evt] SENSOR_DOUBLE_TAP_DETECTED");
        touchInteraction();
        break;
    case SENSOR_TILT_DETECTED:
        // Tilt detection is too noisy to count as a user interaction (any
        // ambient vibration triggers it). Log it for diagnostics but do
        // NOT reset the idle timer — that's what was preventing the watch
        // from ever reaching the 30-sec idle threshold.
        Serial.println("[evt] SENSOR_TILT_DETECTED (ignored)");
        break;
    default:
        break;
    }
}

// =============================================================================
// UI -> work routine bridges (declared extern in ui.h)
// =============================================================================

void onSpeakButtonPressed() {
    WatchState s = currentState();
    if (s == STATE_HOME) {
        // First tap: begin streaming capture
        doVoiceCapture();
    } else if (s == STATE_RECORDING) {
        // Second tap: stop the active recording and send. The recording loop
        // inside doVoiceCapture() checks this flag every I2S read.
        Serial.println("[ui] stop-tap received during recording");
        g_stopRecording = true;
    }
    // Other states: ignore button taps
}

void onQuickPromptPressed(int idx) {
    if (currentState() != STATE_HOME) return;
    // Quick-button index 1 is the Clock entry — opens a sub-screen with
    // alarm/timer/stopwatch buttons (currently stubbed). The sub-screen has
    // its own Close button (top-right) that returns to home.
    if (idx == 1) {
        Serial.println("[ui] clock button pressed — opening sub-screen");
        instance.vibrator();
        setState(STATE_CLOCK);
        ui_showClock();
        return;
    }
    // Quick-button index 2 is the Steps button — shows live pedometer count
    // and resets the counter via tap-twice-to-confirm. First tap arms the
    // confirm overlay (label flips to amber "Tap to confirm" for 3 sec); the
    // second tap actually resets the BMA423 pedometer. Either way the user
    // gets a haptic buzz on the tap that's recognized.
    if (idx == 2) {
        Serial.println("[ui] steps button tapped");
        instance.vibrator();
        if (ui_handleStepsTap()) {
            // Confirming second tap — perform the reset and refresh.
            Serial.println("[ui] steps reset confirmed");
            instance.sensor.resetPedometer();
            ui_refreshSteps();
        }
        return;
    }
    // Slots 0 and 3 are regular text prompts — POST the configured prompt
    // text to the host as a watch message.
    doQuickPrompt(idx);
}

// Dedicated callback for the pinned bottom-edge Sleep button. Used to live
// in onQuickPromptPressed as the idx==3 special case, but Sleep moved out of
// the grid (slot 3 reverted to a regular prompt) and got its own widget.
void onSleepButtonPressed() {
    if (currentState() != STATE_HOME) return;
    Serial.println("[ui] sleep button pressed");
    instance.vibrator();
    // CRITICAL: wait for the user's finger to leave the screen before
    // entering light sleep. WAKEUP_SRC_TOUCH_PANEL fires on any active
    // touch — if we sleep while the finger is still down, the panel
    // wakes the chip back up within milliseconds. Poll the touch chip
    // until it reports 0 points (or 3 sec safety timeout).
    int16_t tx = 0, ty = 0;
    uint32_t wait_start = millis();
    while (instance.getPoint(&tx, &ty, 1) > 0 &&
           millis() - wait_start < 3000) {
        delay(10);
    }
    delay(150);  // extra debounce so the touch IRQ has settled
    Serial.println("[ui] finger released, entering sleep");
    enterLightSleep();
}

void onResponseDismissed() {
    setState(STATE_HOME);
    ui_showHome();
}

// =============================================================================
// Work routines
// =============================================================================

static const char* quickPromptText(int idx) {
    switch (idx) {
    case 0: return QUICK_PROMPT_1;
    case 1: return QUICK_PROMPT_2;
    case 2: return QUICK_PROMPT_3;
    case 3: return QUICK_PROMPT_4;
    default: return "";
    }
}

// Compute the RMS (root mean square) amplitude over the PCM body of a
// recordWAV() buffer. RMS is a much better noise gate metric than peak:
// transient sounds like keyboard/mouse clicks have high peaks but low RMS,
// while sustained speech has consistently moderate RMS. Returns 0 if the
// buffer is too small to contain data.
static float wavRmsAmplitude(const uint8_t* wav, size_t wav_size) {
    if (!wav || wav_size <= 44) return 0.0f;
    const int16_t* samples = reinterpret_cast<const int16_t*>(wav + 44);
    size_t sample_count = (wav_size - 44) / sizeof(int16_t);
    if (sample_count == 0) return 0.0f;
    // Use double accumulator — sum of squares for a 5-sec buffer at 16 kHz
    // of loud speech can exceed 2^31.
    double sum_sq = 0.0;
    for (size_t i = 0; i < sample_count; i++) {
        double s = (double)samples[i];
        sum_sq += s * s;
    }
    return (float)sqrt(sum_sq / (double)sample_count);
}

// Scale every 16-bit PCM sample in a recordWAV() buffer by `factor` (0..1).
// Saturates to int16 range. Header bytes (0..43) are untouched.
static void wavAttenuate(uint8_t* wav, size_t wav_size, float factor) {
    if (!wav || wav_size <= 44) return;
    int16_t* samples = reinterpret_cast<int16_t*>(wav + 44);
    size_t sample_count = (wav_size - 44) / sizeof(int16_t);
    for (size_t i = 0; i < sample_count; i++) {
        int32_t scaled = (int32_t)(samples[i] * factor);
        if (scaled >  32767) scaled =  32767;
        if (scaled < -32768) scaled = -32768;
        samples[i] = (int16_t)scaled;
    }
}

// Build a canonical 44-byte PCM WAV header at the start of `wav` describing
// a mono 16-bit PCM stream of `data_bytes` payload at `sample_rate` Hz.
static void writeWavHeader(uint8_t* wav, uint32_t data_bytes, uint32_t sample_rate) {
    const uint16_t channels = 1;
    const uint16_t bits = 16;
    const uint32_t byte_rate = sample_rate * channels * (bits / 8);
    memcpy(wav + 0, "RIFF", 4);
    uint32_t riff_size = data_bytes + 36;
    memcpy(wav + 4,  &riff_size, 4);
    memcpy(wav + 8,  "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    uint32_t fmt_size = 16;
    memcpy(wav + 16, &fmt_size, 4);
    uint16_t audio_fmt = 1;                                // PCM
    memcpy(wav + 20, &audio_fmt, 2);
    memcpy(wav + 22, &channels,  2);
    memcpy(wav + 24, &sample_rate, 4);
    memcpy(wav + 28, &byte_rate,   4);
    uint16_t block_align = channels * (bits / 8);
    memcpy(wav + 32, &block_align, 2);
    memcpy(wav + 34, &bits, 2);
    memcpy(wav + 36, "data", 4);
    memcpy(wav + 40, &data_bytes, 4);
}

static void doVoiceCapture() {
    Serial.println("[voice] ===== doVoiceCapture ENTER =====");
    Serial.printf("[voice] WiFi connected = %d\n", net_isConnected() ? 1 : 0);

    // Immediate "button registered" feedback
    instance.vibrator();
    setState(STATE_RECORDING);
    ui_showRecording();
    lv_task_handler();
    delay(80);

    // Allocate a PSRAM buffer large enough for the max recording length.
    // 44-byte WAV header + 30 sec * 16000 Hz * 2 bytes = ~961 KB.
    const uint32_t sample_rate = 16000;
    const uint32_t byte_rate = sample_rate * 2;  // mono 16-bit
    const uint32_t max_data_bytes = byte_rate * VOICE_RECORD_MAX_SECONDS;
    const size_t total_size = 44 + max_data_bytes;
    uint8_t* wav_buffer = (uint8_t*)ps_malloc(total_size);
    if (!wav_buffer) {
        Serial.printf("[voice] ERROR: ps_malloc(%u) failed\n", (unsigned)total_size);
        setState(STATE_HOME);
        ui_showError("Out of memory");
        return;
    }

    // Stream-read I2S samples into the buffer. Polls g_stopRecording AND
    // g_cancelRecording every chunk so a second button tap (cancel or
    // send zone) breaks the loop immediately.
    g_stopRecording = false;
    g_cancelRecording = false;
    uint8_t* data_ptr = wav_buffer + 44;
    uint32_t bytes_recorded = 0;
    uint32_t start_ms = millis();
    uint32_t last_ui_update_ms = start_ms;
    uint32_t last_displayed_sec = 0;
    // Direct touch-panel polling for stop-tap detection. Going through
    // LVGL's CLICKED event was unreliable here because lv_task_handler only
    // runs once per I2S chunk (~128 ms), and a quick tap can press-and-
    // release entirely between two polls — LVGL never sees it. Reading the
    // touch chip ourselves catches the press transition immediately.
    //
    // Initialise was_touched = true so we IGNORE whatever the touch state
    // happens to be at loop entry (debounces against any lingering release
    // from the start-tap that put us here). Only fresh press transitions
    // observed *during* the loop count.
    bool was_touched = true;
    Serial.println("[voice] streaming recording — tap again to stop");

    while (bytes_recorded < max_data_bytes) {
        size_t want = VOICE_RECORD_CHUNK_BYTES;
        if (bytes_recorded + want > max_data_bytes) {
            want = max_data_bytes - bytes_recorded;
        }
        size_t got = instance.mic.readBytes(
            (char*)(data_ptr + bytes_recorded), want);
        if (got > 0) {
            bytes_recorded += got;
        }

        // Direct touch poll — primary cancel/stop-tap detection. Hit-test
        // the touch coordinate against the speak button zones:
        //   ui_speakBtnHitTest returns -1 outside, 0 cancel, 1 send.
        // Touches OUTSIDE the speak button (e.g. on a quick-task button or
        // the sleep button) are deliberately ignored during recording so
        // the user can't accidentally interrupt themselves by brushing the
        // wrong widget.
        int16_t tx = 0, ty = 0;
        bool is_touched = instance.getPoint(&tx, &ty, 1) > 0;
        if (is_touched && !was_touched) {
            int hit = ui_speakBtnHitTest(tx, ty);
            if (hit == 0) {
                Serial.printf("[voice] CANCEL tap at %d,%d\n", tx, ty);
                g_cancelRecording = true;
            } else if (hit == 1) {
                Serial.printf("[voice] SEND tap at %d,%d\n", tx, ty);
                g_stopRecording = true;
            } else {
                Serial.printf("[voice] touch at %d,%d outside speak btn — ignored\n",
                              tx, ty);
            }
        }
        was_touched = is_touched;

        // Update the elapsed-time label every ~500 ms so the user sees the
        // recording is alive.
        uint32_t now_ms = millis();
        if (now_ms - last_ui_update_ms >= 500) {
            last_ui_update_ms = now_ms;
            uint32_t elapsed_sec = (now_ms - start_ms) / 1000;
            if (elapsed_sec != last_displayed_sec) {
                last_displayed_sec = elapsed_sec;
                ui_setRecordingElapsed(elapsed_sec);
            }
        }

        // Pump LVGL too (in case a slower tap is caught by the CLICKED path)
        // and give FreeRTOS a tick.
        lv_task_handler();
        if (g_cancelRecording) {
            Serial.println("[voice] cancel-tap flag set — breaking out");
            break;
        }
        if (g_stopRecording) {
            Serial.println("[voice] stop-tap flag set — breaking out");
            break;
        }
    }

    uint32_t duration_ms = millis() - start_ms;
    Serial.printf("[voice] recording finished: %u bytes in %u ms\n",
                  (unsigned)bytes_recorded, (unsigned)duration_ms);

    // Cancel path: user tapped the left zone of the split speak button.
    // Free the buffer, give a confirmation buzz, return to home with no
    // POST and no agent run. Must come BEFORE the empty-buffer check so
    // an instant cancel (sub-chunk capture) doesn't show "Mic failed".
    if (g_cancelRecording) {
        Serial.println("[voice] CANCELLED — discarding buffer");
        free(wav_buffer);
        instance.vibrator();
        // CRITICAL: wait for finger release AND clear LVGL's pending touch
        // state before returning home. Without this, the same tap that
        // triggered the cancel produces a queued LVGL CLICKED event on
        // release, which fires after we set STATE_HOME and immediately
        // starts a NEW recording — the user sees the timer "reset to
        // 0:00" instead of returning to the blue idle button.
        int16_t tx = 0, ty = 0;
        uint32_t wait_start = millis();
        while (instance.getPoint(&tx, &ty, 1) > 0 &&
               millis() - wait_start < 3000) {
            delay(10);
        }
        delay(120);
        lv_indev_reset(NULL, NULL);  // discard any queued LVGL touch events
        setState(STATE_HOME);
        ui_showHome();
        return;
    }

    if (bytes_recorded == 0) {
        Serial.println("[voice] ERROR: no bytes captured");
        free(wav_buffer);
        setState(STATE_HOME);
        ui_showError("Mic failed");
        return;
    }

    // Fill in the WAV header now that we know the actual data size.
    writeWavHeader(wav_buffer, bytes_recorded, sample_rate);
    size_t wav_size = 44 + bytes_recorded;

    float rms = wavRmsAmplitude(wav_buffer, wav_size);
    Serial.printf("[voice] RMS amplitude = %.1f (threshold %.1f)\n",
                  rms, (float)MIC_SPEECH_THRESHOLD);
    if (rms < (float)MIC_SPEECH_THRESHOLD) {
        Serial.println("[voice] GATED: below RMS threshold, dropping capture");
        free(wav_buffer);
        setState(STATE_HOME);
        ui_showNoSpeech();
        return;
    }

    // Attenuate before sending. Lowers the clipping the on-chip PDM mic
    // introduces at the default gain, and helps Whisper.
    wavAttenuate(wav_buffer, wav_size, MIC_ATTENUATION);

    setState(STATE_SENDING);
    ui_showSending();
    lv_task_handler();
    Serial.println("[voice] calling net_postAudio...");

    bool ok = net_postAudio(wav_buffer, wav_size, g_replyBuf, sizeof(g_replyBuf));
    free(wav_buffer);   // mic.recordWAV returns a malloc'd buffer
    Serial.printf("[voice] net_postAudio returned ok=%d reply='%.80s'\n",
                  ok ? 1 : 0, g_replyBuf);

    if (ok) {
        // Explicit "message accepted" confirmation: green "✓ Sent" + a second
        // haptic pulse. Held briefly before we swap to the response screen so
        // it actually registers with the user.
        ui_showSent();
        lv_task_handler();
        instance.vibrator();
        delay(SENT_CONFIRM_MS);

        setLastResponse(g_replyBuf);
        setState(STATE_RESPONSE);
        ui_showResponse(g_replyBuf);
        instance.vibrator();
    } else {
        setLastError(g_replyBuf);
        setState(STATE_HOME);
        ui_showError(g_replyBuf);
    }
}

static void doQuickPrompt(int idx) {
    const char* prompt = quickPromptText(idx);
    if (!prompt || !prompt[0]) return;

    setState(STATE_SENDING);
    ui_showSending();
    lv_task_handler();

    bool ok = net_postText(prompt, g_replyBuf, sizeof(g_replyBuf));
    if (ok) {
        setLastResponse(g_replyBuf);
        setState(STATE_RESPONSE);
        ui_showResponse(g_replyBuf);
        instance.vibrator();
    } else {
        setLastError(g_replyBuf);
        setState(STATE_HOME);
        ui_showError(g_replyBuf);
    }
}

static void doPoll() {
    if (!net_isConnected()) return;
    if (currentState() != STATE_HOME) return;   // don't interrupt active work
    if (millis() - g_lastPollMs < POLL_INTERVAL_MS) return;
    g_lastPollMs = millis();

    if (net_pollForResponse(g_replyBuf, sizeof(g_replyBuf))) {
        setLastResponse(g_replyBuf);
        setState(STATE_RESPONSE);
        ui_showResponse(g_replyBuf);
        instance.vibrator();
        touchInteraction();   // wake from impending idle sleep
    }
}

// =============================================================================
// Sleep management
// =============================================================================

static void configureMotionWake() {
    // Configure BMA423 to fire SENSOR wakeup on double-tap, so we can sleep
    // and wake when the user taps the watch.
    instance.sensor.configAccelerometer();
    instance.sensor.enableAccelerometer();
    instance.sensor.disableActivityIRQ();
    instance.sensor.disableAnyNoMotionIRQ();
    instance.sensor.disablePedometerIRQ();
    instance.sensor.disableTiltIRQ();
    instance.sensor.enableFeature(SensorBMA423::FEATURE_WAKEUP, true);
    instance.sensor.enableWakeupIRQ();

    // Pedometer for step display (separate from wake)
    instance.sensor.enablePedometer();
}

static void enterLightSleep() {
    Serial.println("[main] entering light sleep");
    // WAKEUP_SRC_SENSOR is deliberately OMITTED. We tried re-enabling it
    // for tilt-to-wake on 2026-04-10 and confirmed via serial trace that
    // the BMA423 fires its sensor wakeup IRQ within milliseconds of
    // entering sleep — same-second wake-up, screen never visibly dims,
    // user perceives "sleep doesn't work". The simple "just turn it on"
    // approach does NOT work no matter how we filter the events on the
    // wake side.
    //
    // The path forward for tilt-to-wake (filed in project memory) is to
    // use BMA423's dedicated BMA4_WRIST_WEAR_WAKE_UP interrupt (a different
    // sensor feature than generic motion wakeup), which fires only on a
    // deliberate wrist-raise gesture instead of any vibration. That
    // requires investigating the LilyGo SensorBMA423 wrapper API to find
    // the right enable call. Until then, wake on power key + touch only.
    instance.lightSleep((WakeupSource_t)(WAKEUP_SRC_POWER_KEY |
                                         WAKEUP_SRC_TOUCH_PANEL));
    Serial.println("[main] woke from light sleep");
    touchInteraction();
    instance.setBrightness(BRIGHTNESS_ACTIVE);
}

// =============================================================================
// Setup / loop
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== NanoClaw Watch boot ===");

    // Hardware init (display, touch, sensors, mic, PMU, RTC, haptic)
    instance.begin();
    beginLvglHelper(instance);

    // Audio power on (needed for mic + speaker subsystem)
    instance.powerControl(POWER_SPEAK, true);

    // Sensor wake config
    configureMotionWake();

    // Event subscription — power key, sensor events, touch
    instance.onEvent(device_event_cb);

    // Brightness
    instance.setBrightness(BRIGHTNESS_ACTIVE);

    // App state + UI
    state_init();
    ui_init();

    // Network
    net_begin();
    setState(STATE_HOME);
    ui_showHome();

    Serial.println("[main] setup complete");
}

void loop() {
    instance.loop();          // handles hardware events -> device_event_cb
    lv_task_handler();        // LVGL tick

    net_loop();               // WiFi reconnect logic

    // First-time NTP sync once we get WiFi
    static bool ntpDone = false;
    if (!ntpDone && net_isConnected()) {
        net_syncTime();
        ntpDone = true;
    }

    ui_tick();                // refresh clock + battery
    doPoll();                 // poll host for new responses

    // Handle queued power-key sleep request
    if (g_powerKeyPressed) {
        g_powerKeyPressed = false;
        enterLightSleep();
    }

    // Diagnostic: print idle progress every 5 sec while in HOME state.
    // Lets us catch the case where the idle timer is being constantly
    // reset by something we don't expect (touch panel phantom touches,
    // sensor IRQs, etc.) — the printed value should monotonically grow
    // until it hits IDLE_SLEEP_MS and the watch sleeps.
    static uint32_t s_lastIdleDebug = 0;
    if (currentState() == STATE_HOME && millis() - s_lastIdleDebug > 5000) {
        s_lastIdleDebug = millis();
        Serial.printf("[idle] %lu ms idle (threshold %lu ms)\n",
                      (unsigned long)(millis() - lastInteractionMs()),
                      (unsigned long)IDLE_SLEEP_MS);
    }

    // Idle -> light sleep, with two suppressions:
    //  1. A timer is currently counting down. Light sleep would still wake
    //     correctly via touch/power, but the haptic+audio fire pattern at
    //     0:00 wouldn't play until the next wake — possibly minutes late.
    //     Stay awake so the fire pattern lands at the right second.
    //  2. An alarm is enabled and within the next 60 minutes. Same reason —
    //     alarms can't fire from light sleep on this hardware (no wake
    //     workaround that doesn't strobe the screen). Trade-off: "alarm
    //     reliably fires" costs ~1 hour of watch awake-time before each
    //     alarm. Documented in project memory.
    if (currentState() == STATE_HOME &&
        millis() - lastInteractionMs() > IDLE_SLEEP_MS) {
        if (!ui_timerIsRunning() && !ui_alarmIsImminent(60)) {
            enterLightSleep();
        }
    }

    delay(5);
}
