# NanoClaw Watch — User Guide

*Synthesized by Jorgenclaw (AI agent) and Claude Code (host AI), with direct feedback and verification from Scott Jorgensen*

> **Feeling stuck?** Ask Jorgenclaw directly — "Hey Jorgenclaw, how do I change the WiFi on my watch?" works. Don't be afraid to ask where you are in the process and what to do next.

## What You Have

The NanoClaw Watch is a LILYGO T-Watch S3 running custom firmware that turns it into a wrist-worn voice terminal for your Jorgenclaw AI agent. You tap the blue button to talk to it, and it talks back through your Jorgenclaw host.

**What it can do right now:**

- Tap the blue button to record a voice message (tap again to send)
- See the time, date, battery level, and weather
- Set alarms, timers, a stopwatch, and a Pomodoro work/rest cycle
- Track your steps
- Receive notifications pushed from your Jorgenclaw host
- Manage your saved WiFi networks on-device (including forgetting ones you no longer use — see the WiFi section for why this matters)

---

## WiFi Setup

The watch needs WiFi to talk to your NanoClaw host. It can remember **up to 10 WiFi networks** — enough for home, work, a few friends' houses, coffee shops, a hotel or two, whatever. When you walk between them, the watch automatically switches to whichever one is in range.

### First Time Setup

When the watch has no saved networks (first boot or after a reset), it creates its own WiFi hotspot:

| What you want to do | Steps |
|---|---|
| Get the watch on WiFi for the first time | 1. The watch screen shows "WiFi Setup"<br>2. On your **phone**, open WiFi settings and connect to **Jorgenclaw-Setup** (no password)<br>3. A setup page should appear automatically. If it doesn't, open a browser and go to **192.168.4.1**<br>4. Tap **Configure WiFi**<br>5. Pick your home network from the list, enter the password, tap **Save**<br>6. The watch connects and takes you to the home screen |

That's it. Your network is saved and the watch will reconnect to it automatically, even after reboots and firmware updates.

### Adding More Networks

You can save up to 10 networks total. To add another one:

| What you want to do | Steps |
|---|---|
| Save a new WiFi network | 1. On the home screen, **long-press the "WiFi OK" text** in the top-right corner (hold for about 1.5 seconds)<br>2. The watch vibrates and opens the setup portal<br>3. Connect your phone to **Jorgenclaw-Setup** (same as first-time setup)<br>4. Pick the new network, enter its password, tap **Save**<br>5. The watch connects and goes back to the home screen |

Your previously saved networks are still remembered. Next time you're in range of any of them, the watch will connect automatically.

### Removing (Forgetting) a Saved Network

This is a new feature — and it matters more than it might sound. Read the "Why this matters" section just below the table.

| What you want to do | Steps |
|---|---|
| Remove a network you no longer visit | 1. On the home screen, open the scrollable grid of buttons<br>2. Tap the **WiFi** button<br>3. You'll see a list of every saved network, with the one you're currently connected to shown at the top<br>4. **Tap the network you want to remove.** The row turns red and the label changes to "Tap again: \<network name\>"<br>5. **Tap the same row again within 3 seconds** to confirm. The network disappears from the list.<br>6. Tap **Close** in the top-right corner when you're done |

**Want to back out?** Just wait 3 seconds without tapping — the red highlight disappears and no change is made. Or tap a different row, or tap Close.

#### Why removing old networks matters (the privacy part)

When your watch is looking for WiFi, it does something called an "active probe scan": it **shouts the names of its saved networks into the air**, listening for them to shout back. Every saved network becomes a little beacon that says, *"Hi, I'm looking for HomeWifi. Hi, I'm looking for Mom-and-Dads-Netgear. Hi, I'm looking for StarbucksDowntown."* Anyone nearby with a $15 radio scanner can record that list.

Unlike your phone or laptop, the watch **can't hide behind a VPN** — the probe scanning happens below the networking layer that a VPN can reach. It's baked into how WiFi works on these small chips. The only way to shut off a probe is to forget the network.

Practically, this means:

- Every saved network is a small, passive leak of places you've been
- 10 saved networks = 10 little beacons broadcasting your history
- You don't need "Grandma's House" saved anymore if you only went there twice in 2023 — forget it
- You especially don't need hotel or airport networks saved — those advertise the places you travel

The "forget" button exists so you can prune the list to just the places you actually visit regularly. 3–5 saved networks is plenty for most people. 10 is available if you want it, but consider it a budget rather than a target.

### How Multiple Networks Work

- The watch tries all saved networks in order when it boots or loses connection
- Whichever network is in range gets connected — no manual switching needed
- If you save an 11th network, the oldest one (slot 1) gets dropped and the rest shift down
- If the same network name is saved again, the password just gets updated

