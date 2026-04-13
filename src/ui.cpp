#include "ui.h"
#include "config.h"
#include "state.h"
#include "network.h"
#include "settings.h"

#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <time.h>
#include <esp_heap_caps.h>

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
static lv_obj_t* lbl_weather      = nullptr;  // inner label of grid button 3 (Weather)
static lv_obj_t* speak_btn        = nullptr;
static lv_obj_t* speak_lbl        = nullptr;  // the label *inside* the speak button — primary indicator
static lv_obj_t* cancel_lbl       = nullptr;  // left-half "X Cancel" label, only visible during recording
static lv_obj_t* send_lbl         = nullptr;  // right-half "0:00 Send" label, only visible during recording
static const int GRID_COUNT = 12;
static lv_obj_t* grid_btns[GRID_COUNT] = {};
static lv_obj_t* grid_container   = nullptr;  // scrollable button grid

// Clock sub-screen (loaded when user taps the Clock quick button — slot 1).
static lv_obj_t* clock_screen     = nullptr;

// DND sub-screen
static lv_obj_t* dnd_screen       = nullptr;
static lv_obj_t* dnd_status_lbl   = nullptr;

// Battery detail sub-screen
static lv_obj_t* battery_screen   = nullptr;
static lv_obj_t* bat_info_lbl     = nullptr;

// Weather sub-screen (loaded when user taps the Weather quick button — slot 3).
static lv_obj_t* weather_screen   = nullptr;
static lv_obj_t* w_temp_lbl       = nullptr;  // big "57F" / "14C"
static lv_obj_t* w_today_lbl      = nullptr;  // "Today      72F"
static lv_obj_t* w_tomorrow_lbl   = nullptr;  // "Tomorrow   70F"
static lv_obj_t* w_tonight_lbl    = nullptr;  // "Tonight    48F"
static lv_obj_t* w_uv_wind_lbl    = nullptr;  // "UV 3   Wind 8 mph NW"
static lv_obj_t* w_sun_lbl        = nullptr;  // "Sunrise 6:32  Sunset 7:48"
static lv_obj_t* w_unit_btn_lbl   = nullptr;  // "F" or "C" inside the toggle btn

// Weather data cache + render forward decls (definitions are further down
// near ui_setWeatherData).  Hoisted here so the sub-screen builders and
// callbacks (which sit between the static state block and the weather data
// helpers) can reference them without breaking C++ name lookup.
static WeatherData g_weatherData = {};
static bool g_weatherValid = false;
// Last fetch error code (or empty if last fetch succeeded). Surfaced on
// the home button label and the weather sub-screen status line so the
// user can see WHY a fetch failed without needing serial.
static char g_weatherErr[16] = {0};
static lv_obj_t* w_status_lbl = nullptr;  // status line at bottom of sub-screen
static void w_render_subscreen();
static void update_weather_button_label();

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

