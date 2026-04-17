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

#include <WiFi.h>

#include "config.h"
#include "state.h"
#include "network.h"
#include "ui.h"
#include "settings.h"
#include "gps.h"

// Reply buffer for HTTP responses. Sized to fit typical Jorgenclaw replies
// (multi-paragraph responses can run several KB). Must be at least as large
// as state.cpp's g_responseText to avoid double truncation.
static char g_replyBuf[4096];

// Weather button background refresh state. Refreshed automatically every
// WEATHER_REFRESH_MS while WiFi is up, or immediately on tap.
static uint32_t g_lastWeatherFetchMs = 0;
static uint32_t g_lastWeatherTryMs   = 0;  // even on failure — for backoff
static bool     g_weatherEverFetched = false;
// Backoff after a failed fetch — 60 seconds, so we don't hammer wttr.in
// or block the main loop on every iteration.
static const uint32_t WEATHER_RETRY_MS = 60000;
static uint32_t g_lastPollMs = 0;
static uint32_t g_lastNotifPollMs = 0;
// DND (Do Not Disturb) — when millis() < g_dndUntil, notification buzzes
// are suppressed (banner still shows, just no haptic). Set by the DND
// submenu; 0 = off.
uint32_t g_dndUntil = 0;
static WatchNotification g_notifBuf[3];
static bool g_powerKeyPressed = false;

// Deferred button actions. LVGL 9.x can't reentrantly flush the display
// from within an event callback, so button handlers that do blocking
// work (HTTP POST, touch-poll loops, lv_refr_now) must set a flag and
// return immediately. The main loop() checks the flag and runs the
// work outside the LVGL call stack. Same pattern as g_powerKeyPressed.
enum PendingAction {
    ACTION_NONE = 0,
    ACTION_INBOX,
    ACTION_FIND_PHONE,
    ACTION_NANOCLAW_STATUS,
    ACTION_SCREEN_TEST,
};
static volatile PendingAction g_pendingAction = ACTION_NONE;
// Set to true by the recording loop's direct touch poll (or as a fallback
// by onSpeakButtonPressed via the LVGL CLICK path) when the user taps the
// SEND zone of the speak button while recording. doVoiceCapture() breaks
// out and proceeds to RMS gate + attenuate + POST.
static volatile bool g_stopRecording = false;
// Set to true by the recording loop's direct touch poll when the user taps
// the CANCEL zone (left 40%) of the speak button. doVoiceCapture() breaks
// out, frees the audio buffer, and returns to home WITHOUT POSTing.
static volatile bool g_cancelRecording = false;

// Voice capture intent — dispatches the same recording loop to different
// host endpoints so the agent (or the host itself) knows what the user
// wanted to do with the audio.
//   VOICE_INTENT_CHAT     — regular Speak button, goes to the agent for a
//                           conversational reply
//   VOICE_INTENT_MEMO     — "Capture" grid tile, goes to /api/watch/memo
//                           and gets filed to a daily memo without a chat
//                           round-trip
//   VOICE_INTENT_REMINDER — "Remind" grid tile, goes to /api/watch/reminder
//                           which parses the time and schedules a callback
enum VoiceIntent {
    VOICE_INTENT_CHAT = 0,
    VOICE_INTENT_MEMO,
    VOICE_INTENT_REMINDER,
};

