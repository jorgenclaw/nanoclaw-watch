# NanoClaw Watch

*Synthesized by Jorgenclaw (AI agent) and Claude Code (host AI), with direct prompting and verification from Scott Jorgensen*

A wrist-worn voice terminal for your personal AI agent — built on the LilyGo T-Watch S3 and T-Watch S3 Plus.

---

## What is this?

The NanoClaw Watch turns a LilyGo T-Watch S3 into a voice interface for [Jorgenclaw](https://jorgenclaw.ai), a self-hosted personal AI agent. You press and hold a button, say what you need, and your agent answers — on your wrist.

The watch handles input and output: microphone, speaker, touchscreen, and vibration motor. Your NanoClaw server (running on your home machine or a small VPS) handles the intelligence: it transcribes your voice, figures out what you need, and sends a response back. Because the server has full compute and all your integrations, your watch can do things a standalone smartwatch can't — act on your calendar, send messages, run custom tasks, or do anything else your agent knows how to do.

This is free and open source software. Build it yourself, modify it, run it your way. No subscription, no cloud required, no vendor lock-in.

---

## Support and ongoing development

This project is actively developed. New features, bug fixes, and hardware variants are added regularly.

**If you get stuck:** Scott is reachable via Signal (ScottJorgensen.51) or email (hello@jorgenclaw.ai) and is happy to help you get your watch running. This isn't abandonware — there's a human who built this and uses it every day.

**If you want a new feature:** The firmware is designed to be extended by AI coding agents. Point your agent (Claude Code, Cursor, Copilot, or any tool with file access) at this repo and ask it to read [`docs/WATCH_FEATURES.md`](docs/WATCH_FEATURES.md) — it catalogs every feature, every idea in the queue, and includes step-by-step implementation notes written specifically for AI agents to act on. You don't have to write firmware yourself. Tell your agent what you want, and it will build it for you.

**Feature ideas and feedback** are genuinely welcome — open an issue or DM Scott directly.

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
Daily use and troubleshooting: [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md)
Should you put 2FA codes on your wrist? [`docs/TOTP_TRADEOFFS.md`](docs/TOTP_TRADEOFFS.md)

> 💡 **Feeling stuck or not sure where to start?** Point your AI agent — Claude Code, Cursor, Copilot, or any coding assistant — at this repo and say: *"Read the README and NEXT_STEPS.md and tell me what to do first."* Ask it to explain each step before you do it. You don't need to understand all of this upfront — a good AI agent will walk you through it and teach you along the way.

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

> 🔒 **Want privacy?** Send an end-to-end encrypted DM via [NIP-17](https://github.com/nostr-protocol/nips/blob/master/17.md) (Gift Wrap). Supported by all major Nostr clients — [Primal](https://primal.net), [Amethyst](https://github.com/vitorpamplona/amethyst), and [Ditto](https://ditto.pub) included. Your message is encrypted before it leaves your device — no relay sees the content.

| Model | Price | Includes |
|-------|-------|---------|
| T-Watch S3 | $160 | Hardware, firmware, testing, US shipping |
| T-Watch S3 Plus ⭐ | $190 | Hardware, firmware, testing, US shipping + GPS |

Availability subject to parts in stock.

---

## License

MIT — use it, fork it, ship it. See [LICENSE](LICENSE) for the full text.

---

## About

Built by [Scott Jorgensen](https://jorgenclaw.ai) together with [Jorgenclaw](https://jorgenclaw.ai), a self-hosted AI agent running on [NanoClaw](https://github.com/qwibitai/nanoclaw). Part of the [Sovereignty by Design](https://sovereignty.jorgenclaw.ai) project — practical digital sovereignty for regular people.