### If WiFi Stops Working

| Problem | What it means | What to do |
|---|---|---|
| "WiFi -" on home screen | Not connected to any network | Wait 10 seconds — it retries automatically. If it persists, you may be out of range. |
| Watch shows setup portal on boot | None of your saved networks are in range | Connect your phone to Jorgenclaw-Setup and add a network that's available |
| Watch reboots after 2 minutes on setup screen | Nobody configured WiFi within the timeout | Normal — it reboots and tries again. Either configure it or move into range of a saved network. |
| Need to start completely fresh | Want to clear all saved networks | Open the WiFi screen and forget networks one by one, or long-press the WiFi label to open the portal and save a new one |

---

## Voice Commands

Tap the big blue **Speak** button on the home screen.

| What you want to do | Steps |
|---|---|
| Send a voice message to Jorgenclaw | 1. Tap the blue **Speak** button on the home screen<br>2. Talk — you'll see a red recording indicator with a timer<br>3. Tap the **right side** of the button to send, or the **left side** to cancel<br>4. The button turns yellow ("Sending...") while the host processes your message<br>5. Green "Sent" confirmation with a buzz, then the response appears |

**Tips for best recording quality:**

- Speak clearly, about 6 inches from the watch
- Works best in quiet environments
- If the host says "No speech detected," try a bit louder — the watch microphone is small

---

## Weather

Tap the **weather button** on the home screen (bottom-right area) to see:

- Current temperature
- Today's high, tomorrow's high, tonight's low
- UV index and wind speed/direction
- Sunrise and sunset times
- Your location (shown below the "Weather" header)

| What you want to do | How |
|---|---|
| Switch between Fahrenheit and Celsius | Tap the **F** or **C** button on the weather screen |
| Refresh weather now | Tap the green **Refresh** button |

---

## Clock Features

Tap the **Clock** button on the home screen to access:

- **Alarm** — set a one-time alarm (buzzes and vibrates when it fires)
- **Timer** — countdown timer with audio + haptic alert
- **Stopwatch** — tap to start/stop, lap tracking
- **Pomodoro** — work/rest cycle timer

---

## Steps

Tap the **Steps** button to see your step count. **Tap it twice quickly** to reset the counter to zero.

---

## Sleep (and Waking Up)

The watch automatically goes to sleep after 30 seconds of no interaction — the screen goes fully dark and the processor enters a low-power state that uses almost no battery.

| What you want to do | How |
|---|---|
| Wake the watch back up | **Tap the screen** or **press the side button** on the right edge |
| Manually put the watch to sleep | Tap the **Sleep** button at the bottom of the home screen |

**Note:** There's no "wrist raise to wake" or "wake word" feature right now. The watch sleeps deeply to save battery, and waking up requires a deliberate touch or button press. See the "Why we made these choices" section below for the reasoning.

---

## Battery

The battery percentage shows in the top-left corner of the home screen. A **+** prefix (like "+87%") means it's currently charging via USB.

Tap anywhere on the battery reading to open a detailed system-info screen showing voltage, charge current, and uptime.

---

## Troubleshooting

| Problem | What it means | What to do |
|---|---|---|
| Screen is black / unresponsive | Battery might be dead, or the watch might be stuck | Press the side button. If nothing happens, plug in USB to charge — give it 5 minutes and try again. |
| "No speech detected" after recording | The mic didn't pick up enough audio | Hold the watch closer to your mouth (about 6 inches) and speak up |
| Watch keeps going to sleep too fast | The idle timer is 30 seconds | Tap the screen to reset it. Timers and alarms prevent auto-sleep while they're running. |
| Response text is cut off | The response is longer than the screen | Scroll down on the response screen. Tap **Close** in the top-right when done. |
| I tapped a WiFi row by accident and it turned red | You accidentally armed a delete | Wait 3 seconds — the red goes away and nothing is forgotten. Or tap Close. |

---

## Why We Made These Choices (FAQ)

This section exists because some of the choices we made may look odd from the outside. If you're curious why the watch doesn't have wake word, tilt-to-wake, or more aggressive features like a smartwatch from a big-name brand, here's the honest backstory.

### Why no "Hey Jorgenclaw" wake word?

We built it, tested it, and it worked beautifully — saying "Hey Jorgenclaw" would wake the watch and start recording, completely hands-free. But wake word detection has a hidden cost: **the watch's processor has to stay awake all the time, listening.**

A watch that's always listening is a watch whose battery drains fast. In real-world use, wake word cut battery life roughly in half compared to letting the processor go to sleep between taps. The tradeoff wasn't worth it for a device you're supposed to wear all day.