// Forward declarations of work routines
static void doVoiceCapture(VoiceIntent intent = VOICE_INTENT_CHAT);
static void doQuickPrompt(int idx);
static void doPoll();
static void doNotifPoll();
static void doWeatherFetch();
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
    instance.vibrator();
    Serial.printf("[ui] grid button %d tapped\n", idx);

    // Grid order: Weather, Capture, Remind, Clock, Inbox, NextEvent, WiFi,
    // Status, DND, FindPhone, Flashlight, ScreenTest. Must stay in lockstep
    // with GRID_LABELS[] in src/ui.cpp — index here IS the grid slot.
    switch (idx) {
    case 0: // Weather
        setState(STATE_WEATHER);
        ui_showWeather();
        break;
    case 1: // Capture — voice memo (file to daily journal, no chat)
        Serial.println("[ui] capture");
        doVoiceCapture(VOICE_INTENT_MEMO);
        break;
    case 2: // Remind — voice-triggered scheduled reminder
        Serial.println("[ui] remind");
        doVoiceCapture(VOICE_INTENT_REMINDER);
        break;
    case 3: // Clock
        setState(STATE_CLOCK);
        ui_showClock();
        break;
    case 4: // Inbox — deferred (LVGL reentrancy — see doInbox)
        g_pendingAction = ACTION_INBOX;
        break;
    case 5: // Next Event
        Serial.println("[ui] next event — stub");
        // TODO: show next calendar event (once protond works)
        break;
    case 6: // Saved WiFi manager — list + tap-to-forget
        Serial.println("[ui] wifi manager");
        setState(STATE_WIFI_MANAGER);
        ui_showWifiManager();
        break;
    case 7: // NanoClaw Status — deferred
        g_pendingAction = ACTION_NANOCLAW_STATUS;
        break;
    case 8: // DND
        setState(STATE_DND);
        ui_showDnd();
        break;
    case 9: // Find Phone — deferred
        g_pendingAction = ACTION_FIND_PHONE;
        break;
    case 10: { // Flashlight — full white screen, tap to dismiss
        Serial.println("[ui] flashlight on");
        lv_obj_t* flash = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(flash, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(flash, LV_OPA_COVER, 0);
        lv_obj_clear_flag(flash, LV_OBJ_FLAG_SCROLLABLE);
        lv_refr_now(NULL);          // flush pending frames before screen swap
        lv_screen_load(flash);
        lv_refr_now(NULL);          // push the white screen to display immediately
        instance.setBrightness(255);
        // Wait for finger lift (from the button tap), then wait for new tap.
        int16_t tx, ty;
        while (instance.getPoint(&tx, &ty, 1) > 0) delay(10);
        while (instance.getPoint(&tx, &ty, 1) == 0) delay(10);
        while (instance.getPoint(&tx, &ty, 1) > 0) delay(10);
        // Dismiss
        Serial.println("[ui] flashlight off");
        lv_refr_now(NULL);          // flush before teardown
        lv_obj_delete(flash);
        instance.setBrightness(BRIGHTNESS_ACTIVE);
        setState(STATE_HOME);
        ui_showHome();
        lv_refr_now(NULL);          // push home screen immediately
        touchInteraction();
        break;
    }
    case 11: // Screen Test — deferred
        g_pendingAction = ACTION_SCREEN_TEST;
        break;
    }
}

// =============================================================================
// Deferred button handlers — run from loop(), NOT from an LVGL callback.
// These do blocking work (HTTP POST, touch polling, lv_refr_now) that
// LVGL 9.x can't handle reentrantly when invoked from a button event.
// =============================================================================

static void doInbox() {
    Serial.println("[ui] inbox check (deferred)");
    ui_showSending();
    lv_refr_now(NULL);
    char reply[4096];
    bool ok = net_postText(
        "SYSTEM: Scott tapped 'Inbox' on his watch. "
        "Check the Proton Mail inbox and give a brief "
        "summary of unread emails (sender and subject, "
        "max 5). If no unread, say 'Inbox clear'.",
        reply, sizeof(reply));
    if (ok && reply[0]) {
        setLastResponse(reply);
        setState(STATE_RESPONSE);
        ui_showResponse(reply);
    } else if (ok) {
        ui_showError("Inbox clear");
    } else {
        ui_showError("Host unreachable");
    }
    touchInteraction();
}

static void doFindPhone() {
    Serial.println("[ui] find phone (deferred)");
    ui_showSending();
    lv_refr_now(NULL);
    char reply[512];
    bool ok = net_postText(
        "SYSTEM: Scott pressed 'Find Phone' on his watch. "
        "Send a Signal message to Scott that says "
        "'FIND MY PHONE - ring ring!' so his phone buzzes.",
        reply, sizeof(reply));
    if (ok) {
        ui_showSent();
        lv_refr_now(NULL);
        delay(1500);
    } else {
        ui_showError("Host unreachable");
        lv_refr_now(NULL);
        delay(2000);
    }
    setState(STATE_HOME);
    ui_showHome();
    touchInteraction();
}

static void doNanoclawStatus() {
    Serial.println("[ui] nanoclaw status (deferred)");
    if (!net_isConnected()) {
        ui_showError("No WiFi");
        return;
    }
    ui_showSending();
    lv_refr_now(NULL);
    uint32_t t0 = millis();
    char reply[512];
    bool ok = net_postText(
        "SYSTEM: Scott tapped 'Status' on his watch. "
        "Reply with a one-line status: uptime, last agent "
        "run time, and how many messages today.",
        reply, sizeof(reply));
    uint32_t ping_ms = millis() - t0;
    if (ok && reply[0]) {
        char info[600];
        snprintf(info, sizeof(info),
                 "Host: reachable (%lums)\n\n%s",
                 (unsigned long)ping_ms, reply);
        setLastResponse(info);
        setState(STATE_RESPONSE);
        ui_showResponse(info);
    } else {
        char info[128];
        snprintf(info, sizeof(info),
                 "Host: %s (%lums)",
                 ok ? "no response" : "unreachable",
                 (unsigned long)ping_ms);
        ui_showError(info);
    }
    touchInteraction();
}

