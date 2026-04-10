#pragma once

// =============================================================================
// NanoClaw Watch — Configuration
// Edit these constants before flashing. Everything user-tunable lives here.
// =============================================================================

// --- WiFi credentials ---
// TODO: Fill in before flashing
#define WIFI_SSID            "Rockfish_Guest_optout_nomap"
#define WIFI_PASSWORD        "beourguest"

// --- NanoClaw host endpoint ---
// Base URL of your NanoClaw host. Watch will POST to:
//   <NANOCLAW_HOST_URL>/api/watch/message  (text or audio submissions)
//   <NANOCLAW_HOST_URL>/api/watch/poll     (incoming response polling)
// TODO: Set to your host's LAN IP and port
#define NANOCLAW_HOST_URL    "http://192.168.9.184:3000"

// Shared secret to authenticate the watch to the host. Generate one and store
// it in your NanoClaw config too. (Until /api/watch/* exists, this is unused.)
#define WATCH_AUTH_TOKEN     "526768d607041fd75c830ab34399ef3c306a4c82bd5854917ea0cdfc4d21dfa3"

// Stable identifier for this physical watch
#define WATCH_DEVICE_ID      "twatch-s3"

// --- Timing / behavior ---
// Tap the speak button to START recording. Tap it again to STOP and send.
// If the user doesn't tap again within this many seconds, the recording
// auto-sends (runaway safety cap).
#define VOICE_RECORD_MAX_SECONDS 30
// I2S read chunk size in bytes. Smaller = more responsive to the stop tap,
// larger = less CPU overhead. 4096 bytes @ 16 kHz / 16-bit mono = 128 ms of
// audio per read, so the stop-tap has ~128 ms worst-case latency.
#define VOICE_RECORD_CHUNK_BYTES 4096
#define POLL_INTERVAL_MS        60000  // poll host for incoming responses
#define IDLE_SLEEP_MS           30000  // ms of inactivity before light sleep
#define HTTP_TIMEOUT_MS         60000  // HTTP request timeout (must exceed host's WATCH_SYNC_TIMEOUT_MS)
#define WIFI_CONNECT_TIMEOUT_MS 10000  // WiFi attempt before giving up

// --- Microphone tuning ---
// The T-Watch S3 PDM mic has no hardware gain control, so sensitivity is
// managed in software after capture.
//
// MIC_ATTENUATION: scale factor applied to every PCM sample after recording.
//   1.0  = full volume (default hardware level — too hot, clips easily)
//   0.5  = -6 dB
//   0.25 = -12 dB (recommended starting point)
//   0.125= -18 dB (quiet rooms)
// Lower this if recordings sound distorted or Whisper mis-transcribes.
#define MIC_ATTENUATION          0.25f

// MIC_SPEECH_THRESHOLD: RMS (root-mean-square) amplitude gate. The capture
// is only sent to the host if the average sample energy exceeds this value.
// RMS is much better than peak for rejecting transient noises (keyboard
// clicks, mouse clicks) that have brief high peaks but near-zero RMS.
//   Typical sustained speech RMS: 500–3000
//   Typical click/tap RMS:         < 100 (duration-averaged to near zero)
//   Typical quiet-room noise RMS:  20–200
// Start at 500. If room noise still gets through, raise to 800–1000.
// If soft speech is rejected, lower to 200–300. The serial monitor prints
// each capture's RMS so you can tune to your environment.
#define MIC_SPEECH_THRESHOLD     500

// After a successful send, how long to hold the green "Sent" button before
// switching to the reply screen. Long enough for a human to actually register
// the confirmation, short enough not to feel like padding.
#define SENT_CONFIRM_MS          1800

// --- Quick-tap preset prompts (4 max, displayed on home screen) ---
// Tapping a preset POSTs that text to /api/watch/message.
// Special slots (intercepted in onQuickPromptPressed before quickPromptText):
//   Slot 1 (top-right)    = Clock sub-screen entry (no text POST)
//   Slot 2 (bottom-left)  = Steps button (live pedometer count, tap-twice to reset)
//   Slot 3 (bottom-right) = regular text prompt — used to be Sleep, but Sleep
//                           moved to its own pinned bottom-edge button.
// Slots 1 and 2 don't actually need a prompt string but the macros below
// stay defined so quickPromptText() compiles for all 4 indices.
#define QUICK_PROMPT_1  "What's next for me today?"
#define QUICK_PROMPT_2  "(unused — slot 2 is the Clock sub-screen entry)"
#define QUICK_PROMPT_3  "(unused — slot 3 is the Steps button)"
#define QUICK_PROMPT_4  "Any new messages?"

// --- Display ---
#define BRIGHTNESS_ACTIVE  200  // 0-255, screen brightness when awake
#define BRIGHTNESS_DIM     40   // 0-255, dimmed during inactivity countdown

// --- Timezone offset for clock display (seconds) ---
// Pacific Standard Time = -8 * 3600. PDT = -7 * 3600.
#define TZ_OFFSET_SECONDS  (-7 * 3600)

// --- NTP sync ---
#define NTP_SERVER         "pool.ntp.org"

// --- Weather ---
// Location string passed to wttr.in. Use "City,State" or "City,Country" or
// a postal code. URL-safe characters only — no spaces (use "+" if needed).
#define WEATHER_LOCATION   "Manteca,CA"
// How often the weather button auto-refreshes in the background. Tapping
// the button always forces a fresh fetch regardless.
#define WEATHER_REFRESH_MS 1800000  // 30 minutes
