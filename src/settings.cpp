#include "settings.h"
#include <Preferences.h>

static bool s_metric = false;  // imperial default

static const char* PREFS_NAMESPACE = "watch";
static const char* KEY_METRIC      = "metric";

void settings_load() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);  // read-only
    s_metric = prefs.getBool(KEY_METRIC, false);
    prefs.end();
    Serial.printf("[settings] loaded: metric=%d\n", s_metric ? 1 : 0);
}

bool settings_isMetric() {
    return s_metric;
}

void settings_setMetric(bool metric) {
    if (s_metric == metric) return;
    s_metric = metric;
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);  // read-write
    prefs.putBool(KEY_METRIC, metric);
    prefs.end();
    Serial.printf("[settings] saved: metric=%d\n", metric ? 1 : 0);
}
