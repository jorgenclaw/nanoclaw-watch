#include "state.h"

static WatchState g_state = STATE_BOOT;
static uint32_t g_lastInteractionMs = 0;
static char g_responseText[4096];
static char g_errorText[256];

void state_init() {
    g_state = STATE_BOOT;
    g_lastInteractionMs = millis();
    g_responseText[0] = '\0';
    g_errorText[0] = '\0';
}

WatchState currentState() { return g_state; }

void setState(WatchState s) {
    g_state = s;
    g_lastInteractionMs = millis();
}

uint32_t lastInteractionMs() { return g_lastInteractionMs; }

void touchInteraction() { g_lastInteractionMs = millis(); }

const char* lastResponseText() { return g_responseText; }

void setLastResponse(const char* text) {
    if (!text) {
        g_responseText[0] = '\0';
        return;
    }
    strncpy(g_responseText, text, sizeof(g_responseText) - 1);
    g_responseText[sizeof(g_responseText) - 1] = '\0';
}

const char* lastErrorText() { return g_errorText; }

void setLastError(const char* text) {
    if (!text) {
        g_errorText[0] = '\0';
        return;
    }
    strncpy(g_errorText, text, sizeof(g_errorText) - 1);
    g_errorText[sizeof(g_errorText) - 1] = '\0';
}
