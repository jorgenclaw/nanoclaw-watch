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
static lv_obj_t* lbl_steps        = nullptr;  // inner label of quick_btns[2] (Steps button)
static lv_obj_t* speak_btn        = nullptr;
static lv_obj_t* speak_lbl        = nullptr;  // the label *inside* the speak button — primary indicator
static lv_obj_t* cancel_lbl       = nullptr;  // left-half "X Cancel" label, only visible during recording
static lv_obj_t* send_lbl         = nullptr;  // right-half "0:00 Send" label, only visible during recording
static lv_obj_t* quick_btns[4]    = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t* sleep_btn        = nullptr;  // pinned bottom-edge full-width Sleep button

// Clock sub-screen (loaded when user taps the Clock quick button — slot 1).
static lv_obj_t* clock_screen     = nullptr;

// ===== Clock sub-feature state (alarm / timer / stopwatch) =====
// State lives here in ui.cpp because it's tightly coupled to the LVGL
// labels these features update. ui_clockTick() — called from ui_tick() —
// drives the periodic display refresh and the timer/alarm fire conditions.
// Stopwatch and timer keep running in the background even when their
// screens are closed; alarm stays armed across screen close.

// --- Stopwatch ---
static bool      g_swRunning      = false;
static uint32_t  g_swElapsedMs    = 0;       // accumulated when paused
static uint32_t  g_swStartMs      = 0;       // millis() at most recent start
static lv_obj_t* stopwatch_screen = nullptr;
static lv_obj_t* sw_time_lbl      = nullptr;
static lv_obj_t* sw_start_lbl     = nullptr; // text inside start/pause btn

// --- Timer ---
static bool      g_timerRunning      = false;
static int       g_timerSetMinutes   = 5;    // user-set duration: minutes
static int       g_timerSetSeconds   = 0;    // user-set duration: extra seconds (0-59)
static uint32_t  g_timerStartMs      = 0;
static int       g_timerRemainingSec = 5 * 60;
static lv_obj_t* timer_screen        = nullptr;
static lv_obj_t* tm_time_lbl         = nullptr;
static lv_obj_t* tm_start_lbl        = nullptr;

// --- Alarm ---
static int       g_alarmHour          = 7;
static int       g_alarmMinute        = 30;
static bool      g_alarmEnabled       = false;
// tm_yday at last fire — prevents the alarm from firing repeatedly within
// the same minute (or same day if user dismisses and reopens).
static int       g_alarmLastFiredYday = -1;
static lv_obj_t* alarm_screen         = nullptr;
static lv_obj_t* al_time_lbl          = nullptr;
static lv_obj_t* al_status_lbl        = nullptr;
static lv_obj_t* al_toggle_lbl        = nullptr;

// Steps button "tap to confirm" state. First tap arms; second tap within
// STEPS_CONFIRM_TIMEOUT_MS commits the reset; otherwise the confirm expires
// and the label snaps back to the live count. State lives in ui.cpp because
// the label color/text is what represents it.
static bool g_stepsConfirmPending = false;
static uint32_t g_stepsConfirmExpiresMs = 0;
static const uint32_t STEPS_CONFIRM_TIMEOUT_MS = 3000;

// Response screen widgets
static lv_obj_t* response_text    = nullptr;

