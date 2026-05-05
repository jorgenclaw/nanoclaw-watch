#pragma once

// =============================================================================
// NanoClaw Watch — Configuration
// Edit these constants before flashing. Everything user-tunable lives here.
// =============================================================================

// --- Firmware version (bump on each release) ---
#define FIRMWARE_VERSION  16
#define FIRMWARE_VERSION_STR "v16"

// --- WiFi provisioning ---
// WiFi credentials are stored in NVS (persist across reflashes) and
// configured via a captive portal on first boot or on demand. No need
// to edit source code to change networks.
// --- Notifications ---
#define NOTIF_POLL_INTERVAL_MS   90000   // 90 seconds between polls
#define NOTIF_BANNER_TIMEOUT_MS   8000   // auto-dismiss banner after 8s
#define NOTIF_POLL_HTTP_TIMEOUT  10000   // shorter timeout than voice (10s)

// --- WiFi provisioning ---
#define SETUP_AP_NAME           "Jorgenclaw-Setup"
#define SETUP_PORTAL_TIMEOUT_SEC  60   // auto-reboot if nobody configures
#define WIFI_CONNECT_TIMEOUT_SEC  15   // seconds before falling back to portal

// --- NanoClaw host endpoint ---
//
// The watch finds your NanoClaw host on the LAN via mDNS. Your host advertises
// `<NANOCLAW_HOSTNAME>.local` (most Linux desktop distros do this automatically
// once the hostname is set; on Pop!_OS / Ubuntu / Fedora that's
// `sudo hostnamectl set-hostname nanoclaw`). The watch resolves the hostname
// at boot and on reconnects, so the host can change IPs (DHCP, router swap,
// network move) without a re-flash.
//
// If your router blocks mDNS multicast (rare on home networks, common on guest
// WiFi or enterprise gear), the watch falls back to NANOCLAW_FALLBACK_IP. Set
// that to your host's last-known IP — the watch only uses it after both mDNS
// and DNS resolution fail.
//
// Watch will POST to:
//   http://<resolved>:<NANOCLAW_HOST_PORT>/api/watch/message
//   http://<resolved>:<NANOCLAW_HOST_PORT>/api/watch/poll
#define NANOCLAW_HOSTNAME     "nanoclaw"          // mDNS lookup target (without .local)
#define NANOCLAW_HOST_PORT    3000
#define NANOCLAW_FALLBACK_IP  "192.168.9.184"     // Last-resort literal; set to your
                                                   // host's static/last-known IP

// Shared secret to authenticate the watch to the host. Generate one and store
// it in your NanoClaw config too. (Until /api/watch/* exists, this is unused.)
#define WATCH_AUTH_TOKEN     "526768d607041fd75c830ab34399ef3c306a4c82bd5854917ea0cdfc4d21dfa3"

// Stable identifier for this physical watch
#define WATCH_DEVICE_ID      "twatch-s3"

// --- Timing / behavior ---
// Tap the speak button to START recording. Tap it again to STOP and send.
// If the user doesn't tap again within this many seconds, the recording
// auto-sends (runaway safety cap).
#define VOICE_RECORD_MAX_SECONDS 60
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

// --- Timezone (POSIX TZ string, handles DST automatically) ---
// Format: STD<offset>DST,<start>,<end> where offset is UTC - local in STANDARD time.
// "PST8PDT,M3.2.0,M11.1.0" = Pacific, PST=UTC-8, PDT=UTC-7,
// DST starts 2nd Sunday of March, ends 1st Sunday of November.
// Change if you move, or if US DST rules change.
#define TZ_POSIX           "PST8PDT,M3.2.0,M11.1.0"

// --- NTP sync ---
#define NTP_SERVER         "pool.ntp.org"

// How often to re-sync time from NTP (ms). ESP32's internal RTC drifts
// ~2-4 min/day, so an hourly resync keeps the clock accurate even if
// the watch stays up for weeks.
#define NTP_RESYNC_INTERVAL_MS  (60UL * 60UL * 1000UL)

// --- Weather ---
// Location string passed to wttr.in. Use "City,State" or "City,Country" or
// a postal code. URL-safe characters only — no spaces (use "+" if needed).
#define WEATHER_LOCATION   "Manteca,CA"
// How often the weather button auto-refreshes in the background. Tapping
// the button always forces a fresh fetch regardless.
#define WEATHER_REFRESH_MS 1800000  // 30 minutes
