# NanoClaw Watch — Feature Catalog

*Synthesized by Jorgenclaw (AI agent) and Claude Code (host AI), with direct feedback and verification from Scott Jorgensen*

> **For customers:** if you bought a flashed NanoClaw watch and want to add features, ask your Jorgenclaw agent to read this file. The agent will walk you through what's possible, what it costs (in battery, flash space, code changes, and setup effort), and offer to build the feature for you. Each feature section includes an **Agent implementation pointer** designed for your agent to act on — you don't have to read or understand it yourself.

> **For Scott:** this is the single source of truth for watch features. Every idea we've had, every feature we've built, every feature we've decided against — it all lives here. When we have a new idea, append it to "Ideas (not yet committed)" with a short description. When we implement one, promote it up the list and fill in the Agent implementation pointer so the marketplace stays current.

## What the watch is

The NanoClaw Watch is a **LilyGo T-Watch S3** running custom firmware that turns it into a wrist-worn terminal for a Jorgenclaw AI agent. It is NOT a standalone smartwatch — it depends on a NanoClaw host computer (usually the always-on machine you run Jorgenclaw on) for compute, memory, integrations, and most of its intelligence. The watch is the microphone, the speaker, the haptic motor, and the small bright display. The host is the brain.

This architecture is the whole point: because the host has unlimited compute and all your integrations, the watch can do things a standalone smartwatch can't.

## Hardware inventory

Features are built on top of what the watch physically has. This list is the feasibility check for every new idea.

| Component | What it is | What it enables |
|---|---|---|
| ESP32-S3 | Dual-core microcontroller, 8 MB PSRAM, 16 MB flash | Runs the firmware, WiFi, BLE |
| 1.54" LCD | 240×240 color touchscreen | Visual display + touch input |
| PDM microphone | Digital MEMS mic | Voice capture for commands |
| PCM speaker | Small mono speaker | Audio playback, beeps, tones |
| DRV2605 haptic motor | Vibrating feedback | Notifications, confirmations, silent alerts |
| BMA423 accelerometer | 3-axis motion sensor | Step counting, double-tap detection, activity classification (no gesture-aware wrist-raise wakeup — see [TOTP_TRADEOFFS.md](TOTP_TRADEOFFS.md) and `groups/main/quad-inbox/twatch-tilt-to-wake-response.md` for the history) |
| PCF8563 RTC | Real-time clock chip | Keeps time during deep sleep |
| AXP2101 PMIC | Power management | Battery monitoring, charging, power rails |
| IR LED | Infrared emitter | *Unused so far* — could drive TV/AC/legacy remotes |
| WiFi 2.4 GHz | 802.11 b/g/n | HTTP to the host |
| BLE 5.0 | Low-energy Bluetooth | *Unused so far* — could do HID, proximity, beacons |
| Side button | Single physical button | Wake from sleep, interrupt flows |

Not present: heart rate / PPG sensor, GPS, camera, cellular, NFC, SpO2. Features requiring those are not feasible without a hardware revision.

---

## Implemented features (shipping today)

These are in the current firmware. If you have a flashed watch, you already have them.

### Voice chat with your Jorgenclaw agent

Tap the big blue **Speak** button, talk, tap to send or cancel. The watch records audio, POSTs it to the host, the host transcribes via Whisper, feeds the text to your agent, and returns a text reply shown on the watch's response screen.

**Why it's compelling:** it's the fastest capture → answer loop for a wrist-worn AI agent. Nothing to unlock, no app to open, no keyboard.

**Agent implementation pointer:** already shipping. See `src/main.cpp:doVoiceCapture()`, `src/network.cpp:net_postAudio()`, and `src/channels/watch.ts:handleMessagePost()` in the NanoClaw host repo.

### Voice memo capture (Capture tile)

Tap the **Capture** grid tile, speak a thought, tap to send. The host transcribes and appends the text to a daily memo file (`groups/<folder>/memos/YYYY-MM-DD.md`) without running the agent. No chat round-trip, no response generation — just file it.

**Why it's compelling:** this is the single highest-leverage feature for a wrist AI. Evernote/Drafts/Voice Memos all exist to serve "capture a thought before it evaporates" and they all require unlocking a phone. The watch-on-your-wrist answer is sub-second from impulse to captured. And because the host indexes everything, you can later ask your agent "what was that thing I thought of about X last week?" and it will find it.