static const char* GRID_LABELS[GRID_COUNT] = {
    "DND",             // 0 — Do Not Disturb (submenu for time selection)
    "Clock",           // 1 — clock sub-screen (alarm/timer/stopwatch/pomodoro)
    "Flashlight",      // 2 — white -> tap -> red -> tap -> home
    "Weather...",      // 3 — wttr.in weather sub-screen
    "Network",         // 4 — WiFi info (SSID, IP, signal, saved networks)
    "Inbox",           // 5 — unread email summary (pushed by Jorgenclaw)
    "Find Phone",      // 6 — ping Scott's phone via Jorgenclaw
    "Next Event",      // 7 — next calendar event (once protond works)
    "Clicker",         // 8 — presentation slide advancer via BLE HID
    "Status",          // 9 — NanoClaw host status (reachable, uptime)
    "Spotify",         // 10 — music control (stub)
    "Screen Test",     // 11 — RGB cycle for display health check (stub)
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
    // CRITICAL: lv_obj_create makes containers scrollable by default. With
    // a scrollable home screen, LVGL interprets touches near the bottom
    // edge as scroll-up gestures instead of clicks — which silently broke
    // the pinned Sleep button at y=212-238. Home screen has no scrollable
    // content, so just turn it off.
    lv_obj_clear_flag(home_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Top status bar — battery + WiFi
    // Top status bar — labels positioned directly on home_screen so
    // they don't move when touch overlay sizes change.
    lbl_battery = lv_label_create(home_screen);
    lv_label_set_text(lbl_battery, "BAT --%");
    lv_obj_set_style_text_font(lbl_battery, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_battery, LV_ALIGN_TOP_LEFT, 8, 6);

    lbl_wifi = lv_label_create(home_screen);
    lv_label_set_text(lbl_wifi, "WiFi -");
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_TOP_RIGHT, -8, 6);

    // Invisible touch overlays for long-press actions. These sit on top
    // of the labels but don't parent them, so label positioning is clean.
    lv_obj_t* bat_btn = lv_obj_create(home_screen);
    lv_obj_remove_style_all(bat_btn);
    lv_obj_set_size(bat_btn, 120, 36);
    lv_obj_align(bat_btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(bat_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(bat_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(bat_btn, [](lv_event_t* e) {
        touchInteraction();
        setState(STATE_BATTERY);
        ui_showBatteryDetail();
    }, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t* wifi_btn = lv_obj_create(home_screen);
    lv_obj_remove_style_all(wifi_btn);
    lv_obj_set_size(wifi_btn, 120, 36);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_flag(wifi_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(wifi_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(wifi_btn, [](lv_event_t* e) {
        extern void onWifiLongPress();
        onWifiLongPress();
    }, LV_EVENT_LONG_PRESSED, NULL);

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

    // Scrollable button grid — 2 columns, 6 rows (12 buttons total).
    // The first 4 buttons (2x2) are visible without scrolling; the rest
    // are revealed by swiping up. Larger buttons than the old 2x2 grid.
    grid_container = lv_obj_create(home_screen);
    lv_obj_remove_style_all(grid_container);
    lv_obj_set_size(grid_container, 236, 100);  // visible height = 2 rows
    lv_obj_align(grid_container, LV_ALIGN_TOP_MID, 0, 138);
    lv_obj_set_style_bg_opa(grid_container, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(grid_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(grid_container, LV_DIR_VER);
    lv_obj_set_style_pad_all(grid_container, 0, 0);
    // Snap scrolling so rows don't stop half-visible.
    lv_obj_set_scroll_snap_y(grid_container, LV_SCROLL_SNAP_START);

    const int btn_w = 112, btn_h = 44;
    const int gap = 6;
    for (int i = 0; i < GRID_COUNT; i++) {
        int row = i / 2;
        int col = i % 2;
        int x = col * (btn_w + gap);
        int y = row * (btn_h + gap);

        grid_btns[i] = lv_obj_create(grid_container);
        lv_obj_remove_style_all(grid_btns[i]);
        lv_obj_set_size(grid_btns[i], btn_w, btn_h);
        lv_obj_set_pos(grid_btns[i], x, y);
        lv_obj_set_style_bg_opa(grid_btns[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(grid_btns[i], lv_color_hex(0x222222), 0);
        lv_obj_set_style_radius(grid_btns[i], 10, 0);
        lv_obj_add_flag(grid_btns[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(grid_btns[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(grid_btns[i], quick_event_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)i);

        lv_obj_t* lbl = lv_label_create(grid_btns[i]);
        lv_label_set_text(lbl, GRID_LABELS[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8E8E8), 0);
        lv_obj_center(lbl);

        // Stash Weather button label for live updates.
        if (i == 3) lbl_weather = lbl;
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
        case 3: ui_showPomodoro(); break;
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

    // Four action buttons stacked vertically — each loads a sub-sub-screen
    // (Alarm/Timer/Stopwatch) via clock_stub_cb dispatching on the index
    // passed as user_data. Pomodoro (idx 3) is a stub for now.
    // Buttons are wide (200x38) for easy tapping, tighter spacing to fit 4.
    const char* labels[4] = { "Alarm", "Timer", "Stopwatch", "Pomodoro" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* btn = lv_obj_create(clock_screen);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, 200, 38);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 52 + i * 46);
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

// --- Weather sub-screen ---
//
// Layout (240×240):
//   Title row: "Weather"            [Close]
//   Big current temp                  (font 36)
//   Today      72F                    (font 14)
//   Tomorrow   70F
//   Tonight    48F                    (= weather[1].mintempF, the coming morning low)
//   UV 3   Wind 8 mph NW              (font 14)
//   Sunrise 6:32  Sunset 7:48         (font 12)
//   [F]    [   Refresh   ]            (bottom row)
//
// All temperature/wind labels redraw via w_render_subscreen() which reads
// g_weatherData and the current settings_isMetric() flag, so flipping
// units instantly redraws without needing a re-fetch.

// Forward decl — defined in main.cpp; lets the Refresh button trigger a
// fetch without ui.cpp depending directly on net_fetchWeather.
extern void doWeatherFetchTriggered();

static void w_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    setState(STATE_HOME);
    ui_showHome();
}

static void w_refresh_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    instance.vibrator();
    Serial.println("[ui] weather refresh tapped");
    doWeatherFetchTriggered();
}

static void w_unit_toggle_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    bool new_metric = !settings_isMetric();
    settings_setMetric(new_metric);
    Serial.printf("[ui] unit toggled to %s\n", new_metric ? "metric" : "imperial");
    // Re-render everything so temps + wind + button labels reflect the new unit.
    update_weather_button_label();
    w_render_subscreen();
    lv_refr_now(NULL);
}

// Refresh all weather sub-screen labels from g_weatherData using the
// current unit setting. Safe to call even when the screen isn't showing —
// it just updates the cached label widgets and they'll be correct when
// the screen is loaded next.
static void w_render_subscreen() {
    if (!w_temp_lbl) return;  // sub-screen not built yet
    bool metric = settings_isMetric();
    char buf[40];

    if (g_weatherValid) {
        // Big current temp
        snprintf(buf, sizeof(buf), "%d%c",
                 metric ? g_weatherData.temp_c : g_weatherData.temp_f,
                 metric ? 'C' : 'F');
        lv_label_set_text(w_temp_lbl, buf);

        // Today / Tomorrow / Tonight rows — left-aligned label, right
        // value. snprintf with explicit padding via "%-10s" gives a
        // pseudo-table look in the monospace-ish Montserrat 14.
        snprintf(buf, sizeof(buf), "Today      %d%c",
                 metric ? g_weatherData.today_max_c : g_weatherData.today_max_f,
                 metric ? 'C' : 'F');
        lv_label_set_text(w_today_lbl, buf);

        snprintf(buf, sizeof(buf), "Tomorrow   %d%c",
                 metric ? g_weatherData.tomorrow_max_c : g_weatherData.tomorrow_max_f,
                 metric ? 'C' : 'F');
        lv_label_set_text(w_tomorrow_lbl, buf);

        snprintf(buf, sizeof(buf), "Tonight    %d%c",
                 metric ? g_weatherData.tonight_min_c : g_weatherData.tonight_min_f,
                 metric ? 'C' : 'F');
        lv_label_set_text(w_tonight_lbl, buf);

        // UV + wind line
        snprintf(buf, sizeof(buf), "UV %d   Wind %d %s %s",
                 g_weatherData.uv_index,
                 metric ? g_weatherData.wind_kph : g_weatherData.wind_mph,
                 metric ? "kph" : "mph",
                 g_weatherData.wind_dir);
        lv_label_set_text(w_uv_wind_lbl, buf);

        // Sunrise / sunset line — wttr.in returns these as already-formatted
        // 12-hour strings ("06:32 AM" / "07:48 PM"), so we just concatenate.
        snprintf(buf, sizeof(buf), "Sunrise %s  Sunset %s",
                 g_weatherData.sunrise, g_weatherData.sunset);
        lv_label_set_text(w_sun_lbl, buf);
    } else {
        // No valid data yet (pending or failed) — show placeholders.
        lv_label_set_text(w_temp_lbl,     metric ? "--C" : "--F");
        lv_label_set_text(w_today_lbl,    "Today      --");
        lv_label_set_text(w_tomorrow_lbl, "Tomorrow   --");
        lv_label_set_text(w_tonight_lbl,  "Tonight    --");
        lv_label_set_text(w_uv_wind_lbl,  "UV --   Wind --");
        lv_label_set_text(w_sun_lbl,      "Sunrise --:--  Sunset --:--");
    }

    // Unit toggle button label always shows the CURRENT unit.
    if (w_unit_btn_lbl) {
        lv_label_set_text(w_unit_btn_lbl, metric ? "C" : "F");
    }

    // Status line: success or last error reason.
    if (w_status_lbl) {
        if (g_weatherValid) {
            lv_label_set_text(w_status_lbl, "OK");
        } else if (g_weatherErr[0]) {
            char sbuf[40];
            snprintf(sbuf, sizeof(sbuf), "Last fetch: %s", g_weatherErr);
            lv_label_set_text(w_status_lbl, sbuf);
        } else {
            lv_label_set_text(w_status_lbl, "no fetch yet");
        }
    }

    if (weather_screen) {
        lv_obj_invalidate(weather_screen);
    }
}

static void build_weather_screen() {
    weather_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(weather_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(weather_screen, lv_color_hex(0xE8E8E8), 0);
    lv_obj_clear_flag(weather_screen, LV_OBJ_FLAG_SCROLLABLE);

    make_title(weather_screen, "Weather");
    make_close_btn(weather_screen, w_close_cb);

    // Location label — small, grey, below the title. Shows which city
    // the weather data is for (from WEATHER_LOCATION in config.h).
    {
        lv_obj_t* loc_lbl = lv_label_create(weather_screen);
        // Clean up "Manteca,CA" → "Manteca, CA" for readability.
        String loc = WEATHER_LOCATION;
        loc.replace(",", ", ");
        lv_label_set_text(loc_lbl, loc.c_str());
        lv_obj_set_style_text_font(loc_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(loc_lbl, lv_color_hex(0x888888), 0);
        lv_obj_align(loc_lbl, LV_ALIGN_TOP_MID, 0, 26);
    }

    // Big current-temperature display, font 36, centered horizontally.
    w_temp_lbl = lv_label_create(weather_screen);
    lv_label_set_text(w_temp_lbl, "--F");
    lv_obj_set_style_text_font(w_temp_lbl, &lv_font_montserrat_36, 0);
    lv_obj_align(w_temp_lbl, LV_ALIGN_TOP_MID, 0, 42);

    // Three forecast rows — top-left aligned with small left margin so the
    // text feels like a list, not centered floats.
    w_today_lbl = lv_label_create(weather_screen);
    lv_label_set_text(w_today_lbl, "Today      --");
    lv_obj_set_style_text_font(w_today_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(w_today_lbl, LV_ALIGN_TOP_LEFT, 22, 94);

    w_tomorrow_lbl = lv_label_create(weather_screen);
    lv_label_set_text(w_tomorrow_lbl, "Tomorrow   --");
    lv_obj_set_style_text_font(w_tomorrow_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(w_tomorrow_lbl, LV_ALIGN_TOP_LEFT, 22, 112);

    w_tonight_lbl = lv_label_create(weather_screen);
    lv_label_set_text(w_tonight_lbl, "Tonight    --");
    lv_obj_set_style_text_font(w_tonight_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(w_tonight_lbl, LV_ALIGN_TOP_LEFT, 22, 130);

    // UV + wind line, slightly smaller font to fit more.
    w_uv_wind_lbl = lv_label_create(weather_screen);
    lv_label_set_text(w_uv_wind_lbl, "UV --   Wind --");
    lv_obj_set_style_text_font(w_uv_wind_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(w_uv_wind_lbl, LV_ALIGN_TOP_MID, 0, 152);

    // Sunrise / sunset line, smaller still.
    w_sun_lbl = lv_label_create(weather_screen);
    lv_label_set_text(w_sun_lbl, "Sunrise --:--  Sunset --:--");
    lv_obj_set_style_text_font(w_sun_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(w_sun_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(w_sun_lbl, LV_ALIGN_TOP_MID, 0, 172);

    // Status line — right above the buttons. Shows "OK" on success or
    // the failure reason on failure ("HTTP 0", "no WiFi", "parse fail").
    // Tiny and dim — diagnostic data, not chrome.
    w_status_lbl = lv_label_create(weather_screen);
    lv_label_set_text(w_status_lbl, "");
    lv_obj_set_style_text_font(w_status_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(w_status_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(w_status_lbl, LV_ALIGN_TOP_MID, 0, 188);

    // Bottom row: F/C unit toggle + Refresh button.
    // Small toggle on the left, wider Refresh on the right.
    lv_obj_t* unit_btn = lv_obj_create(weather_screen);
    lv_obj_remove_style_all(unit_btn);
    lv_obj_set_size(unit_btn, 44, 32);
    lv_obj_align(unit_btn, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    lv_obj_set_style_bg_opa(unit_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(unit_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(unit_btn, 8, 0);
    lv_obj_add_flag(unit_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(unit_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(unit_btn, w_unit_toggle_cb, LV_EVENT_CLICKED, NULL);
    w_unit_btn_lbl = lv_label_create(unit_btn);
    lv_label_set_text(w_unit_btn_lbl, "F");
    lv_obj_set_style_text_font(w_unit_btn_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(w_unit_btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(w_unit_btn_lbl);

    lv_obj_t* refresh_btn = lv_obj_create(weather_screen);
    lv_obj_remove_style_all(refresh_btn);
    lv_obj_set_size(refresh_btn, 140, 32);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
    lv_obj_set_style_bg_opa(refresh_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x22C55E), 0);
    lv_obj_set_style_radius(refresh_btn, 10, 0);
    lv_obj_add_flag(refresh_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(refresh_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(refresh_btn, w_refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* refresh_lbl = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_lbl, "Refresh");
    lv_obj_set_style_text_font(refresh_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(refresh_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(refresh_lbl);
}

void ui_showWeather() {
    w_render_subscreen();  // ensure labels reflect current data + units
    lv_screen_load(weather_screen);
}

// =============================================================================
// Pomodoro timer
// =============================================================================

static lv_obj_t* pomodoro_screen     = nullptr;
static lv_obj_t* pom_time_lbl        = nullptr;
static lv_obj_t* pom_phase_lbl       = nullptr;
static lv_obj_t* pom_start_lbl       = nullptr;
static lv_obj_t* pom_work_val_lbl    = nullptr;
static lv_obj_t* pom_rest_val_lbl    = nullptr;

static bool      g_pomRunning        = false;
static int       g_pomWorkMin        = 25;
static int       g_pomRestMin        = 5;
enum PomPhase { POM_WORK, POM_REST };
static PomPhase  g_pomPhase          = POM_WORK;
static uint32_t  g_pomPhaseEndMs     = 0;

static void pom_update_picker() {
    if (pom_work_val_lbl) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", g_pomWorkMin);
        lv_label_set_text(pom_work_val_lbl, buf);
    }
    if (pom_rest_val_lbl) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", g_pomRestMin);
        lv_label_set_text(pom_rest_val_lbl, buf);
    }
}

static void pom_update_display() {
    if (!pom_time_lbl) return;
    if (!g_pomRunning) {
        lv_label_set_text(pom_time_lbl, "--:--");
        if (pom_phase_lbl) lv_label_set_text(pom_phase_lbl, "Ready");
        return;
    }
    uint32_t now = millis();
    int remaining_sec = 0;
    if (now < g_pomPhaseEndMs) {
        remaining_sec = (g_pomPhaseEndMs - now) / 1000;
    }
    int m = remaining_sec / 60;
    int s = remaining_sec % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    lv_label_set_text(pom_time_lbl, buf);

    if (pom_phase_lbl) {
        lv_label_set_text(pom_phase_lbl,
                          g_pomPhase == POM_WORK ? "WORK" : "REST");
        lv_obj_set_style_text_color(pom_phase_lbl,
            lv_color_hex(g_pomPhase == POM_WORK ? 0xEF4444 : 0x22C55E), 0);
    }
    lv_obj_invalidate(pom_time_lbl);
}

// Called from ui_clockTick() every second
static void pom_tick() {
    if (!g_pomRunning) return;
    uint32_t now = millis();
    if (now >= g_pomPhaseEndMs) {
        // Phase transition
        Serial.printf("[pom] phase %s ended\n",
                      g_pomPhase == POM_WORK ? "WORK" : "REST");
        // 3 short buzzes
        for (int i = 0; i < 3; i++) {
            instance.vibrator();
            delay(200);
        }
        if (g_pomPhase == POM_WORK) {
            g_pomPhase = POM_REST;
            g_pomPhaseEndMs = millis() + (uint32_t)g_pomRestMin * 60000UL;
        } else {
            g_pomPhase = POM_WORK;
            g_pomPhaseEndMs = millis() + (uint32_t)g_pomWorkMin * 60000UL;
        }
    }
    pom_update_display();
}

static void pom_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    // Stop the pomodoro when closing
    g_pomRunning = false;
    setState(STATE_CLOCK);
    ui_showClock();
}

static void pom_work_minus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_pomRunning) return;
    if (g_pomWorkMin > 5) g_pomWorkMin -= 5;
    pom_update_picker();
    lv_refr_now(NULL);
}

static void pom_work_plus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_pomRunning) return;
    if (g_pomWorkMin < 60) g_pomWorkMin += 5;
    pom_update_picker();
    lv_refr_now(NULL);
}

static void pom_rest_minus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_pomRunning) return;
    if (g_pomRestMin > 5) g_pomRestMin -= 5;
    pom_update_picker();
    lv_refr_now(NULL);
}

static void pom_rest_plus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_pomRunning) return;
    if (g_pomRestMin < 60) g_pomRestMin += 5;
    pom_update_picker();
    lv_refr_now(NULL);
}

static void pom_start_stop_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_pomRunning) {
        g_pomRunning = false;
        if (pom_start_lbl) lv_label_set_text(pom_start_lbl, "Start");
    } else {
        g_pomRunning = true;
        g_pomPhase = POM_WORK;
        g_pomPhaseEndMs = millis() + (uint32_t)g_pomWorkMin * 60000UL;
        if (pom_start_lbl) lv_label_set_text(pom_start_lbl, "Stop");
    }
    pom_update_display();
    lv_refr_now(NULL);
}

static void build_pomodoro_screen() {
    pomodoro_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(pomodoro_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(pomodoro_screen, lv_color_hex(0xE8E8E8), 0);
    lv_obj_clear_flag(pomodoro_screen, LV_OBJ_FLAG_SCROLLABLE);

    make_title(pomodoro_screen, "Pomodoro");
    make_close_btn(pomodoro_screen, pom_close_cb);

    // Phase label (WORK / REST / Ready)
    pom_phase_lbl = lv_label_create(pomodoro_screen);
    lv_label_set_text(pom_phase_lbl, "Ready");
    lv_obj_set_style_text_font(pom_phase_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(pom_phase_lbl, LV_ALIGN_TOP_MID, 0, 40);

    // Big countdown
    pom_time_lbl = lv_label_create(pomodoro_screen);
    lv_label_set_text(pom_time_lbl, "--:--");
    lv_obj_set_style_text_font(pom_time_lbl, &lv_font_montserrat_48, 0);
    lv_obj_align(pom_time_lbl, LV_ALIGN_TOP_MID, 0, 56);

    // Work row: "Work: [-] 25 [+] min"
    lv_obj_t* w_lbl = lv_label_create(pomodoro_screen);
    lv_label_set_text(w_lbl, "Work:");
    lv_obj_set_style_text_font(w_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(w_lbl, LV_ALIGN_TOP_LEFT, 12, 118);

    make_picker_btn(pomodoro_screen, "-", 10, 8, pom_work_minus_cb);

    pom_work_val_lbl = lv_label_create(pomodoro_screen);
    lv_label_set_text(pom_work_val_lbl, "25");
    lv_obj_set_style_text_font(pom_work_val_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(pom_work_val_lbl, LV_ALIGN_CENTER, 48, 8);

    make_picker_btn(pomodoro_screen, "+", 82, 8, pom_work_plus_cb);

    lv_obj_t* w_unit = lv_label_create(pomodoro_screen);
    lv_label_set_text(w_unit, "min");
    lv_obj_set_style_text_font(w_unit, &lv_font_montserrat_14, 0);
    lv_obj_align(w_unit, LV_ALIGN_CENTER, 110, 8);

    // Rest row: "Rest: [-]  5 [+] min"
    lv_obj_t* r_lbl = lv_label_create(pomodoro_screen);
    lv_label_set_text(r_lbl, "Rest:");
    lv_obj_set_style_text_font(r_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(r_lbl, LV_ALIGN_TOP_LEFT, 12, 158);

    make_picker_btn(pomodoro_screen, "-", 10, 48, pom_rest_minus_cb);

    pom_rest_val_lbl = lv_label_create(pomodoro_screen);
    lv_label_set_text(pom_rest_val_lbl, "5");
    lv_obj_set_style_text_font(pom_rest_val_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(pom_rest_val_lbl, LV_ALIGN_CENTER, 48, 48);

    make_picker_btn(pomodoro_screen, "+", 82, 48, pom_rest_plus_cb);

    lv_obj_t* r_unit = lv_label_create(pomodoro_screen);
    lv_label_set_text(r_unit, "min");
    lv_obj_set_style_text_font(r_unit, &lv_font_montserrat_14, 0);
    lv_obj_align(r_unit, LV_ALIGN_CENTER, 110, 48);

    // Start/Stop button
    make_action_btn(pomodoro_screen, "Start", 90, 0x22C55E,
                    pom_start_stop_cb, &pom_start_lbl);
}

// =============================================================================
// Notification banner (overlay on lv_layer_top) + detail screen
// =============================================================================

// --- DND (Do Not Disturb) sub-screen ---

// Extern: g_dndUntil lives in main.cpp, shared with the notification handler.
extern uint32_t g_dndUntil;

static void dnd_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    setState(STATE_HOME);
    ui_showHome();
}

static void dnd_update_status() {
    if (!dnd_status_lbl) return;
    if (g_dndUntil == 0 || millis() >= g_dndUntil) {
        lv_label_set_text(dnd_status_lbl, "DND: Off");
        lv_obj_set_style_text_color(dnd_status_lbl, lv_color_hex(0x888888), 0);
    } else {
        uint32_t remaining_ms = g_dndUntil - millis();
        uint32_t remaining_min = remaining_ms / 60000;
        uint32_t hours = remaining_min / 60;
        uint32_t mins  = remaining_min % 60;
        char buf[32];
        snprintf(buf, sizeof(buf), "Active - %lu:%02lu remaining",
                 (unsigned long)hours, (unsigned long)mins);
        lv_label_set_text(dnd_status_lbl, buf);
        lv_obj_set_style_text_color(dnd_status_lbl, lv_color_hex(0x60A5FA), 0);
    }
}

static void dnd_set_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    int minutes = (int)(intptr_t)lv_event_get_user_data(e);
    if (minutes == 0) {
        g_dndUntil = 0;
        Serial.println("[dnd] turned off");
    } else {
        g_dndUntil = millis() + (uint32_t)minutes * 60000UL;
        Serial.printf("[dnd] set for %d minutes\n", minutes);
    }
    instance.vibrator();
    dnd_update_status();
    lv_refr_now(NULL);
}

// --- DND Custom duration picker (sub-sub-screen) ---

static lv_obj_t* dnd_custom_screen  = nullptr;
static lv_obj_t* dnd_custom_h_lbl   = nullptr;
static lv_obj_t* dnd_custom_m_lbl   = nullptr;
static int g_dndCustomHours   = 1;
static int g_dndCustomMinutes = 0;  // 0, 15, 30, 45

static void dnd_custom_update() {
    if (dnd_custom_h_lbl) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", g_dndCustomHours);
        lv_label_set_text(dnd_custom_h_lbl, buf);
    }
    if (dnd_custom_m_lbl) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", g_dndCustomMinutes);
        lv_label_set_text(dnd_custom_m_lbl, buf);
    }
}

static void dnd_custom_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    setState(STATE_DND);
    ui_showDnd();
}

static void dnd_custom_h_minus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_dndCustomHours > 0) g_dndCustomHours--;
    dnd_custom_update();
    lv_refr_now(NULL);
}

static void dnd_custom_h_plus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    if (g_dndCustomHours < 8) g_dndCustomHours++;
    dnd_custom_update();
    lv_refr_now(NULL);
}

static void dnd_custom_m_minus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    g_dndCustomMinutes = (g_dndCustomMinutes + 60 - 15) % 60;
    dnd_custom_update();
    lv_refr_now(NULL);
}

static void dnd_custom_m_plus_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    g_dndCustomMinutes = (g_dndCustomMinutes + 15) % 60;
    dnd_custom_update();
    lv_refr_now(NULL);
}

static void dnd_custom_start_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    int total_min = g_dndCustomHours * 60 + g_dndCustomMinutes;
    if (total_min == 0) return;  // nothing to set
    g_dndUntil = millis() + (uint32_t)total_min * 60000UL;
    Serial.printf("[dnd] custom set for %d minutes\n", total_min);
    instance.vibrator();
    setState(STATE_HOME);
    ui_showHome();
}

static void build_dnd_custom_screen() {
    dnd_custom_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(dnd_custom_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(dnd_custom_screen, lv_color_hex(0xE8E8E8), 0);
    lv_obj_clear_flag(dnd_custom_screen, LV_OBJ_FLAG_SCROLLABLE);

    make_title(dnd_custom_screen, "Set DND Duration");
    make_close_btn(dnd_custom_screen, dnd_custom_close_cb);

    // Hours row: "Hours:  [-]  N  [+]"
    lv_obj_t* h_label = lv_label_create(dnd_custom_screen);
    lv_label_set_text(h_label, "Hours:");
    lv_obj_set_style_text_font(h_label, &lv_font_montserrat_14, 0);
    lv_obj_align(h_label, LV_ALIGN_TOP_LEFT, 16, 60);

    make_picker_btn(dnd_custom_screen, "-", 20, -52, dnd_custom_h_minus_cb);
    dnd_custom_h_lbl = lv_label_create(dnd_custom_screen);
    lv_label_set_text(dnd_custom_h_lbl, "1");
    lv_obj_set_style_text_font(dnd_custom_h_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(dnd_custom_h_lbl, LV_ALIGN_CENTER, 60, -52);
    make_picker_btn(dnd_custom_screen, "+", 96, -52, dnd_custom_h_plus_cb);

    // Minutes row: "Minutes: [-]  N  [+]"
    lv_obj_t* m_label = lv_label_create(dnd_custom_screen);
    lv_label_set_text(m_label, "Minutes:");
    lv_obj_set_style_text_font(m_label, &lv_font_montserrat_14, 0);
    lv_obj_align(m_label, LV_ALIGN_TOP_LEFT, 16, 108);

    make_picker_btn(dnd_custom_screen, "-", 20, 0, dnd_custom_m_minus_cb);
    dnd_custom_m_lbl = lv_label_create(dnd_custom_screen);
    lv_label_set_text(dnd_custom_m_lbl, "0");
    lv_obj_set_style_text_font(dnd_custom_m_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(dnd_custom_m_lbl, LV_ALIGN_CENTER, 60, 0);
    make_picker_btn(dnd_custom_screen, "+", 96, 0, dnd_custom_m_plus_cb);

    // Start DND button
    make_action_btn(dnd_custom_screen, "Start DND", 70, 0x3B82F6,
                    dnd_custom_start_cb, NULL);
}

// --- DND main screen (2x3 grid) ---

// Callback for "Custom" button — opens the custom picker
static void dnd_custom_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    setState(STATE_DND_CUSTOM);
    ui_showDndCustom();
}

static void build_dnd_screen() {
    dnd_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(dnd_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(dnd_screen, lv_color_hex(0xE8E8E8), 0);
    lv_obj_clear_flag(dnd_screen, LV_OBJ_FLAG_SCROLLABLE);

    make_title(dnd_screen, "Do Not Disturb");
    make_close_btn(dnd_screen, dnd_close_cb);

    // Status label
    dnd_status_lbl = lv_label_create(dnd_screen);
    lv_obj_set_style_text_font(dnd_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(dnd_status_lbl, LV_ALIGN_TOP_MID, 0, 44);
    lv_label_set_text(dnd_status_lbl, "DND: Off");

    // 2x3 grid of bigger buttons (108x44 each, 6px gap)
    struct { const char* label; int minutes; bool is_custom; } presets[] = {
        { "30 min",   30,  false },
        { "1 hour",   60,  false },
        { "2 hours",  120, false },
        { "4 hours",  240, false },
        { "Custom",   0,   true  },
        { "Off",      0,   false },
    };
    const int btn_w = 108, btn_h = 44, gap = 6;
    const int grid_x_start = (240 - 2 * btn_w - gap) / 2;  // center the grid
    const int grid_y_start = 66;

    for (int i = 0; i < 6; i++) {
        int row = i / 2;
        int col = i % 2;
        int x = grid_x_start + col * (btn_w + gap);
        int y = grid_y_start + row * (btn_h + gap);

        uint32_t color = (presets[i].minutes == 0 && !presets[i].is_custom) ? 0x444444 : 0x222222;
        lv_obj_t* btn = lv_obj_create(dnd_screen);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        if (presets[i].is_custom) {
            lv_obj_add_event_cb(btn, dnd_custom_btn_cb, LV_EVENT_CLICKED, NULL);
        } else {
            lv_obj_add_event_cb(btn, dnd_set_cb, LV_EVENT_CLICKED,
                                (void*)(intptr_t)presets[i].minutes);
        }

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, presets[i].label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8E8E8), 0);
        lv_obj_center(lbl);
    }
}

// --- Battery detail sub-screen ---

static void bat_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    touchInteraction();
    setState(STATE_HOME);
    ui_showHome();
}

// Battery drain rate tracking — circular buffer of {millis, percent} samples.
// Sampled every 5 minutes from bat_update_info (called every 30 sec from
// update_battery, but we only record a sample every BAT_SAMPLE_INTERVAL_MS).
static const int BAT_SAMPLE_COUNT = 10;
static const uint32_t BAT_SAMPLE_INTERVAL_MS = 300000;  // 5 minutes
struct BatSample { uint32_t ms; int pct; };
static BatSample s_battSamples[BAT_SAMPLE_COUNT];
static int s_battSampleIdx   = 0;   // next write position
static int s_battSampleCount = 0;   // total samples stored (max BAT_SAMPLE_COUNT)
static uint32_t s_lastBatSampleMs = 0;

static void bat_record_sample(int pct) {
    uint32_t now = millis();
    if (s_battSampleCount > 0 && (now - s_lastBatSampleMs < BAT_SAMPLE_INTERVAL_MS)) {
        return;  // not time yet
    }
    s_battSamples[s_battSampleIdx] = { now, pct };
    s_battSampleIdx = (s_battSampleIdx + 1) % BAT_SAMPLE_COUNT;
    if (s_battSampleCount < BAT_SAMPLE_COUNT) s_battSampleCount++;
    s_lastBatSampleMs = now;
}

// Calculate drain rate in %/hr from oldest and newest samples.
// Returns negative value if draining, positive if charging.
// Returns 0 if insufficient data.
static float bat_drain_rate_pct_per_hr(bool* valid) {
    *valid = false;
    if (s_battSampleCount < 2) return 0.0f;
    // Oldest sample
    int oldest_idx = (s_battSampleCount < BAT_SAMPLE_COUNT)
                     ? 0
                     : s_battSampleIdx;  // wrap-around: next write pos IS the oldest
    BatSample& oldest = s_battSamples[oldest_idx];
    // Newest sample
    int newest_idx = (s_battSampleIdx + BAT_SAMPLE_COUNT - 1) % BAT_SAMPLE_COUNT;
    BatSample& newest = s_battSamples[newest_idx];
    uint32_t dt_ms = newest.ms - oldest.ms;
    if (dt_ms < 600000) return 0.0f;  // need at least 10 minutes of data
    *valid = true;
    float dt_hr = (float)dt_ms / 3600000.0f;
    return (float)(oldest.pct - newest.pct) / dt_hr;  // positive = draining
}

static void bat_update_info() {
    if (!bat_info_lbl) return;
    int pct = instance.pmu.getBatteryPercent();
    bool charging = instance.pmu.isCharging();
    uint32_t uptime_sec = millis() / 1000;
    uint32_t up_h = uptime_sec / 3600;
    uint32_t up_m = (uptime_sec % 3600) / 60;

    // Record sample for drain tracking
    bat_record_sample(pct);

    bool drain_valid = false;
    float drain = bat_drain_rate_pct_per_hr(&drain_valid);

    // Heap stats with health indicators and explanations.
    size_t free_dram  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    const char* dram_health  = free_dram > 100 ? "healthy" : free_dram > 50 ? "low" : "critical";
    const char* psram_health = free_psram > 500 ? "healthy" : free_psram > 100 ? "low" : "critical";

    char buf[400];
    int pos = 0;

    // Battery + drain section
    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "Battery: %d%% (%s)\n",
                    pct, charging ? "charging" : "not charging");
    if (drain_valid && drain > 0.01f) {
        float remaining_hr = (float)pct / drain;
        int rem_h = (int)remaining_hr;
        int rem_m = (int)((remaining_hr - rem_h) * 60);
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "Drain: %.1f %%/hr\n"
                        "Est. remaining: ~%dh %02dm\n",
                        drain, rem_h, rem_m);
    } else if (drain_valid) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "Drain: charging/stable\n");
    } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "Drain: Calculating...\n");
    }

    // Uptime + memory section with education
    snprintf(buf + pos, sizeof(buf) - pos,
             "Uptime: %luh %02lum\n\n"
             "Fast memory: %u KB (%s)\n"
             "  >100 OK, <50 = problems\n"
             "Slow memory: %u KB (%s)\n"
             "  >500 OK, <100 = problems",
             (unsigned long)up_h, (unsigned long)up_m,
             (unsigned)free_dram, dram_health,
             (unsigned)free_psram, psram_health);

    lv_label_set_text(bat_info_lbl, buf);
}

static void build_battery_screen() {
    battery_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(battery_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(battery_screen, lv_color_hex(0xE8E8E8), 0);
    lv_obj_clear_flag(battery_screen, LV_OBJ_FLAG_SCROLLABLE);

    make_title(battery_screen, "System Info");
    make_close_btn(battery_screen, bat_close_cb);

    bat_info_lbl = lv_label_create(battery_screen);
    lv_label_set_long_mode(bat_info_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(bat_info_lbl, 220);
    lv_obj_set_style_text_font(bat_info_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(bat_info_lbl, "Loading...");
    lv_obj_align(bat_info_lbl, LV_ALIGN_TOP_LEFT, 12, 48);
}

// =============================================================================

static lv_obj_t* notif_banner       = nullptr;
static lv_obj_t* notif_banner_from  = nullptr;
static lv_obj_t* notif_banner_text  = nullptr;
static uint32_t  g_bannerShowMs     = 0;

// Cached full text for tap-to-expand.
static char g_notifFrom[32]     = {0};
static char g_notifFullText[1024] = {0};

static lv_obj_t* notif_detail_screen = nullptr;
static lv_obj_t* notif_detail_from   = nullptr;
static lv_obj_t* notif_detail_body   = nullptr;

static void notif_banner_tap_cb(lv_event_t* e) {
    (void)e;
    onNotifBannerTapped();
}

static void notif_close_cb(lv_event_t* e) {
    (void)e;
    onNotifDismissed();
}

static void build_notif_banner() {
    // The banner lives on lv_layer_top() so it overlays any screen.
    notif_banner = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(notif_banner);
    lv_obj_set_size(notif_banner, 240, 52);
    lv_obj_align(notif_banner, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(notif_banner, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(notif_banner, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_radius(notif_banner, 0, 0);
    lv_obj_set_style_pad_all(notif_banner, 6, 0);
    lv_obj_add_flag(notif_banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(notif_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(notif_banner, notif_banner_tap_cb, LV_EVENT_CLICKED, NULL);

    notif_banner_from = lv_label_create(notif_banner);
    lv_obj_set_style_text_font(notif_banner_from, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(notif_banner_from, lv_color_hex(0x60A5FA), 0);
    lv_label_set_text(notif_banner_from, "");
    lv_obj_align(notif_banner_from, LV_ALIGN_TOP_LEFT, 0, 0);

    notif_banner_text = lv_label_create(notif_banner);
    lv_obj_set_style_text_font(notif_banner_text, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(notif_banner_text, lv_color_hex(0xE8E8E8), 0);
    lv_label_set_text(notif_banner_text, "");
    lv_obj_set_width(notif_banner_text, 226);
    lv_label_set_long_mode(notif_banner_text, LV_LABEL_LONG_DOT);
    lv_obj_align(notif_banner_text, LV_ALIGN_TOP_LEFT, 0, 16);

    // Start hidden.
    lv_obj_add_flag(notif_banner, LV_OBJ_FLAG_HIDDEN);
}

static void build_notif_detail_screen() {
    notif_detail_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(notif_detail_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(notif_detail_screen, lv_color_hex(0xE8E8E8), 0);

    make_title(notif_detail_screen, "Notification");
    make_close_btn(notif_detail_screen, notif_close_cb);

    notif_detail_from = lv_label_create(notif_detail_screen);
    lv_obj_set_style_text_font(notif_detail_from, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(notif_detail_from, lv_color_hex(0x60A5FA), 0);
    lv_label_set_text(notif_detail_from, "");
    lv_obj_align(notif_detail_from, LV_ALIGN_TOP_LEFT, 8, 28);

    // Scrollable body container.
    lv_obj_t* body_cont = lv_obj_create(notif_detail_screen);
    lv_obj_remove_style_all(body_cont);
    lv_obj_set_size(body_cont, 230, 170);
    lv_obj_align(body_cont, LV_ALIGN_TOP_LEFT, 5, 48);
    lv_obj_set_style_bg_opa(body_cont, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(body_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body_cont, LV_DIR_VER);

    notif_detail_body = lv_label_create(body_cont);
    lv_label_set_long_mode(notif_detail_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(notif_detail_body, 220);
    lv_obj_set_style_text_font(notif_detail_body, &lv_font_montserrat_14, 0);
    lv_label_set_text(notif_detail_body, "");
}

void ui_showNotifBanner(const char* from, const char* preview,
                        const char* full_text) {
    strncpy(g_notifFrom, from, sizeof(g_notifFrom) - 1);
    strncpy(g_notifFullText, full_text, sizeof(g_notifFullText) - 1);
    lv_label_set_text(notif_banner_from, from);
    lv_label_set_text(notif_banner_text, preview);
    lv_obj_clear_flag(notif_banner, LV_OBJ_FLAG_HIDDEN);
    g_bannerShowMs = millis();
}

void ui_hideNotifBanner() {
    if (notif_banner) lv_obj_add_flag(notif_banner, LV_OBJ_FLAG_HIDDEN);
}

bool ui_notifBannerVisible() {
    return notif_banner && !lv_obj_has_flag(notif_banner, LV_OBJ_FLAG_HIDDEN);
}

void ui_showNotifDetail(const char* from, const char* full_text) {
    lv_label_set_text(notif_detail_from, from);
    lv_label_set_text(notif_detail_body, full_text);
    lv_screen_load(notif_detail_screen);
}

// --- Public API ---

void ui_init() {
    build_home_screen();
    build_response_screen();
    build_clock_screen();
    build_stopwatch_screen();
    build_timer_screen();
    build_alarm_screen();
    build_weather_screen();
    build_dnd_screen();
    build_dnd_custom_screen();
    build_pomodoro_screen();
    build_battery_screen();
    build_notif_banner();
    build_notif_detail_screen();
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

void ui_showDnd() {
    dnd_update_status();
    lv_screen_load(dnd_screen);
}

void ui_showDndCustom() {
    dnd_custom_update();
    lv_screen_load(dnd_custom_screen);
}

void ui_showPomodoro() {
    pom_update_picker();
    pom_update_display();
    lv_screen_load(pomodoro_screen);
}

bool ui_pomodoroIsRunning() {
    return g_pomRunning;
}

void ui_showBatteryDetail() {
    bat_update_info();
    lv_screen_load(battery_screen);
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

    // Pomodoro — update countdown and handle phase transitions.
    pom_tick();

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

// --- Weather data cache + UI helpers ---
//
// The g_weatherData cache and forward decls are hoisted up next to the
// other static state at the top of the file. Definitions of the helpers
// live here next to the public ui_setWeather* functions.

static void update_weather_button_label() {
    if (!lbl_weather) return;
    char buf[24];
    if (g_weatherValid) {
        if (settings_isMetric()) {
            snprintf(buf, sizeof(buf), "%dC UV%d",
                     g_weatherData.temp_c, g_weatherData.uv_index);
        } else {
            snprintf(buf, sizeof(buf), "%dF UV%d",
                     g_weatherData.temp_f, g_weatherData.uv_index);
        }
    } else if (g_weatherErr[0]) {
        // Surface the last failure reason directly on the button so the
        // user can self-diagnose without serial: "no WiFi", "HTTP 0",
        // "parse fail", "no fcst", etc.
        snprintf(buf, sizeof(buf), "%s", g_weatherErr);
    } else {
        snprintf(buf, sizeof(buf), "Weather...");
    }
    lv_label_set_text(lbl_weather, buf);
    lv_obj_invalidate(lbl_weather);
}

void ui_setWeatherData(const WeatherData& data) {
    g_weatherData = data;
    g_weatherValid = true;
    g_weatherErr[0] = '\0';  // clear last error on success
    update_weather_button_label();
    w_render_subscreen();
}

void ui_setWeatherPending() {
    if (!lbl_weather) return;
    lv_label_set_text(lbl_weather, "Weather...");
    lv_obj_invalidate(lbl_weather);
}

void ui_setWeatherFailed() {
    ui_setWeatherError("fail");
}

void ui_setWeatherError(const char* err_code) {
    g_weatherValid = false;
    if (err_code) {
        strncpy(g_weatherErr, err_code, sizeof(g_weatherErr) - 1);
        g_weatherErr[sizeof(g_weatherErr) - 1] = '\0';
    }
    update_weather_button_label();
    w_render_subscreen();
}

// Steps button removed from the grid — these are no-ops now.
void ui_refreshSteps() {}
bool ui_handleStepsTap() { return false; }
static void update_steps_confirm_expiry() {}

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

    // Record sample for drain rate tracking (only actually stores every 5 min)
    bat_record_sample(pct);

    char buf[24];
    snprintf(buf, sizeof(buf), "%s%d%%", charging ? "+" : "BAT ", pct);
    lv_label_set_text(lbl_battery, buf);

    lv_label_set_text(lbl_wifi, net_isConnected() ? "WiFi OK" : "WiFi -");

    // (Steps button removed — pedometer still runs for future use but
    // no longer has a home-screen label to update.)
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
    // Auto-dismiss notification banner after timeout.
    if (ui_notifBannerVisible() &&
        now - g_bannerShowMs > NOTIF_BANNER_TIMEOUT_MS) {
        ui_hideNotifBanner();
    }
    // Stopwatch / timer / alarm tick. Runs unconditionally so timer +
    // stopwatch keep advancing even when their screens are closed and the
    // user is back on the home screen. Alarm check is throttled internally.
    ui_clockTick();
}
