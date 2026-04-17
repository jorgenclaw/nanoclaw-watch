# NanoClaw Watch — Builders & Agents Guide

*Synthesized by Jorgenclaw (AI agent) and Claude Code (host AI), with direct prompting and verification from Scott Jorgensen*

This document is for developers extending the firmware, hardware builders creating variants, and AI agents implementing new features on behalf of users. If you're a regular user looking to set up your watch, see [`README.md`](README.md) and [`NEXT_STEPS.md`](NEXT_STEPS.md) instead.

---

## Architecture overview

The NanoClaw Watch is a **thin client**. Almost no intelligence runs on the ESP32 itself — the watch is a hardware terminal that sends and receives data over WiFi.

```
[T-Watch S3/S3 Plus]
    ↕ HTTP (WiFi)
[NanoClaw server]  ←→  [AI agent runtime]  ←→  [tools, APIs, memory]
```

**On the watch (ESP32-S3):**
- Voice recording (PCM audio → HTTP POST to server)
- Audio playback (HTTP GET → stream to PCM speaker)
- Display rendering (LVGL, 240×240 LCD)
- Touch input handling
- Sensor reads (BMA423 accelerometer, RTC, optional GPS on Plus)
- Notification polling (HTTP GET on interval)
- Deep sleep / wake management

**On the server (NanoClaw host):**
- Speech-to-text transcription
- Agent inference (LLM)
- Tool execution (calendar, reminders, integrations)
- Text-to-speech synthesis
- Notification queue management

The watch never holds state beyond the current UI frame and buffered audio. All persistent memory (preferences, history, schedules) lives on the server.

---

## Repository layout

```
nanoclaw-watch/
├── src/
│   ├── main.cpp          # Setup, loop, deep sleep, button ISR
│   ├── config.h          # WiFi, server URL, compile-time constants
│   ├── ui.cpp / ui.h     # LVGL screen management, tile navigation
│   ├── network.cpp / .h  # WiFi multi-network, HTTP helpers
│   ├── state.cpp / .h    # Runtime state struct (shared across modules)
│   ├── settings.cpp / .h # NVS-backed persistent settings
│   ├── gps.cpp / .h      # L76K GPS (Plus only, off by default)
├── docs/
│   ├── WATCH_FEATURES.md # Full feature catalog with agent implementation pointers
│   ├── USER_GUIDE.md     # End-user setup and operation
│   ├── TOTP_TRADEOFFS.md # Design decision log for TOTP / tilt-to-wake
├── boards/               # PlatformIO board definitions
├── partitions.csv        # Custom partition table (larger app partition)
├── platformio.ini        # Build targets: s3, s3plus
├── NEXT_STEPS.md         # Ordered setup walkthrough for new builds
├── README.md             # User-facing overview and purchase info
└── BUILDERS.md           # This file
```

---

## Build targets

Two targets in `platformio.ini`:

| Target | Hardware | Use |
|--------|----------|-----|
| `env:twatch-s3` | T-Watch S3 (base) | No GPS module |
| `env:twatch-s3-plus` | T-Watch S3 Plus | GPS module present (off by default) |

```bash
# Flash base S3
pio run -e twatch-s3 --target upload

# Flash S3 Plus
pio run -e twatch-s3-plus --target upload

# Open serial monitor (115200 baud)
pio device monitor
```

The only firmware difference between targets is the GPS power rail and the `GPS_AVAILABLE` compile-time flag that gates `src/gps.cpp`. All UI, voice, and network code is identical.

---

## Key dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| LVGL | 8.x | UI rendering |
| LilyGo-AMOLED-Series | upstream | Board HAL |
| ArduinoJson | 7.x | JSON encode/decode |
| ESP-IDF WiFi / HTTP | bundled | Network stack |
| PCF8563 | upstream | RTC driver |
| BMA423 | upstream | Accelerometer driver |

All dependencies are declared in `platformio.ini` and fetched automatically on first build.

---

## Server API contract

The watch communicates with the NanoClaw server over plain HTTP. All endpoints are relative to `NANOCLAW_HOST_URL` in `config.h`.

### Voice command
```
POST /watch/voice
Content-Type: audio/wav
Body: raw PCM audio (16kHz, 16-bit mono, WAV container)

Response 200:
{
  "text": "Your agent's reply as a string",
  "audio_url": "/watch/audio/abc123.wav"   // optional, if TTS was generated
}
```

### Audio fetch
```
GET /watch/audio/<id>.wav
Response 200: audio/wav stream
```

### Notification poll
```
GET /watch/notifications
Response 200:
{
  "notifications": [
    { "id": "abc", "text": "Your 2pm meeting starts in 10 minutes", "haptic": true }
  ]
}
```

### Settings sync (on boot)
```
GET /watch/settings
Response 200:
{
  "timezone": "America/Los_Angeles",
  "weather_location": "Manteca, CA",
  "pomodoro_minutes": 25
}
```

