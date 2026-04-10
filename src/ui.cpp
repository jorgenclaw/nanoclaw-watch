#include "ui.h"
#include "config.h"
#include "state.h"
#include "network.h"

#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <time.h>

// =============================================================================
// LVGL UI for NanoClaw Watch
//
// Two screens:
//   home_screen      — clock, battery, WiFi, big speak button, 4 quick presets
//   response_screen  — scrollable text area for the agent's reply
//
// "Recording" and "Sending" are transient overlays drawn on the home screen
// (label changes + spinner). State machine in state.cpp drives what's shown.
// =============================================================================

static lv_obj_t* home_screen      = nullptr;
static lv_obj_t* response_screen  = nullptr;

// Home screen widgets
static lv_obj_t* lbl_time         = nullptr;
static lv_obj_t* lbl_date         = nullptr;
static lv_obj_t* lbl_battery      = nullptr;
static lv_obj_t* lbl_wifi         = nullptr;
static lv_obj_t* lbl_steps        = nullptr;  // BMA423 pedometer count, top bar centered
static lv_obj_t* lbl_status       = nullptr;  // secondary status line (errors / "No speech")
static lv_obj_t* speak_btn        = nullptr;
static lv_obj_t* speak_lbl        = nullptr;  // the label *inside* the speak button — primary indicator
static lv_obj_t* quick_btns[4]    = {nullptr, nullptr, nullptr, nullptr};

// Response screen widgets
static lv_obj_t* response_text    = nullptr;

static const char* QUICK_LABELS[4] = {
    "Today?",
    "Messages?",
    "Workshop?",
    "Sleep",          // index 3 — manual screen-off, see onQuickPromptPressed
};

// --- Event callbacks ---

static void speak_event_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        touchInteraction();
        onSpeakButtonPressed();
    }
}

static void quick_event_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    Serial.printf("[ui] quick_event_cb CLICKED idx=%d\n", idx);
    touchInteraction();
    onQuickPromptPressed(idx);
}

static void response_dismiss_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    onResponseDismissed();
}

// --- Screen builders ---