static const char* QUICK_LABELS[4] = {
    "Today?",
    "Clock",          // index 1 — opens the clock sub-screen
    "0 steps",        // index 2 — live BMA423 pedometer count, tap-twice to reset
    "Messages?",      // index 3 — regular text prompt (Sleep moved to its own pinned button)
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

static void sleep_event_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    Serial.println("[ui] sleep_event_cb CLICKED");
    touchInteraction();
    onSleepButtonPressed();
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
    // CRITICAL: lv_obj_create makes containers scrollable by default. With
    // a scrollable home screen, LVGL interprets touches near the bottom
    // edge as scroll-up gestures instead of clicks — which silently broke
    // the pinned Sleep button at y=212-238. Home screen has no scrollable
    // content, so just turn it off.
    lv_obj_clear_flag(home_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Top status bar — battery + WiFi
    lbl_battery = lv_label_create(home_screen);
    lv_label_set_text(lbl_battery, "BAT --%");
    lv_obj_set_style_text_font(lbl_battery, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_battery, LV_ALIGN_TOP_LEFT, 8, 6);

    lbl_wifi = lv_label_create(home_screen);
    lv_label_set_text(lbl_wifi, "WiFi -");
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_TOP_RIGHT, -8, 6);

    // Step counter used to live in the top bar center, but it moved into the
    // quick-button grid (slot 2 — bottom-left) so it's a tap target that
    // resets the pedometer on tap. The lbl_steps pointer below gets
    // assigned later inside the quick-button loop. See onQuickPromptPressed
    // in main.cpp for the reset handler.

    // Clock — large
    lbl_time = lv_label_create(home_screen);
    lv_label_set_text(lbl_time, "--:--");
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 28);

    // Date — top center, between battery (top-left) and WiFi (top-right).
    // Was previously below the clock at y=80 but Scott moved it to the top
    // line to fill the slot the step counter used to occupy. Same font as
    // the battery/wifi indicators so the top bar reads as one unit.
    lbl_date = lv_label_create(home_screen);
    lv_label_set_text(lbl_date, "");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_date, LV_ALIGN_TOP_MID, 0, 6);

    // Speak button — use a plain lv_obj instead of lv_button so we get no
    // inherited theme styles at all. lv_button_create pulls in the default
    // theme's button styles (gradients, transitions, press animations) which
    // were visibly overriding my local bg_color changes. A plain obj with
    // manual styling gives us a clean flat button that actually responds to
    // bg_color updates.
    speak_btn = lv_obj_create(home_screen);
    lv_obj_remove_style_all(speak_btn);                       // nuke inherited theme
    lv_obj_set_size(speak_btn, 180, 44);
    // Pulled up from y-offset +8 to -10 so the quick-button grid + pinned
    // Sleep button below it have room to breathe without overlapping.
    lv_obj_align(speak_btn, LV_ALIGN_CENTER, 0, -10);
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

    // Split-button cancel/send labels — only visible during STATE_RECORDING.
    // The button stays a single tap target with one red background; the
    // recording loop's direct touch poll hit-tests the x coordinate via
    // ui_speakBtnHitTest() to distinguish a cancel-zone tap (left 40%)
    // from a send-zone tap (right 60%). Labels are children of speak_btn
    // and inherit the touch behavior of their parent (labels themselves
    // aren't clickable so they don't intercept events).
    cancel_lbl = lv_label_create(speak_btn);
    lv_label_set_text(cancel_lbl, "X Cancel");
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(cancel_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_flag(cancel_lbl, LV_OBJ_FLAG_HIDDEN);

    send_lbl = lv_label_create(speak_btn);
    lv_label_set_text(send_lbl, "0:00 Send");
    lv_obj_set_style_text_color(send_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(send_lbl, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_flag(send_lbl, LV_OBJ_FLAG_HIDDEN);

    // (Bottom status line removed — error / "No speech" feedback is now
    // shown directly on the speak button label, freeing the bottom edge of
    // the screen for the pinned Sleep button below.)

    // Quick-tap preset row (2x2 grid). Layout from top:
    //   speak_btn   y= 88-132   (180x44, center -10)
    //   row 0        y=140-168  (90x28)
    //   row 1        y=174-202  (90x28)
    //   sleep_btn   y=210-238   (200x28, pinned bottom)
    // The 8 px gap between row 1 and the sleep button is the visible
    // separation Scott asked for; previously they touched/overlapped.
    int btn_w = 90, btn_h = 28;
    int spacing = 6;
    int row_y = 140;
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
        // Slot 2 (bottom-left) is the Steps button. Stash a reference to its
        // inner label so update_battery() can refresh the step count each
        // tick without looking the widget up again. Tap handling is in
        // main.cpp::onQuickPromptPressed (which calls resetPedometer).
        if (i == 2) {
            lbl_steps = lbl;
        }
    }

    // Pinned Sleep button — sits at the very bottom edge of the screen,
    // outside the quick-button grid. Wider than a grid button so it's an
    // easy single-tap target. Uses a plain lv_obj (not lv_button) for the
    // same reason the speak button does — themed buttons override our
    // local bg_color setting.
    sleep_btn = lv_obj_create(home_screen);
    lv_obj_remove_style_all(sleep_btn);
    lv_obj_set_size(sleep_btn, 200, 28);
    lv_obj_align(sleep_btn, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_opa(sleep_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sleep_btn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(sleep_btn, 8, 0);
    lv_obj_set_style_border_width(sleep_btn, 0, 0);
    lv_obj_set_style_pad_all(sleep_btn, 0, 0);
    lv_obj_add_flag(sleep_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(sleep_btn, LV_OBJ_FLAG_SCROLLABLE);
    // Extend the click area 8 px in every direction so taps that land just
    // outside the visible button still register. Helps because the watch's
    // round display clips the bottom-edge corners and the touch panel can
    // be finicky there.
    lv_obj_set_ext_click_area(sleep_btn, 8);
    lv_obj_add_event_cb(sleep_btn, sleep_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* sleep_lbl = lv_label_create(sleep_btn);
    lv_label_set_text(sleep_lbl, "Sleep");
    lv_obj_set_style_text_font(sleep_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sleep_lbl, lv_color_hex(0xE8E8E8), 0);
    lv_obj_center(sleep_lbl);
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

// --- Clock sub-screen (pattern C — reusable sub-screen template) ---
//
// Loaded when the user taps the Clock quick button (slot 1, top-right).
// Currently has stub buttons for Alarm / Timer / Stopwatch — they don't
// do anything yet but the screen, header, and Close button are wired so
// the same template can be reused for other sub-screens (settings,
// network info, etc.) without duplicating boilerplate.

static void clock_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    setState(STATE_HOME);
    ui_showHome();
}

static void clock_stub_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    // Dispatch by index passed via user_data: 0 = Alarm, 1 = Timer,
    // 2 = Stopwatch. Each loads its own sub-sub-screen built in
    // build_alarm_screen / build_timer_screen / build_stopwatch_screen.
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    Serial.printf("[ui] clock sub-button tapped idx=%d\n", idx);
    switch (idx) {
        case 0: ui_showAlarm();     break;
        case 1: ui_showTimer();     break;
        case 2: ui_showStopwatch(); break;
    }
}

static void build_clock_screen() {
    clock_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(clock_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(clock_screen, lv_color_hex(0xE8E8E8), 0);

    // Header label, top-left
    lv_obj_t* hdr = lv_label_create(clock_screen);
    lv_label_set_text(hdr, "Clock");
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_18, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 12, 12);

    // Close button, top-right (same shape/style as the response screen one)
    lv_obj_t* close_btn = lv_obj_create(clock_screen);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 56, 32);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(close_btn, clock_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xE8E8E8), 0);
    lv_obj_center(close_lbl);

    // Three action buttons stacked vertically — each loads a sub-sub-screen
    // (Alarm/Timer/Stopwatch) via clock_stub_cb dispatching on the index
    // passed as user_data. Buttons are wide (200x42) for easy tapping.
    const char* labels[3] = { "Alarm", "Timer", "Stopwatch" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* btn = lv_obj_create(clock_screen);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, 200, 42);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 60 + i * 52);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, clock_stub_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)i);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8E8E8), 0);
        lv_obj_center(lbl);
    }
}

