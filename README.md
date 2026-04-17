# NanoClaw Watch

*Synthesized by Jorgenclaw (AI agent) and Claude Code (host AI), with direct prompting and verification from Scott Jorgensen*

A wrist-worn voice terminal for your personal AI agent — built on the LilyGo T-Watch S3 and T-Watch S3 Plus.

---

## What is this?

The NanoClaw Watch turns a LilyGo T-Watch S3 into a voice interface for [Jorgenclaw](https://jorgenclaw.ai), a self-hosted personal AI agent. You press and hold a button, say what you need, and your agent answers — on your wrist.

The watch handles input and output: microphone, speaker, touchscreen, and vibration motor. Your NanoClaw server (running on your home machine or a small VPS) handles the intelligence: it transcribes your voice, figures out what you need, and sends a response back. Because the server has full compute and all your integrations, your watch can do things a standalone smartwatch can't — act on your calendar, send messages, run custom tasks, or do anything else your agent knows how to do.

This is free and open source software. Build it yourself, modify it, run it your way. No subscription, no cloud required, no vendor lock-in.

---

## What it does

- 🎙️ **Voice commands** — press and hold the button, speak, get a response
- 🔊 **Audio playback** — hear your agent's reply through the watch speaker
- 🕐 **Clock face** — time, date, and sub-screens for weather and daily stats
- 🌤️ **Weather display** — current conditions fetched by your agent
- 👟 **Step counter** — daily steps via the built-in accelerometer
- 🍅 **Pomodoro timer** — timed focus sessions with haptic feedback
- 📶 **Multi-network WiFi** — remembers multiple networks, roams automatically
- 🔔 **Notifications** — your agent pushes alerts and reminders to your wrist
- ⚙️ **Configured by conversation** — tell your agent what you want, it handles the settings
- 🔄 **OTA firmware updates** — new features delivered over the air (coming soon)

---

## Two versions of the hardware

The firmware runs on both variants of the LilyGo T-Watch S3:

| | T-Watch S3 | T-Watch S3 Plus ⭐ recommended |
|---|---|---|
| Battery | Standard capacity | Larger — noticeably longer between charges |
| GPS | Not present | Built in (off by default, privacy-preserving) |
| Size / weight | Slightly smaller | Slightly larger |
| Price | Lower | Modest premium |

**If you're not sure which to get, choose the S3 Plus.** The bigger battery is the main reason — everything you use daily (voice, notifications, weather) just runs longer between charges. The GPS is turned off by default and physically powered down when not in use; it only activates if you switch it on from the watch settings.

---

## Build it yourself

**You'll need:**
- A LilyGo T-Watch S3 or T-Watch S3 Plus (available on AliExpress and Amazon)
- [PlatformIO](https://platformio.org/install/ide?install=vscode) — a VS Code extension or CLI tool for flashing microcontroller firmware
- A running NanoClaw server — see [qwibitai/nanoclaw](https://github.com/qwibitai/nanoclaw)

**Steps:**
1. Clone this repo
2. Open `src/config.h` and fill in your WiFi name, password, and server address
3. Plug in the watch and run `pio run --target upload`

Full walkthrough: [`NEXT_STEPS.md`](NEXT_STEPS.md)
Everything the firmware can do: [`docs/WATCH_FEATURES.md`](docs/WATCH_FEATURES.md)

> 💡 **Feeling stuck?** Ask your Jorgenclaw agent directly where you are in the process and what to do next. It has read access to all these docs and can walk you through them step by step.

---

## Get a pre-flashed watch

**Don't want to build it yourself? Scott and Jorgenclaw will ship you a ready-to-run watch.**

We source the hardware, flash the firmware, and do the initial setup. You receive a watch that's ready to pair with your NanoClaw server on first boot — no soldering, no build tools, no guesswork.

**We accept payment in cryptocurrency:**
- ⚡ Lightning (instant, low-fee Bitcoin)
- ₿ Bitcoin on-chain
- ɱ Monero (XMR) — for maximum privacy
- Ł Litecoin (LTC) — on-chain or MWeb extension for privacy

**To order or ask questions:**

| Channel | Who | Address |
|---------|-----|---------|
| Signal | Scott | ScottJorgensen.51 |
| Email | Scott | hello@jorgenclaw.ai |
| Nostr DM | Scott | `npub1ghawdls89y3vsjnz0505c2zpkccv7vjpddnpf9rt9m3x7nvsv30qchw63y` |
| Nostr DM | Jorgenclaw (AI agent) | `npub16pg5zadrrhseg2qjt9lwfcl50zcc8alnt7mnaend3j04wjz4gnjqn6efzc` |

> 🔒 **Want privacy?** Nostr supports end-to-end encrypted DMs via [NIP-17](https://github.com/nostr-protocol/nips/blob/master/17.md) (Gift Wrap). Any Nostr client with NIP-17 support can send an encrypted message to either npub above. Your message will be readable only by the recipient — no relay sees the content.

Pricing covers hardware, flashing, testing, and shipping. Availability subject to parts in stock.

---

## License

MIT — use it, fork it, ship it. See [LICENSE](LICENSE) for the full text.

---

## About

Built by [Scott Jorgensen](https://jorgenclaw.ai) together with [Jorgenclaw](https://jorgenclaw.ai), a self-hosted AI agent running on [NanoClaw](https://github.com/qwibitai/nanoclaw). Part of the [Sovereignty by Design](https://sovereignty.jorgenclaw.ai) project — practical digital sovereignty for regular people.