static void doScreenTest() {
    Serial.println("[ui] screen test (deferred)");
    const uint32_t colors[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000 };
    const char* names[]     = { "RED",    "GREEN",  "BLUE",   "WHITE",  "BLACK" };
    instance.setBrightness(255);
    int16_t tx, ty;
    // Wait for the original button-press finger to lift.
    while (instance.getPoint(&tx, &ty, 1) > 0) delay(10);
    delay(100);
    for (int c = 0; c < 5; c++) {
        lv_obj_t* scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr, lv_color_hex(colors[c]), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* lbl = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        bool dark_bg = (colors[c] == 0x0000FF || colors[c] == 0xFF0000 || colors[c] == 0x000000);
        lv_obj_set_style_text_color(lbl, lv_color_hex(dark_bg ? 0xFFFFFF : 0x000000), 0);
        lv_label_set_text_fmt(lbl, "%s\n\nTap for next", names[c]);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(lbl, 200);
        lv_obj_center(lbl);
        lv_screen_load(scr);
        lv_refr_now(NULL);
        while (instance.getPoint(&tx, &ty, 1) == 0) delay(10);
        while (instance.getPoint(&tx, &ty, 1) > 0) delay(10);
        lv_obj_delete(scr);
    }
    instance.setBrightness(BRIGHTNESS_ACTIVE);
    setState(STATE_HOME);
    ui_showHome();
    lv_refr_now(NULL);
    touchInteraction();
}

static void runPendingAction() {
    PendingAction action = g_pendingAction;
    if (action == ACTION_NONE) return;
    g_pendingAction = ACTION_NONE;
    switch (action) {
        case ACTION_INBOX:            doInbox();          break;
        case ACTION_FIND_PHONE:       doFindPhone();      break;
        case ACTION_NANOCLAW_STATUS:  doNanoclawStatus(); break;
        case ACTION_SCREEN_TEST:      doScreenTest();     break;
        default: break;
    }
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

// The newest notification is cached in g_notifBuf[latest] by doNotifPoll,
// and also cached inside ui.cpp by ui_showNotifBanner. For the detail
// screen we use the most recent entry from our buffer.
static int g_latestNotifIdx = 0;

void onNotifBannerTapped() {
    ui_hideNotifBanner();
    setState(STATE_NOTIFICATION);
    ui_showNotifDetail(g_notifBuf[g_latestNotifIdx].from,
                       g_notifBuf[g_latestNotifIdx].full_text);
}

void onNotifDismissed() {
    setState(STATE_HOME);
    ui_showHome();
}

// When the WiFi manager's "+ Add Network" button triggers the portal,
// we want the portal to return to the manager on close/save instead
// of home, so the user sees the newly-saved network land in the list.
// Set by openConfigPortal(true); read by portal_return_to_origin().
static bool g_portalFromWifiMgr = false;

// Shared return path for both the user-tapped-Close case and the
// main-loop "portal finished" case. Routes to WiFi manager if the
// portal was entered from the manager's Add Network button, otherwise
// back to home screen.
static void portal_return_to_origin() {
    if (g_portalFromWifiMgr) {
        g_portalFromWifiMgr = false;
        setState(STATE_WIFI_MANAGER);
        ui_showWifiManager();
    } else {
        setState(STATE_HOME);
        ui_showHome();
    }
}

// WiFi portal screen — Close button callback.
static void portal_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    Serial.println("[main] portal: user tapped Close");
    net_portalStop();
    portal_return_to_origin();
}

