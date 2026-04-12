# NanoClaw Watch — User Guide

*Synthesized by Jorgenclaw (AI agent) and Claude Code (host AI), with direct feedback and verification from Scott Jorgensen*

> **Feeling stuck?** Ask Jorgenclaw directly — "Hey Jorgenclaw, how do I change the WiFi on my watch?" works.

## What You Have

The NanoClaw Watch is a LILYGO T-Watch S3 running custom firmware that turns it into a wrist-worn voice terminal for your Jorgenclaw AI agent. You can talk to it, and it talks back.

**What it can do right now:**
- Tap the blue button to record a voice message (tap again to send)
- Say "Hey Jorgenclaw" to start recording hands-free (no button needed)
- See the time, date, battery level, and weather
- Set alarms, timers, and use a stopwatch
- Track your steps

---

## WiFi Setup

The watch needs WiFi to talk to your NanoClaw host. It can remember up to **3 WiFi networks** — enough for home, work, and a friend's house. When you walk between them, the watch automatically switches to whichever one is in range.

### First Time Setup

When the watch has no saved networks (first boot or after a reset), it creates its own WiFi hotspot:

| Step | What to do |
|------|-----------|
| 1 | The watch screen shows "WiFi Setup" |
| 2 | On your **phone**, open WiFi settings and connect to **Jorgenclaw-Setup** (no password) |
| 3 | A setup page should appear automatically. If it doesn't, open a browser and go to **192.168.4.1** |
| 4 | Tap **Configure WiFi** |
| 5 | Pick your home network from the list, enter the password, tap **Save** |
| 6 | The watch connects and takes you to the home screen |

That's it. Your network is saved and the watch will reconnect to it automatically, even after reboots and firmware updates.

### Adding a Second or Third Network

You can save up to 3 networks total. To add another one:

| Step | What to do |
|------|-----------|
| 1 | On the watch home screen, **long-press the "WiFi OK" text** in the top-right corner (hold for about 1.5 seconds) |
| 2 | The watch vibrates and opens the setup portal |
| 3 | Connect your phone to **Jorgenclaw-Setup** (same as first-time setup) |
| 4 | Pick the new network, enter its password, tap **Save** |
| 5 | The watch connects and goes back to the home screen |

Your previously saved networks are still remembered. Next time you're in range of any of them, the watch will connect automatically.

### How Multiple Networks Work

- The watch tries all saved networks in order when it boots or loses connection
- Whichever network is in range gets connected — no manual switching needed
- If you save a 4th network, the oldest one gets replaced (first in, first out)
- If the same network name is saved again, the password just gets updated

### If WiFi Stops Working

| Problem | What it means | What to do |
|---------|--------------|------------|
| "WiFi -" on home screen | Not connected to any network | Wait 10 seconds — it retries automatically. If it persists, you may be out of range. |
| Watch shows setup portal on boot | None of your saved networks are in range | Connect your phone to Jorgenclaw-Setup and add a network that's available |
| Watch reboots after 2 minutes on setup screen | Nobody configured WiFi within the timeout | Normal — it reboots and tries again. Either configure it or move into range of a saved network. |
| Need to start fresh | Want to clear all saved networks | Long-press the WiFi label to open the portal. The new network you enter replaces one of the saved slots. |

---

## Voice Commands

### Tap to Talk

1. Tap the blue **Speak** button on the home screen
2. Talk — you'll see a red recording indicator with a timer
3. Tap the **right side** of the button to send, or the **left side** to cancel
4. The button turns yellow ("Sending...") while the host processes your message
5. Green "Sent" confirmation with a buzz, then the response appears

### "Hey Jorgenclaw" (Hands-Free)

Just say **"Hey Jorgenclaw"** while the watch is awake on the home screen. It automatically starts recording — same flow as if you tapped the button.

**Important:** The wake word only works while the screen is on. If the watch is asleep (screen dark), tap it to wake it first, then say the wake word.

**Tips for best detection:**
- Speak clearly, about 6 inches from the watch
- Works best in quiet environments
- If it doesn't trigger, try a bit louder — the watch mic is small

---

## Weather

Tap the **weather button** on the home screen (bottom-right area) to see:
- Current temperature
- Today's high, tomorrow's high, tonight's low
- UV index and wind speed/direction
- Sunrise and sunset times
- Your location (shown below the "Weather" header)

**Switching between Fahrenheit and Celsius:** Tap the **F** or **C** button on the weather screen.

**Refreshing weather:** Tap the green **Refresh** button.

---

## Clock Features

Tap the **Clock** button on the home screen to access:

- **Alarm** — set a one-time alarm (buzzes and vibrates when it fires)
- **Timer** — countdown timer with audio + haptic alert
- **Stopwatch** — tap to start/stop, lap tracking

---

## Steps

Tap the **Steps** button to see your step count. **Tap it twice quickly** to reset the counter to zero.

---

## Sleep

The watch automatically sleeps after 30 seconds of no interaction (screen goes dark to save battery). To wake it:
- **Tap the screen**, or
- **Press the side button**

You can also manually sleep by tapping the **Sleep** button on the home screen.

---

## Battery

The battery percentage shows in the top-left corner of the home screen. A **+** prefix (like "+87%") means it's currently charging via USB.

---

## Troubleshooting

| Problem | What to do |
|---------|-----------|
| Screen is black / unresponsive | Press the side button. If nothing happens, plug in USB to charge — the battery may be dead. |
| "No speech detected" after recording | The mic didn't pick up enough audio. Hold the watch closer to your mouth (6 inches) and speak up. |
| Watch keeps going to sleep too fast | The idle timer is 30 seconds. Tap the screen to reset it. Timers and alarms prevent auto-sleep while they're running. |
| Response text is cut off | Scroll down on the response screen. Tap **Close** in the top-right when done. |
| Wake word never triggers | Make sure the screen is on (wake it first). Say "Hey Jorgenclaw" clearly, about 6 inches away. Background noise reduces accuracy. |