static void build_home_screen() {
    home_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(home_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(home_screen, lv_color_hex(0xE8E8E8), 0);

    // Top status bar — battery + WiFi
    lbl_battery = lv_label_create(home_screen);
    lv_label_set_text(lbl_battery, "BAT --%");
    lv_obj_set_style_text_font(lbl_battery, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_battery, LV_ALIGN_TOP_LEFT, 8, 6);

    lbl_wifi = lv_label_create(home_screen);
    lv_label_set_text(lbl_wifi, "WiFi -");
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_TOP_RIGHT, -8, 6);

    // Step counter — center of the top status bar between battery (left) and
    // WiFi (right). Updated alongside battery on the 30-sec cadence so we
    // don't hammer the BMA423 over I2C every loop iteration.
    lbl_steps = lv_label_create(home_screen);
    lv_label_set_text(lbl_steps, "0 steps");
    lv_obj_set_style_text_font(lbl_steps, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_steps, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_steps, LV_ALIGN_TOP_MID, 0, 8);

    // Clock — large
    lbl_time = lv_label_create(home_screen);
    lv_label_set_text(lbl_time, "--:--");
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 28);

    // Date — small below clock
    lbl_date = lv_label_create(home_screen);
    lv_label_set_text(lbl_date, "");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_date, LV_ALIGN_TOP_MID, 0, 80);

    // Speak button — use a plain lv_obj instead of lv_button so we get no
    // inherited theme styles at all. lv_button_create pulls in the default
    // theme's button styles (gradients, transitions, press animations) which
    // were visibly overriding my local bg_color changes. A plain obj with
    // manual styling gives us a clean flat button that actually responds to
    // bg_color updates.
    speak_btn = lv_obj_create(home_screen);
    lv_obj_remove_style_all(speak_btn);                       // nuke inherited theme
    lv_obj_set_size(speak_btn, 180, 44);
    lv_obj_align(speak_btn, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_bg_opa(speak_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(speak_btn, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_radius(speak_btn, 22, 0);
    lv_obj_set_style_border_width(speak_btn, 0, 0);
    lv_obj_set_style_pad_all(speak_btn, 0, 0);
    lv_obj_add_flag(speak_btn, LV_OBJ_FLAG_CLICKABLE);        // plain obj isn't clickable by default
    lv_obj_clear_flag(speak_btn, LV_OBJ_FLAG_SCROLLABLE);     // don't intercept scrolls
    lv_obj_add_event_cb(speak_btn, speak_event_cb, LV_EVENT_ALL, NULL);

    speak_lbl = lv_label_create(speak_btn);
    lv_label_set_text(speak_lbl, "Tap to speak");
    lv_obj_set_style_text_color(speak_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(speak_lbl);

    // Secondary status line — only shown for errors / "No speech detected".
    // Anchored to the very bottom of the screen so it doesn't collide with
    // the quick-tap button grid. Primary state feedback lives on the speak
    // button itself (text + color).
    lbl_status = lv_label_create(home_screen);
    lv_label_set_text(lbl_status, "");
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -6);

    // Quick-tap preset row (2x2 grid)
    int btn_w = 90, btn_h = 28;
    int spacing = 6;
    int row_y = 158;
    int cols[2] = { -btn_w/2 - spacing/2, btn_w/2 + spacing/2 };
    for (int i = 0; i < 4; i++) {
        int row = i / 2;
        int col = i % 2;
        quick_btns[i] = lv_button_create(home_screen);
        lv_obj_set_size(quick_btns[i], btn_w, btn_h);
        lv_obj_align(quick_btns[i], LV_ALIGN_TOP_MID, cols[col], row_y + row * (btn_h + spacing));
        lv_obj_set_style_bg_color(quick_btns[i], lv_color_hex(0x222222), 0);
        lv_obj_set_style_radius(quick_btns[i], 6, 0);
        lv_obj_add_event_cb(quick_btns[i], quick_event_cb, LV_EVENT_ALL, (void*)(intptr_t)i);
        lv_obj_t* lbl = lv_label_create(quick_btns[i]);
        lv_label_set_text(lbl, QUICK_LABELS[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
    }
}

static void build_response_screen() {
    response_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(response_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(response_screen, lv_color_hex(0xE8E8E8), 0);

    // App-name header in the top-left corner so it's visually clear which
    // screen the user is on. Small label, dim color — informational only,
    // not a tap target.
    lv_obj_t* header = lv_label_create(response_screen);
    lv_label_set_text(header, "Jorgenclaw");
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(header, lv_color_hex(0x888888), 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 8, 12);

    // Dedicated close button in the top-right corner. Replaces the previous
    // "tap anywhere to dismiss" handler — that fired LV_EVENT_CLICKED at the
    // end of every touch gesture including scrolls, so trying to scroll the
    // reply text would accidentally close the screen.
    lv_obj_t* close_btn = lv_obj_create(response_screen);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 44, 32);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(close_btn, response_dismiss_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xE8E8E8), 0);
    lv_obj_center(close_lbl);

    // Scrollable response text. Padding leaves room for the close button at
    // the top and gives the body its own scroll surface.
    response_text = lv_label_create(response_screen);
    lv_label_set_long_mode(response_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(response_text, LV_PCT(92));
    lv_obj_set_style_text_font(response_text, &lv_font_montserrat_14, 0);
    lv_label_set_text(response_text, "");
    lv_obj_align(response_text, LV_ALIGN_TOP_LEFT, 8, 44);
}

// --- Public API ---

void ui_init() {
    build_home_screen();
    build_response_screen();
    lv_screen_load(home_screen);
}

// Drive the speak button's color + label to communicate the current state.
// This is the primary feedback channel — it's where the user's eyes already
// are when they're interacting with the watch.
static void set_speak_button(const char* text, uint32_t bg_color) {
    Serial.printf("[ui] set_speak_button text='%s' color=0x%06lX\n",
                  text, (unsigned long)bg_color);
    if (!speak_btn || !speak_lbl) {
        Serial.println("[ui] ERROR: speak_btn/speak_lbl is NULL!");
        return;
    }
    lv_label_set_text(speak_lbl, text);
    lv_obj_set_style_bg_color(speak_btn, lv_color_hex(bg_color), 0);

    // Read back the stored bg_color to confirm LVGL actually accepted the
    // style change. If the readback doesn't match, the issue is in the style
    // write path (caching, theme override). If it matches but the screen
    // doesn't update, the issue is purely render/refresh.
    lv_color_t stored = lv_obj_get_style_bg_color(speak_btn, LV_PART_MAIN);
    Serial.printf("[ui]   readback bg_color = 0x%02X%02X%02X\n",
                  stored.red, stored.green, stored.blue);

    // Nuclear refresh: invalidate the object AND force an immediate render.
    lv_obj_invalidate(speak_btn);
    lv_refr_now(NULL);
}

// Secondary status line at the bottom of the screen — only used for errors.
static void set_status(const char* text, uint32_t color) {
    lv_label_set_text(lbl_status, text);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(color), 0);
}

void ui_showHome() {
    set_speak_button("Tap to speak", 0x3B82F6);  // blue
    set_status("", 0x888888);
    lv_screen_load(home_screen);
}

void ui_showRecording() {
    set_speak_button("● 0:00 Tap to send", 0xEF4444);  // red
    set_status("", 0x888888);
    lv_screen_load(home_screen);
}

void ui_setRecordingElapsed(uint32_t seconds) {
    if (!speak_lbl) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "● 0:%02u Tap to send", (unsigned)seconds);
    lv_label_set_text(speak_lbl, buf);
    // Same display-flush dance the rest of the UI uses — invalidate alone
    // doesn't push pixels on this watch's LVGL setup; lv_refr_now does.
    lv_obj_invalidate(speak_lbl);
    lv_refr_now(NULL);
}

void ui_showSending() {
    set_speak_button("Sending...", 0xFBBF24);      // amber
    set_status("", 0x888888);
    lv_screen_load(home_screen);
}

void ui_showSent() {
    set_speak_button("Sent", 0x22C55E);            // green
    set_status("", 0x888888);
    lv_screen_load(home_screen);
}

void ui_showNoSpeech() {
    // Button returns to "tap to speak" blue so another attempt is obviously
    // available; the reason is explained on the bottom status line.
    set_speak_button("Tap to speak", 0x3B82F6);
    set_status("No speech — try again", 0xFBBF24);  // amber
    lv_screen_load(home_screen);
}

void ui_showResponse(const char* text) {
    lv_label_set_text(response_text, text ? text : "(no reply)");
    lv_screen_load(response_screen);
}

void ui_showError(const char* text) {
    set_speak_button("Tap to speak", 0x3B82F6);
    set_status(text ? text : "Error", 0xEF4444);   // red
    lv_screen_load(home_screen);
}

// --- Periodic refresh ---

static uint32_t s_lastClockUpdate = 0;
static uint32_t s_lastBatteryUpdate = 0;

static void update_clock() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    if (!localtime_r(&now, &timeinfo) || timeinfo.tm_year < 70) {
        lv_label_set_text(lbl_time, "--:--");
        lv_label_set_text(lbl_date, "syncing...");
        return;
    }
    char buf[24];

    // Time format: 12-hour with AM/PM, no leading zero on the hour ("9:14 PM").
    // Built manually for the same reason the date is — newlib's strftime
    // doesn't support the GNU "%-I" no-leading-zero extension and would
    // print a literal "?" in its place.
    int hour12 = timeinfo.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = (timeinfo.tm_hour < 12) ? "AM" : "PM";
    snprintf(buf, sizeof(buf), "%d:%02d %s", hour12, timeinfo.tm_min, ampm);
    lv_label_set_text(lbl_time, buf);

    // Date format: "Thu Apr 9". Built manually because newlib (which ESP32
    // ships) does NOT support the GNU "%-d" extension for day-of-month
    // without a leading zero — strftime emits a literal "?" for unknown
    // specifiers, which is exactly what was showing up on screen.
    char wd_mo[12];
    strftime(wd_mo, sizeof(wd_mo), "%a %b", &timeinfo);
    snprintf(buf, sizeof(buf), "%s %d", wd_mo, timeinfo.tm_mday);
    lv_label_set_text(lbl_date, buf);
}

static void update_battery() {
    int pct = instance.pmu.getBatteryPercent();
    bool charging = instance.pmu.isCharging();
    char buf[24];
    snprintf(buf, sizeof(buf), "%s%d%%", charging ? "+" : "BAT ", pct);
    lv_label_set_text(lbl_battery, buf);

    lv_label_set_text(lbl_wifi, net_isConnected() ? "WiFi OK" : "WiFi -");

    // Step count from the BMA423 pedometer (already enabled in setup() via
    // configureMotionWake → instance.sensor.enablePedometer()).
    uint32_t steps = instance.sensor.getPedometerCounter();
    snprintf(buf, sizeof(buf), "%lu steps", (unsigned long)steps);
    lv_label_set_text(lbl_steps, buf);
}

void ui_tick() {
    uint32_t now = millis();
    if (now - s_lastClockUpdate > 1000) {
        s_lastClockUpdate = now;
        update_clock();
    }
    if (now - s_lastBatteryUpdate > 30000) {
        s_lastBatteryUpdate = now;
        update_battery();
    }
}