// Build the portal screen and transition into STATE_PORTAL. Callable
// from two entry points:
//   - onWifiLongPress() — the home-screen WiFi indicator long-press
//     (which now first routes through the WiFi manager, but we keep
//     this path in case the manager's button calls us directly)
//   - the WiFi manager's "+ Add Network" button (via extern linkage
//     from ui.cpp; sets from_wifi_mgr=true so the close path routes
//     back to the manager instead of home)
void openConfigPortal(bool from_wifi_mgr) {
    Serial.printf("[main] openConfigPortal(from_wifi_mgr=%d)\n", (int)from_wifi_mgr);
    instance.vibrator();
    g_portalFromWifiMgr = from_wifi_mgr;

    // Start the portal FIRST (non-blocking). If it fails to start we bail
    // without touching the UI.
    if (!net_startPortalAsync()) {
        Serial.println("[main] portal: failed to start, staying on current screen");
        g_portalFromWifiMgr = false;
        return;
    }

    // Build the portal screen: header, instructions, Close button.
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Header
    lv_obj_t* t = lv_label_create(scr);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xE8E8E8), 0);
    lv_label_set_text(t, "WiFi Setup");
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 8, 12);

    // Close button (matches response_screen / clock_screen pattern)
    lv_obj_t* close_btn = lv_obj_create(scr);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 44, 32);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(close_btn, portal_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xE8E8E8), 0);
    lv_obj_center(close_lbl);

    // Body — plain instructions, no misleading side-button hint
    lv_obj_t* body = lv_label_create(scr);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 220);
    lv_label_set_text(body,
        "On your phone, connect\n"
        "to WiFi network:\n\n"
        SETUP_AP_NAME "\n\n"
        "Then open a browser\n"
        "and follow the prompts.\n\n"
        "Tap Close when done.");
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 52);

    lv_screen_load(scr);

    // Enter STATE_PORTAL — the main loop() will pump net_portalProcess()
    // every tick until the portal completes or the user taps Close.
    setState(STATE_PORTAL);
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