**Confirmation:** the response screen shows the transcript so you can verify you were heard correctly.

**Mirror to Signal:** every memo is also mirrored to your Signal conversation with Jorgenclaw (prefixed `[Memo]`) so you can scan recent captures from your phone without opening the daily file.

**Agent implementation pointer:** `VoiceIntent::VOICE_INTENT_MEMO` in `src/main.cpp`, routed to `net_postMemoAudio()` in `src/network.cpp`, received by `handleMemoPost()` in `NanoClaw/src/channels/watch.ts`. To customize where memos are stored, change the `memosDir` path in `handleMemoPost()`.

### Voice-triggered scheduled reminders (Remind tile)

Tap the **Remind** grid tile, say *"remind me in 10 minutes to call Mom"* or *"remind me at 3pm to take out the trash"*, tap to send. The host parses the time phrase, schedules a reminder, and at the target time buzzes your watch with a notification showing the action.

**Supported phrasings:**

| What you want | Example |
|---|---|
| Relative time | "in 10 minutes to call Mom" / "in 2 hours to check the oven" |
| Absolute time today | "at 3pm to take out the trash" / "at 9:30am to call the doctor" |
| Tomorrow at a specific time | "tomorrow at 8am to go for a run" |

If the phrasing doesn't match one of these patterns, the response tells you how to rephrase. The parser is deliberately simple (no LLM round-trip, no chrono-node dependency) so it's fast, deterministic, and has no new failure modes.

**Persistence caveat:** reminders are scheduled in-memory via `setTimeout`. If the NanoClaw host restarts before the reminder fires, the reminder is lost. Your host runs as a systemd service and restarts are rare, so this is usually fine — but if you run into it, ask your agent to add SQLite persistence via `NanoClaw/src/db.ts`.

**Agent implementation pointer:** `VoiceIntent::VOICE_INTENT_REMINDER` in `src/main.cpp` → `net_postReminderAudio()` → `handleReminderPost()` + `parseReminder()` in `NanoClaw/src/channels/watch.ts`. Extending the parser to handle more natural phrasings (e.g. "in half an hour", "at noon", "next Tuesday at 9am") is a self-contained edit — add new regex cases to `parseReminder()` and document them in this file.

### WiFi manager with tap-to-forget and add-network

Tap the **WiFi** grid tile to see your saved networks, the current connection, and a "+ Add new network" button. Tap a saved network to arm deletion (row turns red), tap again within 3 seconds to confirm. Tap the add button to open the captive portal and add a new network.

Up to **10 saved networks**. Every saved network is actively probed on reconnect (broadcasting the SSID over the air below any VPN layer), so the "forget" button exists for privacy reasons — see [USER_GUIDE.md](USER_GUIDE.md) for the full explanation. Prune the list to just the places you actually visit.

**Agent implementation pointer:** `src/ui.cpp:build_wifi_mgr_screen()` and `src/settings.cpp:settings_addWifi/settings_removeWifi()`. To change the 10-slot limit, edit `WIFI_MAX_NETWORKS` in `src/settings.h`.

### Weather

Tap the **Weather** grid tile for current temperature, today/tomorrow highs, tonight's low, UV index, wind, sunrise, sunset. Location is configured in `src/config.h`. F/C toggle on the weather screen.

**Agent implementation pointer:** `src/ui.cpp:build_weather_screen()`. Location via `WEATHER_LOCATION` in `src/config.h`. Weather source is wttr.in.

### Clock, alarm, timer, stopwatch, pomodoro

Tap the **Clock** grid tile for sub-screens: one-shot alarm, countdown timer, stopwatch with lap tracking, Pomodoro work/rest cycle timer.

**Agent implementation pointer:** `src/ui.cpp:build_clock_screen()` and siblings. Fire patterns in `src/ui.cpp:ui_clockTick()`.

### Proactive notifications

The host pushes notifications to the watch via `POST /api/watch/notify`. The watch polls `/api/watch/notifications` and displays them as a bottom banner with haptic. Tap the banner for full text. Used for incoming Signal messages and emails from contacts.

**Agent implementation pointer:** `src/main.cpp:doNotifPoll()`, `src/ui.cpp:build_notif_banner()`, `NanoClaw/src/channels/watch.ts:addNotification()`.

### Do Not Disturb (DND)

Grid tile with presets (30m / 1h / 2h / 4h / custom) that suppresses notification haptics. Banner still shows; just no buzz.