// --- Reusable builders for sub-sub-screens (Alarm/Timer/Stopwatch) ---

// Standard top-right Close button — same shape and style on every sub-screen.
// `cb` is invoked on click; tap behavior is the caller's responsibility
// (typically: setState(STATE_CLOCK) + ui_showClock to return to the parent).
static lv_obj_t* make_close_btn(lv_obj_t* parent, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 56, 32);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Close");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8E8E8), 0);
    lv_obj_center(lbl);
    return btn;
}

// Title label, top-left, font 18 — matches the Clock parent screen.
static void make_title(lv_obj_t* parent, const char* text) {
    lv_obj_t* hdr = lv_label_create(parent);
    lv_label_set_text(hdr, text);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_18, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 12, 12);
}

// Reusable action button (full width 200x40). `out_label` receives the
// inner label pointer if the caller needs to update its text later.
static lv_obj_t* make_action_btn(lv_obj_t* parent, const char* text,
                                 int y_offset, uint32_t bg_color,
                                 lv_event_cb_t cb, lv_obj_t** out_label) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 200, 40);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, y_offset);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);
    if (out_label) *out_label = lbl;
    return btn;
}

// Small +/- picker button (32x32). Returns the button so caller can wire
// click events.
static lv_obj_t* make_picker_btn(lv_obj_t* parent, const char* text,
                                 int x_offset, int y_offset,
                                 lv_event_cb_t cb) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 36, 36);
    lv_obj_align(btn, LV_ALIGN_CENTER, x_offset, y_offset);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);
    return btn;
}