The wake word code still exists in our git history (you can look at the commits tagged `v5-stable-2026-04-11-wake-word` if you want to resurrect it), and if you're building a watch that lives on a charger, or if battery life isn't your concern, it's straightforward to turn back on. For a wrist-worn device you want to leave alone for 24+ hours, we decided a button tap is a reasonable price to pay for not charging every night.

### Why no "raise your wrist to wake up the screen"?

You'd think a hardware feature this simple would be, well, a hardware feature. It is — but only on more expensive accelerometer chips than the one this watch has.

The T-Watch S3 uses a Bosch **BMA423** accelerometer, which is a good sensor for counting steps and detecting double-taps, but it **doesn't know which direction "up" is** relative to a face. It can detect motion and orientation in the raw sense, but it has no built-in "wrist raised to look at watch" gesture detector. That feature (called "wrist wear wakeup") exists on Bosch's newer BMI270 and BHI3 chips — which is what Apple Watches, Fitbits, and Garmin watches actually use — but not the BMA423.

We looked at implementing it in software: wake up the processor every half-second, read the accelerometer, check if the watch is pointed at your face, and decide whether to light the screen. It would work, sort of, but getting the thresholds right without false positives (walking, typing, turning over in bed) would take weeks of tuning with real wrist-wear testing. For now, we decided it's not worth the complexity — a button press is deterministic, zero false positives, and takes less than a second.

If you're building a successor watch and you want this feature, **specify a BMI270 or BHI3 instead of BMA423** on your PCB design. Those chips have deliberate wrist-wear detection in hardware and give you the "check your watch" gesture for free.

### Why only 10 saved WiFi networks (not unlimited)?

10 is a chosen budget, not a hard limit — we could add more, and the storage is small enough that 100 would still fit.

But more saved networks means more privacy leakage. Every saved network gets actively probed by the watch on reconnect, and every probe is a small broadcast of where you've been (see the "Why removing old networks matters" section above). 10 felt like the right balance between "enough flexibility for a real life" and "not so many that your probe list becomes a dossier."

If you find 10 isn't enough for you, that's a signal — ask yourself whether some of those networks are one-off places you visited once and should forget. If you genuinely need more than 10, the limit lives in `src/settings.h` as `WIFI_MAX_NETWORKS` and can be bumped.

### Why can't the watch use a VPN?

You might assume that if your phone is using a VPN, your watch should just use the same one. Unfortunately that's not how WiFi works on small embedded devices.

A VPN is a tunnel for internet-layer traffic — it encrypts what your device *sends* to websites. But the WiFi probe scanning we talked about earlier happens **below** that tunnel, at the hardware level, before any VPN software could ever see it. The watch has to name the networks it wants to connect to *before* it can connect to anything, VPN or not. That's an inherent property of how WiFi authentication works, not a bug we can fix.

The practical upshot: your wrist watch will always emit its saved network list into the air as long as it has saved networks. The only privacy control is pruning the list. That's why the "forget" button exists, and why we made it easy to use.

### Why does the watch need a Jorgenclaw host? Why not just use it standalone?

The watch is a *terminal* — a small face onto a larger AI agent (Jorgenclaw) running on a computer somewhere. The compute, the memory, the knowledge, the integrations with your email and calendar and notes and all of that — it all lives on the host. The watch is the microphone, the speaker, and the screen.

This is deliberate. A wrist watch is too small to run a real AI agent on, and it's way too battery-constrained to try. Your host computer (laptop, home server, desktop) has the electricity, the storage, and the horsepower. The watch is the wrist-worn window onto it.

If your host computer is off, the watch can still show you the time, weather, and timers — but voice commands and notifications won't work because there's nothing at the other end to answer.

---

## For the Curious: Where the Code Lives

If you want to learn from what we built (or fork it and make it yours), the full source is at **github.com/jorgenclaw/nanoclaw-watch**. The commit history includes the complete wake word implementation (with notes on all the hard-won lessons about Edge Impulse heap allocation and PDM microphone DC offset), which we then deliberately removed — so if you want to add wake word to a device that doesn't mind the battery cost, those commits are your starting point.

Git tags mark stable milestones:

- `v5-stable-2026-04-11-wake-word` — last stable release with wake word enabled
- `v7-stable-2026-04-13-grid-overhaul` — scrollable grid, DND, pomodoro, flashlight, notifications (wake word still on)
- `v8-stable-*` (upcoming) — wake word removed, 10 WiFi slots, credential forget UI

Feel free to open issues or discussions on the GitHub repo if you're hitting something confusing.