static void doVoiceCapture(VoiceIntent intent) {
    const char* intent_name =
        (intent == VOICE_INTENT_MEMO)     ? "MEMO" :
        (intent == VOICE_INTENT_REMINDER) ? "REMINDER" :
                                            "CHAT";
    Serial.printf("[voice] ===== doVoiceCapture ENTER (intent=%s) =====\n",
                  intent_name);
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
    Serial.printf("[voice] calling net_post*Audio (intent=%s)...\n", intent_name);

    bool ok;
    switch (intent) {
        case VOICE_INTENT_MEMO:
            ok = net_postMemoAudio(wav_buffer, wav_size,
                                   g_replyBuf, sizeof(g_replyBuf));
            break;
        case VOICE_INTENT_REMINDER:
            ok = net_postReminderAudio(wav_buffer, wav_size,
                                       g_replyBuf, sizeof(g_replyBuf));
            break;
        case VOICE_INTENT_CHAT:
        default:
            ok = net_postAudio(wav_buffer, wav_size,
                               g_replyBuf, sizeof(g_replyBuf));
            break;
    }
    free(wav_buffer);   // mic.recordWAV returns a malloc'd buffer
    Serial.printf("[voice] post returned ok=%d reply='%.80s'\n",
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

static void doWeatherFetch() {
    g_lastWeatherTryMs = millis();
    if (!net_isConnected()) {
        Serial.println("[weather] skip — no WiFi");
        ui_setWeatherError("no WiFi");
        return;
    }
    Serial.println("[weather] fetching");
    ui_setWeatherPending();
    WeatherData data;
    char err_buf[16] = {0};
    if (net_fetchWeather(&data, err_buf, sizeof(err_buf))) {
        ui_setWeatherData(data);
        g_weatherEverFetched = true;
        g_lastWeatherFetchMs = millis();
    } else {
        ui_setWeatherError(err_buf[0] ? err_buf : "fail");
    }
}

// Bridge for the weather sub-screen's Refresh button. ui.cpp can't call
// doWeatherFetch() directly (that would be a circular include); it calls
// this trampoline instead.
void doWeatherFetchTriggered() {
    doWeatherFetch();
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

static void doNotifPoll() {
    if (!net_isConnected()) return;
    if (currentState() != STATE_HOME) return;
    if (millis() - g_lastNotifPollMs < NOTIF_POLL_INTERVAL_MS) return;
    g_lastNotifPollMs = millis();

    int count = net_pollNotifications(settings_getLastNotifTimestamp(),
                                      g_notifBuf, 3);
    if (count <= 0) return;

    // Update high-water mark to the newest timestamp received (persisted to
    // NVS so reboots don't re-fetch old notifications).
    settings_setLastNotifTimestamp(g_notifBuf[count - 1].timestamp);

    // Show banner for the most recent notification.
    g_latestNotifIdx = count - 1;
    WatchNotification& newest = g_notifBuf[g_latestNotifIdx];
    ui_showNotifBanner(newest.from, newest.preview, newest.full_text);

    // Double-buzz notification pattern (distinct from single confirmation buzz).
    // Suppressed during DND — banner still shows, just no haptic.
    if (g_dndUntil == 0 || millis() >= g_dndUntil) {
        instance.vibrator();
        delay(150);
        instance.vibrator();
    } else {
        Serial.println("[notif] DND active — suppressing buzz");
    }

    touchInteraction();  // prevent imminent idle-sleep
    Serial.printf("[notif] showing: %s — %s\n", newest.from, newest.preview);
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
    instance.setBrightness(BRIGHTNESS_ACTIVE);
    touchInteraction();
}

// =============================================================================
// Setup / loop
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== NanoClaw Watch boot ===");

    // Persistent app settings (NVS-backed). Load BEFORE ui_init so the
    // first paint of any screen reflects the right unit/etc.
    settings_load();

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

    // GPS (T-Watch-S3-Plus only; no-op on base S3 if disabled in settings).
    // Honors the persisted opt-in flag — default off on first boot.
    gps_init();

    // App state + UI
    state_init();
    ui_init();

    // Network — may block if no saved credentials (captive portal).
    net_begin();
    setState(STATE_HOME);
    ui_showHome();

    Serial.println("[main] setup complete");
}

void loop() {
    instance.loop();          // handles hardware events -> device_event_cb
    lv_task_handler();        // LVGL tick

    // Deferred button actions — queued by LVGL event callbacks that
    // can't safely run blocking work (HTTP, touch polling, lv_refr_now)
    // from inside the callback. See enum PendingAction above.
    runPendingAction();

    // Non-blocking WiFi config portal — pump every tick while active.
    // When process() flags the portal as done (user saved creds OR
    // timeout), tear it down and return to wherever the user came
    // from (home or WiFi manager, per portal_return_to_origin()).
    // User-initiated close is handled by portal_close_cb which takes
    // the same return path.
    if (currentState() == STATE_PORTAL) {
        net_portalProcess();
        if (!net_portalIsActive()) {
            Serial.printf("[main] portal: finished (saved=%d)\n",
                          (int)net_portalDidSave());
            net_portalStop();
            portal_return_to_origin();
            if (net_isConnected()) net_syncTime();
        }
    }

    net_loop();               // WiFi reconnect logic

    // Hourly NTP resync to counter RTC drift (~2-4 min/day on ESP32).
    // Initial sync and reconnect sync are handled inside net_loop();
    // this is belt-and-suspenders for watches that stay connected for
    // days — without it, drift is only corrected on a WiFi hiccup.
    static uint32_t lastNtpResyncMs = 0;
    if (net_isConnected() &&
        (lastNtpResyncMs == 0 ||
         millis() - lastNtpResyncMs > NTP_RESYNC_INTERVAL_MS)) {
        // Skip the very first call here — net_loop already synced on
        // the reconnect transition. Just start the timer.
        if (lastNtpResyncMs != 0) {
            net_syncTime();
        }
        lastNtpResyncMs = millis();
    }

    ui_tick();                // refresh clock + battery
    gps_loop();               // pump NMEA parser when GPS is enabled
    doPoll();                 // poll host for new responses
    doNotifPoll();            // poll host for proactive notifications

    // Weather background refresh. Two timers:
    //   - g_lastWeatherFetchMs: time of last SUCCESSFUL fetch. Drives the
    //     30-minute auto-refresh interval.
    //   - g_lastWeatherTryMs: time of last ATTEMPT (success or fail).
    //     Used as a 60-sec backoff so failed fetches don't hammer the
    //     wttr.in server (and don't block the main loop on every iteration).
    if (currentState() == STATE_HOME && net_isConnected()) {
        bool need_first      = !g_weatherEverFetched && g_lastWeatherTryMs == 0;
        bool need_retry      = !g_weatherEverFetched && g_lastWeatherTryMs != 0 &&
                               (millis() - g_lastWeatherTryMs > WEATHER_RETRY_MS);
        bool need_refresh    = g_weatherEverFetched &&
                               (millis() - g_lastWeatherFetchMs > WEATHER_REFRESH_MS);
        if (need_first || need_retry || need_refresh) {
            doWeatherFetch();
        }
    }

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
        if (!ui_timerIsRunning() && !ui_alarmIsImminent(60) && !ui_pomodoroIsRunning()) {
            enterLightSleep();
        }
    }

    delay(5);
}