The server is responsible for implementing these endpoints. The NanoClaw agent runtime handles routing — the watch itself has no awareness of LLM inference, tool calls, or memory.

---

## Adding a feature (for human developers)

1. **Check `docs/WATCH_FEATURES.md` first.** It catalogs every feature we've considered — built, deferred, and rejected — with reasoning. Don't duplicate a rejected approach without reading why it was rejected.

2. **Add hardware calls in the appropriate module.** The modules map roughly to hardware: `ui.cpp` for display, `network.cpp` for HTTP, `settings.cpp` for NVS. Shared runtime state lives in `state.h` — add fields there if multiple modules need to read your data.

3. **Add a new LVGL tile if needed.** The watch uses a grid of tiles navigated by swipe. New features that need their own screen get a tile registered in `ui.cpp`.

4. **Add a server endpoint if needed.** If your feature needs new data from the agent (beyond voice/notification/settings), define the endpoint contract here in `BUILDERS.md` and implement it in the NanoClaw server.

5. **Update `docs/WATCH_FEATURES.md`.** Every implemented feature gets a full entry with status, hardware dependencies, and an Agent implementation pointer.

---

## Adding a feature (for AI agents)

If a user has asked you to add a feature to their watch, follow this flow:

1. **Read `docs/WATCH_FEATURES.md`** — find the feature (or the closest thing to it) and read the *Agent implementation pointer* section. This tells you exactly what to change and where.

2. **Locate the feature in `src/`** — most UI features live in `ui.cpp`, most network features in `network.cpp`. State shared between modules is in `state.h`.

3. **Make the change via Quad inbox or direct file edit** — if you're an agent running in a container with read-only repo access, write your proposed changes to the quad-inbox with exact before/after code snippets. If you have direct write access to the repo, open a branch and a PR.

4. **Note flash + PSRAM cost** — the ESP32-S3 has 16 MB flash and 8 MB PSRAM. Features that add large assets (images, audio clips) need to be assessed against available space. The partition table is in `partitions.csv`.

5. **Update `docs/WATCH_FEATURES.md`** — move the feature from "Ideas" to "Implemented" and fill in the agent implementation pointer so the next agent has a clean path.

### What agents can do without a firmware change

Many features don't require reflashing the watch at all. The server-side agent controls:
- **What the voice response says** (any agent capability can be surfaced via voice)
- **Notification content and timing** (the watch just displays what the server sends)
- **Weather data source and format**
- **Settings values** (timezone, pomodoro duration, etc.)

Reserve firmware changes for features that need new hardware interaction (a new sensor, a new UI element, a new haptic pattern) or new network endpoints.

---

## GPS (S3 Plus only)

The L76K GPS receiver is wired to the PMIC's dedicated power rail. `src/gps.cpp` controls power via the `GPS_POWER_PIN` macro. When the `gps_enabled` settings flag is false (the default), the firmware calls `gps_power_off()` on boot and the receiver draws zero current.

**Privacy model:** GPS is purely passive (receives satellite signals, transmits nothing). Cutting board power to the receiver means no reception at all — not "listening silently," physically off. Users can verify this by watching battery drain before and after toggling the setting.

**Location data flow:** When GPS is on, `src/gps.cpp` parses NMEA sentences and writes lat/lon/accuracy to the shared `state` struct. The network layer includes current coordinates in voice request payloads when available. The server decides whether and how to use them.

---

## Partition table

The default ESP32-IDF partition layout doesn't leave enough room for the NanoClaw firmware + LVGL assets. `partitions.csv` defines a custom layout:

| Partition | Size | Purpose |
|-----------|------|---------|
| nvs | 20 KB | NVS (settings, WiFi credentials) |
| otadata | 8 KB | OTA state |
| app0 | 3 MB | Main app (active) |
| app1 | 3 MB | OTA staging slot |
| spiffs | ~9 MB | Asset storage |

OTA firmware updates will use the app0/app1 slot scheme — the server pushes a new binary to app1, the watch validates and reboots into it, then marks app1 as active. This is not yet implemented but the partition layout is already ready for it.

---

## Design decisions log

Non-obvious decisions are documented in `docs/TOTP_TRADEOFFS.md`. Before changing a behavior that seems obviously improvable (gesture wakeup, TOTP display, power management thresholds), check if there's an entry there. Several "obvious" improvements were evaluated and rejected for hardware or UX reasons.

---

## Contributing

PRs welcome. Please:
- Target `main` for bug fixes, `dev` for new features
- Run the PlatformIO build clean before submitting (`pio run --target clean`)
- Update `docs/WATCH_FEATURES.md` if you're adding or changing a feature
- Keep `config.h` changes out of your PR (it contains personal credentials in local builds)

Questions? Open an issue, or reach Scott via Signal (ScottJorgensen.51) or Nostr (`npub1ghawdls89y3vsjnz0505c2zpkccv7vjpddnpf9rt9m3x7nvsv30qchw63y`).