// --- Audio beep (used by alarm + timer fire patterns) ---
//
// Builds a 250 ms WAV in RAM (16 kHz mono 16-bit, 1 kHz square wave) and
// hands it to the I2S player. Synchronous — playWAV blocks until the
// buffer is drained, so this returns ~250 ms after being called. The
// speaker is powered via POWER_SPEAK in main.cpp::setup().
static void play_alarm_beep() {
    const uint32_t sample_rate  = 16000;
    const uint32_t freq_hz      = 1000;
    const uint32_t duration_ms  = 250;
    const uint32_t num_samples  = (sample_rate * duration_ms) / 1000;
    const uint32_t data_bytes   = num_samples * sizeof(int16_t);
    const uint32_t wav_size     = 44 + data_bytes;

    uint8_t* buf = (uint8_t*)malloc(wav_size);
    if (!buf) {
        Serial.println("[audio] beep malloc failed");
        return;
    }

    // PCM WAV header — same shape as the firmware's writeWavHeader in
    // main.cpp, just inlined here so this helper is self-contained.
    memcpy(buf + 0, "RIFF", 4);
    uint32_t riff_size = data_bytes + 36;
    memcpy(buf + 4,  &riff_size, 4);
    memcpy(buf + 8,  "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    uint32_t fmt_size  = 16;
    memcpy(buf + 16, &fmt_size, 4);
    uint16_t audio_fmt = 1;     // PCM
    memcpy(buf + 20, &audio_fmt, 2);
    uint16_t channels  = 1;
    memcpy(buf + 22, &channels,  2);
    memcpy(buf + 24, &sample_rate, 4);
    uint32_t byte_rate = sample_rate * sizeof(int16_t);
    memcpy(buf + 28, &byte_rate,   4);
    uint16_t block_align = sizeof(int16_t);
    memcpy(buf + 32, &block_align, 2);
    uint16_t bits = 16;
    memcpy(buf + 34, &bits, 2);
    memcpy(buf + 36, "data", 4);
    memcpy(buf + 40, &data_bytes, 4);

    // 1 kHz square wave samples — square is louder/more attention-grabbing
    // than sine through the small piezo speaker.
    int16_t* samples = reinterpret_cast<int16_t*>(buf + 44);
    const int half_period = (sample_rate / freq_hz) / 2;  // 8 samples
    for (uint32_t i = 0; i < num_samples; i++) {
        bool high = ((i / half_period) % 2) == 0;
        samples[i] = high ? 8000 : -8000;
    }

    instance.player.playWAV(buf, wav_size);
    free(buf);
}

// --- Stopwatch ---

static void sw_update_display() {
    if (!sw_time_lbl) return;
    uint32_t total_ms = g_swElapsedMs;
    if (g_swRunning) {
        total_ms += (millis() - g_swStartMs);
    }
    uint32_t total_s = total_ms / 1000;
    uint32_t hundredths = (total_ms % 1000) / 10;
    char buf[20];
    snprintf(buf, sizeof(buf), "%02lu:%02lu.%02lu",
             (unsigned long)(total_s / 60),
             (unsigned long)(total_s % 60),
             (unsigned long)hundredths);
    lv_label_set_text(sw_time_lbl, buf);
    lv_obj_invalidate(sw_time_lbl);
}

static void sw_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    setState(STATE_CLOCK);
    ui_showClock();
}

static void sw_start_pause_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_swRunning) {
        g_swElapsedMs += (millis() - g_swStartMs);
        g_swRunning = false;
        if (sw_start_lbl) lv_label_set_text(sw_start_lbl, "Start");
    } else {
        g_swStartMs = millis();
        g_swRunning = true;
        if (sw_start_lbl) lv_label_set_text(sw_start_lbl, "Pause");
    }
    sw_update_display();
    lv_refr_now(NULL);
}

static void sw_reset_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    g_swRunning = false;
    g_swElapsedMs = 0;
    g_swStartMs = 0;
    if (sw_start_lbl) lv_label_set_text(sw_start_lbl, "Start");
    sw_update_display();
    lv_refr_now(NULL);
}