**Agent implementation pointer:** `src/ui.cpp:build_dnd_screen()` + `g_dndUntil` in `src/main.cpp`.

### Flashlight

Tap the **Flashlight** grid tile for a full-white bright screen. Tap to dismiss.

### Battery detail

Long-press the battery percentage in the top-left corner for drain rate, estimated remaining time, memory health, and uptime.

### Signal strength bars

Three tiny bars in the top-right corner show the current WiFi signal strength. Not tappable — WiFi management is via the grid tile.

---

## Planned features (approved direction, not yet built)

### TOTP 2FA codes on your wrist

**Status:** DESIGN REVIEW — security tradeoffs being evaluated. See [TOTP_TRADEOFFS.md](TOTP_TRADEOFFS.md) for details.

Tap a tile, see the current rotating 2FA code for your selected account. Replaces the phone → unlock → open authenticator → squint workflow for Scott's frequently-used services. The TOTP seeds stay on the host; only the current 6-digit code crosses the wire on demand.

**Why it matters:** if you use 2FA many times a day (Scott does, he uses Proton Pass heavily), this is a real daily quality-of-life win. The watch is already on your wrist; you're just glancing at it.

**Why it's not built yet:** the current watch↔host channel is plain HTTP, not HTTPS. On a shared network (coffee shop, airport), a sniffer could capture the code in transit. The code is valid for 30 seconds, which is a narrow window, but it's not zero. Before shipping this feature we need to either (a) add HTTPS to the host, (b) add a pre-shared-key AES layer over the existing channel, or (c) accept the home-network-only risk model and document it clearly. See [TOTP_TRADEOFFS.md](TOTP_TRADEOFFS.md).

**Agent implementation pointer:** Not yet specified. Blocked on security decision.

### Sleep tracking via accelerometer

**Status:** PLANNED, ~half day of work.

Use the BMA423's no-motion detection to log sleep start/end during user-configured sleep windows. Host gets a `/api/watch/sleep` endpoint (or reuses `/api/watch/notify` with a new type). Daily summary pushed to watch in the morning.

**Hardware:** BMA423 already polls for activity — just need to wire the state machine + endpoint.

**Agent implementation pointer:** watch-side: read `instance.sensor.isActivity()` periodically, track `last_motion_ms`, on transition detect sleep-start / sleep-end, POST to host. Host-side: new endpoint that appends to `groups/<folder>/health/sleep.jsonl`.

### Sedentary / hydration / movement nudges

**Status:** PLANNED, ~half day of work.

Scheduled haptic buzzes during working hours. If no motion for 60 min → stretch nudge. Hourly hydration reminder (tap to confirm, auto-silent if already confirmed twice in the last hour). Backend-driven — host triggers, watch displays.

**Agent implementation pointer:** `NanoClaw/src/task-scheduler.ts` already supports time-based triggers. Add scheduled tasks that POST to `/api/watch/notify` with specific messages. No watch-side code changes needed.

### Morning briefing tile

**Status:** PLANNED, ~1 hour of work.

Tap once, watch hits `/api/watch/briefing` on the host, agent composes a multi-part summary: weather, calendar, unread message counts, top open tasks. Displayed on the response screen.

**Agent implementation pointer:** new grid tile that calls `net_postText("BRIEFING", ...)` or a dedicated endpoint. Host side: `handleBriefingPost()` that runs the agent with a "morning briefing" prompt template, returns the reply.

---

## Ideas (not yet committed)

Every idea we've considered. Promote any of these to "Planned" when you decide to build them.

### Agent-powered features (need host+agent support)

- **Memory moment bookmark** — "remember this, now" tile that saves timestamp + voice context, later retrievable by agent. Different from Capture because it captures *the surrounding context* (date, location if known, recent conversations) not just the user's words. Feels like dropping a pin on your life.
- **Follow-up log** — speak a thing to follow up on, agent tracks it and pings you when the context is relevant ("you mentioned Jane — you said you'd get back to her about the venue").
- **Decision support tile** — "should I...?" prompt with agent weighing in. Agent considers your calendar, recent conversations, known preferences, and gives a reasoned recommendation.
- **Conversation resumption** — tap to jump back to "where we left off" in an agent conversation.
- **Quick email draft** — dictate an email via voice, agent composes + sends via your email account. Approval gate before send.
- **Shopping list viewer** — pushed from host, tick items off with a tap.
- **Reading list viewer** — short snippets the agent thinks you'd enjoy, shown when you have a moment.
- **Daily journal prompt** — at scheduled times, watch buzzes with a question ("what went well today?") and records a voice answer to your journal.
- **Audience engagement indicator** — for workshops, agent reads the room (chat backchannel, questions) and pushes a subtle haptic scale ("lively / bored / lost") to the presenter.

