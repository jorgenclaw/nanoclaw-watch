# NanoClaw Watch — Next Steps

A step-by-step playbook to take this project from "compiles cleanly" to "Scott talks
to Jorgenclaw on his wrist." Do these in order. Each step is a checkpoint — finish
it, verify it works, then move to the next.

---

## Step 1 — Get the watch to flash and boot (~15 minutes)

**Goal:** see "NanoClaw Watch boot" in the serial monitor and a clock on the screen.

1. **Find your computer's LAN IP.**
   ```
   hostname -I | awk '{print $1}'
   ```
   Write it down. Probably looks like `192.168.1.X`.

2. **Edit the config file.**
   ```
   xdg-open ~/projects/nanoclaw-watch/src/config.h
   ```
   Change three values:
   - `WIFI_SSID` → your WiFi name
   - `WIFI_PASSWORD` → your WiFi password
   - `NANOCLAW_HOST_URL` → `"http://YOUR_IP:3000"` (the IP from step 1)

   Save and close.

3. **Give yourself permission to flash the watch (one-time).**
   ```
   sudo usermod -aG dialout $USER
   ```
   **Then log out and log back in.** Pop_OS won't pick up the group change until you do.

4. **Plug the watch in via USB-C** if it isn't already. Verify it shows up:
   ```
   ls /dev/ttyACM0
   ```
   Should print `/dev/ttyACM0` with no error.

5. **Flash the watch.**
   ```
   cd ~/projects/nanoclaw-watch
   ~/.local/bin/pio run --target upload
   ```
   Takes about 30 seconds. If it fails with "waiting for download," **hold the side
   button** while you unplug and re-plug the USB cable, then try again.

6. **Open the serial monitor to watch it boot.**
   ```
   ~/.local/bin/pio device monitor
   ```
   You should see:
   ```
   === NanoClaw Watch boot ===
   [net] connecting to YOUR_SSID
   [main] setup complete
   ```
   And on the watch screen: a clock (probably `--:--` until it gets WiFi/NTP),
   "BAT XX%", "WiFi OK", "Tap to speak" button, and the 4 quick-reply buttons.

   Press `Ctrl+C` to exit the monitor.

**Stop here. Tell Quad what you see.** If anything's wrong, this is the place to
catch it before adding complexity.

---

## Step 2 — Build the host-side endpoint (next session — Quad task)

**Goal:** the watch's "Tap to speak" button actually does something useful.

Right now the watch will POST to `/api/watch/message` on your NanoClaw host, but
that endpoint doesn't exist yet — so you'll get HTTP 404 errors on every press.
We need to build it on the NanoClaw side.

When you're ready, just tell Quad: **"build the watch host endpoint"** and Quad will:

- Add `POST /api/watch/message` to NanoClaw (text and audio submissions)
- Add `GET /api/watch/poll` (incoming response polling)
- Wire it up so submissions inject into Jorgenclaw's main chat as if they came from you
- Wire response polling so when Jorgenclaw replies, the watch picks it up
- Add the auth token to NanoClaw's config

This is purely a NanoClaw-side task — no firmware changes needed.

---

## Step 3 — First real conversation with Jorgenclaw via the watch

**Goal:** press the speak button, talk for 5 seconds, see Jorgenclaw's reply on the watch.

After Step 2 is done, you just press "Tap to speak" on the watch. The watch will:

1. Show "Listening..." on screen
2. Record your voice for 5 seconds via the PDM mic
3. Show "Sending..."
4. Upload the WAV to your host
5. Host transcribes via Whisper, injects to Jorgenclaw, waits for reply
6. Watch shows the reply on screen with a haptic buzz

If Jorgenclaw replies later (via a scheduled task, an incoming Signal message that
triggers him, etc.), the watch's polling loop will pick it up within 60 seconds
and buzz.

---

## Step 4 — Tune and iterate (ongoing)

After it's working, things you'll probably want to adjust:

| What                      | Where         | How                                                |
|---------------------------|---------------|----------------------------------------------------|
| Quick-tap prompts         | `src/config.h`| Edit `QUICK_PROMPT_1..4`, reflash                  |
| Voice recording length    | `src/config.h`| Change `VOICE_RECORD_SECONDS`, reflash             |
| How often the watch polls | `src/config.h`| Change `POLL_INTERVAL_MS`, reflash                 |
| Idle sleep timeout        | `src/config.h`| Change `IDLE_SLEEP_MS`, reflash                    |
| Brightness                | `src/config.h`| Change `BRIGHTNESS_ACTIVE`, reflash                |
| Timezone                  | `src/config.h`| Change `TZ_OFFSET_SECONDS`, reflash                |

**Reflash workflow:** from the project dir, run `~/.local/bin/pio run --target upload`.

---

## Step 5 — Add more features once the basics feel solid (later)

Things deliberately left out of v1 that you can add when you want them:

- **Step counter on home screen** — sensor is already enabled, just needs an LVGL
  widget added to the home screen
- **TTS playback** — Jorgenclaw's reply read aloud through the watch speaker
  (uses `instance.player.playWAV()` — host needs to return audio in the response)
- **Smart home buttons** — extra screens that fire HTTP requests to Home Assistant,
  Hue, etc. Trivially easy if you have the APIs
- **Battery life tuning** — measure real drain, gate WiFi more aggressively, use
  longer deep-sleep periods between polls
- **Photo viewer** — for images Jorgenclaw sends back
- **Custom watch faces** — multiple home screen layouts, swipe between them

Don't think about these yet. Get Steps 1–3 working first.

---

## Quick reference

**Project location:** `~/projects/nanoclaw-watch/`

**Edit config:** `~/projects/nanoclaw-watch/src/config.h`

**Build:** `cd ~/projects/nanoclaw-watch && ~/.local/bin/pio run`

**Flash:** `cd ~/projects/nanoclaw-watch && ~/.local/bin/pio run --target upload`

**Monitor serial:** `~/.local/bin/pio device monitor` (Ctrl+C to exit)

**Bootloader recovery:** hold side button while plugging in USB

---

## Right now: just do Step 1.

Edit the config, log out and back in, flash the watch, watch it boot. Then come
back and tell Quad what happened.