static void build_stopwatch_screen() {
    stopwatch_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(stopwatch_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(stopwatch_screen, lv_color_hex(0xE8E8E8), 0);
    lv_obj_clear_flag(stopwatch_screen, LV_OBJ_FLAG_SCROLLABLE);

    make_title(stopwatch_screen, "Stopwatch");
    make_close_btn(stopwatch_screen, sw_close_cb);

    sw_time_lbl = lv_label_create(stopwatch_screen);
    lv_label_set_text(sw_time_lbl, "00:00.00");
    // Font 36 fits "00:00.00" (8 chars) cleanly in the 240 px wide screen.
    // Font 48 would overflow, so we trade slight visual size for the
    // hundredths-of-a-second precision.
    lv_obj_set_style_text_font(sw_time_lbl, &lv_font_montserrat_36, 0);
    lv_obj_align(sw_time_lbl, LV_ALIGN_CENTER, 0, -30);

    make_action_btn(stopwatch_screen, "Start", 40, 0x22C55E,
                    sw_start_pause_cb, &sw_start_lbl);
    make_action_btn(stopwatch_screen, "Reset", 88, 0x444444,
                    sw_reset_cb, NULL);
}

// --- Timer ---

static int tm_total_set_seconds() {
    return g_timerSetMinutes * 60 + g_timerSetSeconds;
}

static void tm_update_display() {
    if (!tm_time_lbl) return;
    int s = g_timerRunning ? g_timerRemainingSec : tm_total_set_seconds();
    if (s < 0) s = 0;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", s / 60, s % 60);
    lv_label_set_text(tm_time_lbl, buf);
    lv_obj_invalidate(tm_time_lbl);
}

static void tm_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    setState(STATE_CLOCK);
    ui_showClock();
}

static void tm_min_minus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_timerRunning) return;
    if (g_timerSetMinutes > 0) g_timerSetMinutes--;
    tm_update_display();
    lv_refr_now(NULL);
}

static void tm_min_plus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_timerRunning) return;
    if (g_timerSetMinutes < 99) g_timerSetMinutes++;
    tm_update_display();
    lv_refr_now(NULL);
}

static void tm_sec_minus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_timerRunning) return;
    g_timerSetSeconds = (g_timerSetSeconds + 59) % 60;  // wrap, no minute borrow
    tm_update_display();
    lv_refr_now(NULL);
}

static void tm_sec_plus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_timerRunning) return;
    g_timerSetSeconds = (g_timerSetSeconds + 1) % 60;
    tm_update_display();
    lv_refr_now(NULL);
}

static void tm_start_pause_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_timerRunning) {
        g_timerRunning = false;
        if (tm_start_lbl) lv_label_set_text(tm_start_lbl, "Start");
    } else {
        if (tm_total_set_seconds() == 0) return;  // nothing to count
        g_timerRunning = true;
        g_timerStartMs = millis();
        g_timerRemainingSec = tm_total_set_seconds();
        if (tm_start_lbl) lv_label_set_text(tm_start_lbl, "Pause");
    }
    tm_update_display();
    lv_refr_now(NULL);
}

static void tm_reset_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    g_timerRunning = false;
    g_timerRemainingSec = tm_total_set_seconds();
    if (tm_start_lbl) lv_label_set_text(tm_start_lbl, "Start");
    tm_update_display();
    lv_refr_now(NULL);
}

static void build_timer_screen() {
    timer_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(timer_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(timer_screen, lv_color_hex(0xE8E8E8), 0);
    lv_obj_clear_flag(timer_screen, LV_OBJ_FLAG_SCROLLABLE);

    make_title(timer_screen, "Timer");
    make_close_btn(timer_screen, tm_close_cb);

    // Pickers: [-] MM [+]  [-] SS [+] — same layout as the alarm screen.
    // Picker buttons sit above the time label so they don't overlap the
    // big "MM:SS" font 48 text.
    make_picker_btn(timer_screen, "-", -90, -45, tm_min_minus_cb);
    make_picker_btn(timer_screen, "+", -30, -45, tm_min_plus_cb);
    make_picker_btn(timer_screen, "-",  30, -45, tm_sec_minus_cb);
    make_picker_btn(timer_screen, "+",  90, -45, tm_sec_plus_cb);

    tm_time_lbl = lv_label_create(timer_screen);
    lv_label_set_text(tm_time_lbl, "05:00");
    lv_obj_set_style_text_font(tm_time_lbl, &lv_font_montserrat_48, 0);
    lv_obj_align(tm_time_lbl, LV_ALIGN_CENTER, 0, 5);

    make_action_btn(timer_screen, "Start", 65, 0x22C55E,
                    tm_start_pause_cb, &tm_start_lbl);
    make_action_btn(timer_screen, "Reset", 110, 0x444444,
                    tm_reset_cb, NULL);
}

// --- Alarm ---

static void al_update_display() {
    if (!al_time_lbl) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", g_alarmHour, g_alarmMinute);
    lv_label_set_text(al_time_lbl, buf);
    lv_obj_invalidate(al_time_lbl);
    if (al_status_lbl) {
        lv_label_set_text(al_status_lbl,
                          g_alarmEnabled ? "Enabled" : "Disabled");
    }
    if (al_toggle_lbl) {
        lv_label_set_text(al_toggle_lbl,
                          g_alarmEnabled ? "Disable" : "Enable");
    }
}

static void al_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    setState(STATE_CLOCK);
    ui_showClock();
}