### Hardware-leveraging features

- **IR remote control tile** — the T-Watch S3 has an IR LED (`USING_IR_REMOTE` in platformio.ini). Save codes from household remotes (TV, AC) and control them from your wrist. Geeky + delightful.
- **BLE HID clicker** — presentation slide advancer. The T-Watch has BLE, `lv_obj_add_event_cb` already handles taps. Pair with a laptop, use as Bluetooth remote.
- **BLE proximity detection** — "you're near Bob's desk" (requires Bob's BLE tag).
- **BLE beacon scanning** for presence-aware actions.

### Health and wellness

- **Breathing coach** — haptic pulses in sync with a 4-7-8 breath pattern for 2 minutes. The haptic motor can already do variable-duration buzzes. Mental health benefit, very cheap to build.
- **Silent haptic alarm** — wake-up alarm that uses only vibration, not sound. Less disruptive to a partner.
- **Rising alarm** — alarm that starts faint and ramps up over 5 minutes. Gentler than beeping.
- **Posture/ergonomic coach** — accelerometer orientation can detect slouching vs upright (noisy but possible).
- **Fall detection** — sudden deceleration + no-motion → auto-alert via host to a predefined emergency contact. Heavy, requires thought on ethics/false-positives.

### Focus and productivity

- **Focus lockout mode** — pomodoro that also hides all grid tiles except Speak, silences notifications, reports total focused time. Extends existing pomodoro.
- **Classroom timer** — silent countdown for class periods with haptic alerts at interval boundaries. Scott is a former teacher — could be a whole mode for workshops.
- **Workshop silent cues** — buzz patterns at scheduled cues (pause for questions, water break, wrap up).
- **Reading comprehension coach** for language learning — audio prompts to practice listening.

### Family / social / presence

- **Kindred pulse** — tap a tile to send a haptic buzz to a family member's watch. Zero-word presence signal. Feels like a wordless "I'm thinking of you."
- **Family async voice messages** — short recordings delivered to a family member via their own Jorgenclaw agent. Walkie-talkie without pulling out a phone.
- **Presence broadcasting** — "Scott is in a meeting, don't interrupt" based on calendar + WiFi + state.
- **Shared notes / scratchpad** — post-it-note sync between watch and laptop.

### Security / auth

- **Emergency panic button** — triple-tap on a specific tile sends an alert to a predefined contact with timestamp + last known network. Low code cost, high value in the edge case.
- **Dead-man's switch check-in** — if user doesn't tap the watch every X hours, host alerts someone.
- **Auth-gated tiles** — sensitive features require a pattern tap sequence to unlock (cheap password alternative for low-risk uses).

### Creative / fun

- **Small games** — Simon-says with haptic patterns, tap-back challenge.
- **Dice roller for tabletop games** — tap to roll, haptic on critical hit, visual dice on screen.
- **Coin flip / random picker** — tap to decide.
- **Custom watchfaces** — offer a selection, user picks.
- **Photo of the day** — display a small photo pushed from host (family photo, kid drawing).
- **Morse code trainer** — learn by haptic.

### Smart home integration

- **Home automation toggles** — tap to control lights, thermostat, etc. via home automation webhook.
- **Doorbell notification** — webhook from home automation.
- **Motion sensor alerts** — home security events pushed to watch.

### Passive info display

- **Countdown to next event on home screen** — replace or rotate with the current top-line info. Requires calendar integration on the host.
- **Unread message count badge** — on the home screen, show total unread across Signal + email.
- **Weather trend arrow** — "getting warmer / colder / staying same" instead of just temperature.
- **Ambient context chip** — rotating top-line info (next event, unread count, agent energy assessment).

### Quirky / experimental

- **Audio environment sounds** — pink noise, rain, white noise via the speaker for focus or sleep.
- **Ambient audio recording with permission** — tap-and-hold to record environment, saved for recall later.
- **Gesture library** — distinguish single/double/triple-tap, wrist flick, wrist rotation for hands-free actions.
- **LoRa mesh** — if a board has LoRa, long-range comms between watches without WiFi. Probably not on T-Watch S3 but worth noting for future hardware.

---

## Rejected / deferred features

Not every idea is worth building. These were considered and set aside — with the reasoning so we don't waste time re-litigating them.

### Tilt-to-wake (wrist-raise)

**Rejected in April 2026.** The BMA423 accelerometer on the T-Watch S3 lacks hardware wrist-wear gesture detection. That feature exists on Bosch's newer BMI270 and BHI3 chips but not BMA423. A software polling workaround is possible but experimental — would take weeks of false-positive tuning across sitting/standing/walking/sleeping states. For now, wake methods are button + touch. If a future watch revision uses BMI270/BHI3, this unlocks natively. Full investigation in `groups/main/quad-inbox/twatch-tilt-to-wake-response.md`.

### Wake word ("Hey Jorgenclaw")

**Built then removed in April 2026.** The Edge Impulse inference model worked beautifully — saying "Hey Jorgenclaw" would wake the watch and start recording, fully hands-free. But it required the CPU to stay active at all times to listen, which roughly halved battery life. For a wrist device you want to leave alone for 24+ hours, the tradeoff wasn't worth it.

The full implementation is preserved in git history at tag `v5-stable-2026-04-11-wake-word` — if you want to resurrect it (e.g. for a device that lives on a charger, or if you don't mind overnight charging), `git checkout v5-stable-2026-04-11-wake-word -- src/wake_word.cpp src/wake_word.h lib/Jorgenclaw-project-1_inferencing/` restores everything including the 3 MB PSRAM heap architecture, PDM mic DC offset correction, 8x software gain, and threshold tuning. The commit body of `a1c3875` documents every hard-won lesson.

### Spotify control tile

**Deferred.** Stub in the grid, never implemented. Would require a Spotify API client on the host. Not a priority.

### Clicker (BLE HID presentation remote)

**Listed as an idea above, was originally a grid stub.** Removed from grid to make room for Capture/Remind. Could be resurrected as a tile if the BLE HID work is done.

---

## Feature development workflow (for Scott + agent)

When Scott (or a customer) wants to add a new feature, the typical flow is:

1. **Find it in this catalog.** If it's under "Ideas," look at the brief description to see if it matches what's wanted. If it's under "Planned," there's already a rough design. If it's not listed, add it to "Ideas" first so we have a record.
2. **Promote it.** Move the entry from "Ideas" to "Planned" and fill in enough detail to estimate effort, hardware requirements, and the implementation pointer.
3. **Build it.** Agent reads the implementation pointer, modifies the relevant files, builds, flashes, tests. All work goes through git commits on `master`.
4. **Document it.** Move the entry from "Planned" to "Implemented features." Update [USER_GUIDE.md](USER_GUIDE.md) with the user-facing description of how to use it.
5. **Push.** Commits land on `github.com/jorgenclaw/nanoclaw-watch` so other watches can pull the update.

The marketplace idea is that a customer says *"I'd love to have the TOTP feature on my watch"*, their agent reads this file, sees the feature is under Planned/DESIGN REVIEW, explains the tradeoffs from `TOTP_TRADEOFFS.md`, and if the customer says yes, the agent implements it on their specific watch. The customer doesn't need to read any C++ or TypeScript.

## File map (for agents)

When implementing features, these are the files you'll touch most.

| File | What lives there |
|---|---|
| `src/main.cpp` | Boot, voice loops, grid button dispatch, main loop |
| `src/ui.cpp` | All LVGL screens, widgets, tick handlers, event callbacks |
| `src/network.cpp` | WiFi stack management, all HTTP endpoints the watch calls |
| `src/settings.cpp` | NVS-backed persistent settings (WiFi slots, weather unit, etc.) |
| `src/state.cpp` | State machine (`STATE_HOME`, `STATE_WEATHER`, etc.) |
| `src/config.h` | Compile-time constants (WiFi timeouts, brightness, thresholds) |
| `NanoClaw/src/channels/watch.ts` | Host-side HTTP server for the watch. All watch endpoints live here. |
| `NanoClaw/src/task-scheduler.ts` | Agent task scheduler (for planned nudge features) |
| `NanoClaw/src/db.ts` | SQLite persistence (if a feature needs to survive host restart) |

Each file has comments explaining the "why" of its structure. Read them before making large changes.