static void al_h_minus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    g_alarmHour = (g_alarmHour + 23) % 24;
    al_update_display();
    lv_refr_now(NULL);
}

static void al_h_plus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    g_alarmHour = (g_alarmHour + 1) % 24;
    al_update_display();
    lv_refr_now(NULL);
}

static void al_m_minus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    g_alarmMinute = (g_alarmMinute + 59) % 60;
    al_update_display();
    lv_refr_now(NULL);
}

static void al_m_plus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    g_alarmMinute = (g_alarmMinute + 1) % 60;
    al_update_display();
    lv_refr_now(NULL);
}

static void al_toggle_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    g_alarmEnabled = !g_alarmEnabled;
    if (g_alarmEnabled) {
        // Reset the fired-today flag so a fresh enable can fire even if
        // we already fired earlier today.
        g_alarmLastFiredYday = -1;
    }
    al_update_display();
    lv_refr_now(NULL);
}

static void build_alarm_screen() {
    alarm_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(alarm_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(alarm_screen, lv_color_hex(0xE8E8E8), 0);
    lv_obj_clear_flag(alarm_screen, LV_OBJ_FLAG_SCROLLABLE);

    make_title(alarm_screen, "Alarm");
    make_close_btn(alarm_screen, al_close_cb);

    // Pickers: [-] HH [+] : [-] MM [+]
    // Tighter spacing than timer because we have two pickers side-by-side.
    make_picker_btn(alarm_screen, "-", -90, -45, al_h_minus_cb);
    make_picker_btn(alarm_screen, "+", -30, -45, al_h_plus_cb);
    make_picker_btn(alarm_screen, "-",  30, -45, al_m_minus_cb);
    make_picker_btn(alarm_screen, "+",  90, -45, al_m_plus_cb);

    al_time_lbl = lv_label_create(alarm_screen);
    lv_label_set_text(al_time_lbl, "07:30");
    lv_obj_set_style_text_font(al_time_lbl, &lv_font_montserrat_48, 0);
    lv_obj_align(al_time_lbl, LV_ALIGN_CENTER, 0, 5);

    make_action_btn(alarm_screen, "Enable", 65, 0x22C55E,
                    al_toggle_cb, &al_toggle_lbl);

    al_status_lbl = lv_label_create(alarm_screen);
    lv_label_set_text(al_status_lbl, "Disabled");
    lv_obj_set_style_text_font(al_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(al_status_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(al_status_lbl, LV_ALIGN_CENTER, 0, 100);
}

// --- Public API ---

void ui_init() {
    build_home_screen();
    build_response_screen();
    build_clock_screen();
    build_stopwatch_screen();
    build_timer_screen();
    build_alarm_screen();
    lv_screen_load(home_screen);
}

void ui_showClock() {
    lv_screen_load(clock_screen);
}

void ui_showStopwatch() {
    sw_update_display();
    lv_screen_load(stopwatch_screen);
}

void ui_showTimer() {
    tm_update_display();
    lv_screen_load(timer_screen);
}

void ui_showAlarm() {
    al_update_display();
    lv_screen_load(alarm_screen);
}

// Periodic tick for clock features. Called from ui_tick().
void ui_clockTick() {
    uint32_t now_ms = millis();

    // Stopwatch — display refresh every 100 ms while running, so the
    // hundredths-of-a-second column ticks visibly. Cheap because only one
    // label is dirty per frame.
    static uint32_t last_sw_render = 0;
    if (g_swRunning && now_ms - last_sw_render >= 100) {
        last_sw_render = now_ms;
        sw_update_display();
    }

    // Timer — countdown and fire detection.
    if (g_timerRunning) {
        int elapsed = (now_ms - g_timerStartMs) / 1000;
        int remaining = tm_total_set_seconds() - elapsed;
        if (remaining <= 0) {
            g_timerRunning = false;
            g_timerRemainingSec = 0;
            tm_update_display();
            if (tm_start_lbl) lv_label_set_text(tm_start_lbl, "Start");
            // Fire pattern: 3 buzzes WITH audio. The DRV2605 haptic
            // waveform takes ~250 ms to play and consecutive vibrator()
            // calls within that window interrupt the previous one — so
            // the delays between calls must be longer than the haptic
            // duration to actually feel all 3 distinct buzzes. play_alarm_beep
            // is 250 ms blocking, which doubles as the spacing AND adds an
            // audible cue.
            Serial.println("[clock] timer fired");
            for (int i = 0; i < 3; i++) {
                instance.vibrator();
                play_alarm_beep();   // ~250 ms blocking — drains the haptic too
                delay(180);
            }
        } else if (remaining != g_timerRemainingSec) {
            g_timerRemainingSec = remaining;
            tm_update_display();
        }
    }

    // Alarm — check current time vs target every 5 sec while enabled.
    static uint32_t last_alarm_check = 0;
    if (g_alarmEnabled && now_ms - last_alarm_check > 5000) {
        last_alarm_check = now_ms;
        time_t now;
        struct tm tinfo;
        time(&now);
        if (localtime_r(&now, &tinfo) && tinfo.tm_year >= 70) {
            if (tinfo.tm_hour == g_alarmHour &&
                tinfo.tm_min == g_alarmMinute &&
                tinfo.tm_yday != g_alarmLastFiredYday) {
                g_alarmLastFiredYday = tinfo.tm_yday;
                Serial.println("[clock] alarm fired");
                // Fire pattern: 5 buzzes WITH audio. Same haptic-collapse
                // problem as the timer above — each buzz needs ~400 ms
                // before the next one to play distinctly. play_alarm_beep
                // is 250 ms blocking, plus 180 ms delay = ~430 ms cadence.
                for (int i = 0; i < 5; i++) {
                    instance.vibrator();
                    play_alarm_beep();
                    delay(180);
                }
            }
        }
    }
}

// --- Public state-query helpers (called from main loop's idle-sleep guard) ---

bool ui_timerIsRunning() {
    return g_timerRunning;
}

bool ui_alarmIsImminent(int minutes_window) {
    if (!g_alarmEnabled) return false;
    time_t now;
    struct tm tinfo;
    time(&now);
    if (!localtime_r(&now, &tinfo) || tinfo.tm_year < 70) return false;

    // Build the timestamp for today's alarm at HH:MM:00.
    struct tm alarm_tm = tinfo;
    alarm_tm.tm_hour = g_alarmHour;
    alarm_tm.tm_min  = g_alarmMinute;
    alarm_tm.tm_sec  = 0;
    time_t alarm_t = mktime(&alarm_tm);
    // If today's alarm time has already passed, look at tomorrow's.
    if (alarm_t <= now) {
        alarm_t += 24 * 3600;
    }
    return (alarm_t - now) <= (minutes_window * 60);
}

// Show/hide the split-button cancel+send labels (recording mode) vs the
// regular single label (everything else). Helper used by every ui_show*
// function so callers don't have to remember to flip both halves.
static void set_speak_split_visible(bool split) {
    if (!speak_lbl || !cancel_lbl || !send_lbl) return;
    if (split) {
        lv_obj_add_flag(speak_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(cancel_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(send_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(speak_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cancel_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(send_lbl, LV_OBJ_FLAG_HIDDEN);
    }
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
    // Any caller of set_speak_button is leaving recording mode (or never
    // entered it), so make sure the single-label view is the visible one.
    set_speak_split_visible(false);
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

void ui_showHome() {
    set_speak_button("Tap to speak", 0x3B82F6);  // blue
    lv_screen_load(home_screen);
}

void ui_showRecording() {
    if (!speak_btn || !cancel_lbl || !send_lbl) return;
    // Recording mode is the ONE state that uses the split-button view:
    // left "X Cancel" zone + right "0:00 Send" zone, both showing on a
    // single red background. Direct touch poll in the recording loop will
    // hit-test by x to distinguish the two zones.
    lv_obj_set_style_bg_color(speak_btn, lv_color_hex(0xEF4444), 0);  // red
    set_speak_split_visible(true);
    lv_label_set_text(cancel_lbl, "X Cancel");
    lv_label_set_text(send_lbl, "0:00 Send");
    lv_obj_invalidate(speak_btn);
    lv_refr_now(NULL);
    lv_screen_load(home_screen);
}

void ui_setRecordingElapsed(uint32_t seconds) {
    if (!send_lbl) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "0:%02u Send", (unsigned)seconds);
    lv_label_set_text(send_lbl, buf);
    // Same display-flush dance the rest of the UI uses — invalidate alone
    // doesn't push pixels on this watch's LVGL setup; lv_refr_now does.
    lv_obj_invalidate(send_lbl);
    lv_refr_now(NULL);
}

int ui_speakBtnHitTest(int x, int y) {
    if (!speak_btn) return -1;
    lv_area_t a;
    lv_obj_get_coords(speak_btn, &a);
    if (x < a.x1 || x > a.x2 || y < a.y1 || y > a.y2) {
        return -1;  // outside the button bounds
    }
    int btn_w = a.x2 - a.x1;
    int rel_x = x - a.x1;
    // Left 40% of the button is the cancel zone, right 60% is send/stop.
    if (rel_x < (btn_w * 40) / 100) {
        return 0;
    }
    return 1;
}

void ui_refreshSteps() {
    if (!lbl_steps) return;
    // Any explicit refresh clears a pending confirm — the count is the
    // canonical state and any tap-to-confirm UI is just a temporary overlay.
    g_stepsConfirmPending = false;
    uint32_t steps = instance.sensor.getPedometerCounter();
    char buf[24];
    snprintf(buf, sizeof(buf), "%lu steps", (unsigned long)steps);
    lv_label_set_text(lbl_steps, buf);
    // Clear any local color override (e.g. amber from confirm-pending mode)
    // so the label inherits the button's default text color again.
    lv_obj_remove_local_style_prop(lbl_steps, LV_STYLE_TEXT_COLOR, 0);
    lv_obj_invalidate(lbl_steps);
    lv_refr_now(NULL);
}

// Tap-twice-to-confirm pattern for the Steps button. Returns true if THIS
// tap is the confirming second tap (caller should perform the reset);
// returns false if this is the first tap (just arm the confirm overlay) or
// if the button isn't built yet.
bool ui_handleStepsTap() {
    if (!lbl_steps) return false;
    if (g_stepsConfirmPending) {
        // Second tap within the timeout — caller will reset and call
        // ui_refreshSteps() which clears g_stepsConfirmPending.
        return true;
    }
    // First tap — arm confirm. Show "Tap to confirm" in amber and start the
    // expiry timer. ui_tickStepsConfirm() (called from ui_tick) will revert
    // automatically if no second tap arrives in STEPS_CONFIRM_TIMEOUT_MS.
    g_stepsConfirmPending = true;
    g_stepsConfirmExpiresMs = millis() + STEPS_CONFIRM_TIMEOUT_MS;
    lv_label_set_text(lbl_steps, "Tap to confirm");
    lv_obj_set_style_text_color(lbl_steps, lv_color_hex(0xFBBF24), 0);
    lv_obj_invalidate(lbl_steps);
    lv_refr_now(NULL);
    return false;
}

// Called from ui_tick() each second to expire stale confirm-pending state.
static void update_steps_confirm_expiry() {
    if (!g_stepsConfirmPending) return;
    if (millis() > g_stepsConfirmExpiresMs) {
        // Timeout — silently revert to the live count (this also clears the
        // pending flag and the amber color override via ui_refreshSteps).
        ui_refreshSteps();
    }
}

void ui_showSending() {
    set_speak_button("Sending...", 0xFBBF24);      // amber
    lv_screen_load(home_screen);
}

void ui_showSent() {
    set_speak_button("Sent", 0x22C55E);            // green
    lv_screen_load(home_screen);
}

void ui_showNoSpeech() {
    // The status line that used to host this message was removed when the
    // pinned Sleep button took its place at the bottom edge. Show the
    // condition on the speak button itself in amber — user can tap again
    // to retry. ui_showHome() will reset it back to blue.
    set_speak_button("No speech — retry", 0xFBBF24);
    lv_screen_load(home_screen);
}

void ui_showResponse(const char* text) {
    lv_label_set_text(response_text, text ? text : "(no reply)");
    lv_screen_load(response_screen);
}

void ui_showError(const char* text) {
    // Display the error briefly via the speak button (red). User taps the
    // speak button again to start a new recording, which calls
    // ui_showRecording() and overwrites this label.
    set_speak_button(text ? text : "Error", 0xEF4444);
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
    // configureMotionWake → instance.sensor.enablePedometer()). Skip the
    // refresh while a confirm-pending overlay is active so we don't stomp
    // on the "Tap to confirm" label mid-prompt.
    if (!g_stepsConfirmPending) {
        uint32_t steps = instance.sensor.getPedometerCounter();
        snprintf(buf, sizeof(buf), "%lu steps", (unsigned long)steps);
        lv_label_set_text(lbl_steps, buf);
    }
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
    // Cheap — only does anything when a confirm is actually pending.
    update_steps_confirm_expiry();
    // Stopwatch / timer / alarm tick. Runs unconditionally so timer +
    // stopwatch keep advancing even when their screens are closed and the
    // user is back on the home screen. Alarm check is throttled internally.
    ui_clockTick();
}
