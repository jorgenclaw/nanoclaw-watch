# TOTP on your wrist — what to think about before saying yes

*Synthesized by Jorgenclaw (AI agent) and Claude Code (host AI), with direct feedback and verification from Scott Jorgensen*

> **Feeling stuck?** Ask Jorgenclaw directly — "Hey Jorgenclaw, should I turn on TOTP codes on my watch?" The agent has read this document and can help you decide based on your specific situation.

## What is TOTP, in plain language

TOTP stands for **Time-based One-Time Password**. It's the rolling 6-digit code that apps like Google Authenticator, Authy, or Proton Pass show you — the code you type in after your password as a second factor of authentication. A new code appears every 30 seconds and the old one stops working.

The code is calculated from two things:
1. A secret "seed" that you and the service share (you got this when you set up 2FA, usually by scanning a QR code)
2. The current time

Your authenticator app stores the seed. It runs the math, shows you the code, you type the code in, the service runs the same math, the codes match, you're in. The seed never leaves your device in normal operation.

**The TOTP-on-watch idea:** instead of unlocking your phone, opening an authenticator app, scrolling to the right account, and squinting at a 6-digit code, a tile on the watch shows you the code immediately. Wrist is faster than phone every time.

## Why it's a nice idea

- **Phone-free 2FA.** If you use 2FA many times a day (Scott does, because he uses Proton Pass heavily), replacing "unlock phone → open app → find account → read code" with "glance at wrist" is a real daily-quality-of-life improvement.
- **Always-with-you.** If you leave your phone in another room, you still have codes on your wrist.
- **Less app-switching.** On a laptop, reaching for your phone breaks your flow. Glancing at a watch doesn't.

## The problem: the watch doesn't talk to the host over HTTPS yet

This is the heart of the security conversation. Read this section carefully — it's the whole point of this document.

### How the watch currently talks to your NanoClaw host

When the watch needs to send or receive something to/from your NanoClaw host (for voice commands, weather, WiFi setup, notifications, etc.), it does so over **plain HTTP** — not HTTPS. HTTPS is the "lock icon in the browser" version that encrypts everything between your device and the server. Plain HTTP is the older version where the content crosses the network in readable form.

**Why plain HTTP?** Two reasons:
1. Setting up HTTPS on a home server requires getting a TLS certificate (usually via Let's Encrypt) and configuring the server. It's doable but it's a chore, and for a watch that only talks to your local NanoClaw host, most users never need it.
2. The ESP32-S3 chip in the watch can do HTTPS but it's slower and uses more battery than plain HTTP. Measurable but not huge.

So today, most NanoClaw watch setups use plain HTTP inside the home network.

### WiFi encryption is not the same as HTTPS

This is the part most people get confused about. When you connect your watch to your home WiFi with a password, the data between the watch and the router IS encrypted at what networking people call "the link layer." That's what WPA2 and WPA3 do. So at a coffee shop, your WiFi password protects you from the person at the next table casually listening in on your traffic.

**But** that encryption only exists between the watch and the access point (the router). After the router, your data continues on to the NanoClaw host. If the path from the router to the host is plain HTTP, anyone who can get between the two can read the traffic.

On your **home network**, that path is usually just a few feet of ethernet cable inside your house, and the only people "between" are whoever's already inside your house and on your WiFi. Low risk for most people.

On a **shared network** (coffee shop, airport, conference, hotel lobby), the path is through a router you don't control, and other people on the same network might be able to intercept traffic with readily-available tools. Theoretically possible, practically uncommon, but the risk is real.

### What the specific TOTP risk looks like

Here's the attack, in concrete terms:

1. You're at a coffee shop. You've connected your watch to the guest WiFi. Your NanoClaw host is back at your house, connected through the internet (via some kind of tunnel, or exposed to the public internet).
2. You want to log into your email. You tap the TOTP tile on your watch. The watch asks your NanoClaw host "what's the code for my email account right now?"
3. The request and the response cross the coffee shop WiFi, the coffee shop's upstream, and eventually the internet until they reach your house.
4. If someone on the coffee shop WiFi is running a packet sniffer, they can see the request and the response **if the connection is plain HTTP**.
5. They now know your current TOTP code.
6. The code is valid for 30 seconds. If they type it in (along with your already-captured password, if they also got that) fast enough, they're in.

**Is this a real attack?** Technically yes. Practically rare. It requires:
- Being on the same WiFi network as you
- Running sniffing tools actively
- Also having your password
- Acting within ~30 seconds

Most attackers don't do targeted work. Most breaches are automated, wide-net attacks against poorly-secured servers, not against individual coffee shop users. BUT — if you're someone whose 2FA codes are worth stealing (targeted by anyone specific), the threat is real.

### Home network vs public network

The single biggest variable is **where you physically are** when you use the TOTP feature.

| Where you are | How risky | Why |
|---|---|---|
| Your own home, alone | Very low | The only devices on your network are yours |
| Your own home, with trusted family | Very low | Your kids aren't sniffing your WiFi |
| Your own home, with a houseguest | Low | Technically possible if they're adversarial and technical, but unlikely |
| A trusted friend's home | Low | Same as above |
| An office network | Medium | Depends on who else is on it and whether IT monitors/logs traffic |
| A coffee shop or airport | Medium-High | Anyone on the network can try, and these networks are often targets |
| A hotel network | Medium-High | Hotel networks are often poorly secured and targets of drive-by attacks |
| A conference network | High | Adversarial environment; security researchers specifically test these |

**If you only ever use your watch's TOTP feature while at home**, the risk is very low and the convenience is very high. This is a perfectly reasonable tradeoff for many people.

**If you travel with your watch and might use TOTP in public places**, the risk is meaningful and you should either not enable the feature or mitigate it first.

## The options, ranked by effort

### Option 1: Don't ship TOTP on the watch (the safest default)

Don't build the feature. Keep using your phone's authenticator app. No new risk. No new convenience either.

**This is the right choice if:** you're not sure what you want, or you're worried about security, or you don't use 2FA often enough to care about the convenience.

### Option 2: Ship TOTP but limit it to your home network

Build the feature. Document clearly that the codes cross the wire in plaintext. Tell the user "only use this at home, never on public WiFi."

**This is the right choice if:** you have a strong "at home" use case and you're disciplined about not tapping the tile in coffee shops. Many people are. Scott probably is.

**What the user experience looks like:** the TOTP tile is always available. The user just knows not to use it on public WiFi. We can add a warning screen on first use and a reminder in the user guide.

### Option 3: Ship TOTP with a pre-shared-key encryption layer

Build the feature. Add a small secret key shared between the watch and the host (configured during firmware flashing). All TOTP requests and responses are encrypted with that key using AES. Even someone sniffing the network sees only gibberish.

**This is the right choice if:** you want the convenience of TOTP on your watch and you sometimes use it in places where you don't trust the network. This is a solid middle ground — it doesn't require you to set up HTTPS with a real certificate, but it gives you encryption on the wire.

**What the user experience looks like:** same as Option 2 for the user. No difference in how you use the tile. The only difference is that the setup process (flashing your watch) includes generating a key, and if you reflash the watch with a different key, you have to update the host too.

**Downsides:** the pre-shared key is only as good as the flashing process. If your watch is stolen, whoever has it can read the key out of flash memory (probably — depends on whether we also enable the ESP32's flash encryption, which is a bigger project). Also, "we rolled our own encryption" is a classic red flag in security circles — even with a well-tested AES library, subtle implementation mistakes are common.

### Option 4: Ship TOTP over HTTPS to your NanoClaw host

The proper solution. Set up a real TLS certificate for your NanoClaw host (e.g., via Let's Encrypt if it's publicly reachable, or via a self-signed cert that the watch is pinned to), and run the host's HTTP server on HTTPS.

**This is the right choice if:** you want "actually secure" rather than "pretty good." HTTPS is a well-understood, widely-audited solution to exactly this problem. Every banking app in the world uses it.

**What the user experience looks like:** same tile, same tap, same glance. The only difference is that setting up your NanoClaw host initially now involves getting a certificate. That's a one-time inconvenience.

**Downsides:** setup is more involved. Let's Encrypt requires your host to be publicly reachable (port 80 open for HTTP challenge, or you use DNS challenge with an API-capable DNS provider). A self-signed certificate works without any of that, but you have to manually trust it in the watch firmware. Also, HTTPS on the ESP32-S3 is slower than plain HTTP — each code fetch might take an extra 500-800 ms. Usually fine.

### Option 5: Store the seeds on the watch itself

Completely different architecture: flash the seeds into the watch during setup. The watch computes the codes locally and never talks to the host at all. No network transit, no sniffing risk.

**This is the right choice if:** you're willing to treat the watch as a trusted device (like a YubiKey) and accept that losing the watch is equivalent to losing an authenticator app.

**What the user experience looks like:** you add each account via the flashing/setup process (possibly via the captive portal — the watch shows a QR code scanner in its web UI, you scan the QR code from the service, the seed gets written to the watch's flash). Then the TOTP tile works fully offline.

**Downsides:** the seeds live on the watch. If the watch is stolen or reflashed by someone who has physical access, they could theoretically extract the seeds. ESP32-S3 flash encryption mitigates this but it's a more advanced setup. Also, if your watch's battery dies before you can copy the seeds somewhere, you might be locked out of accounts — so you'd want to keep a backup copy of each seed on the host anyway.

## Recommendation

**For Scott's use case today:** go with **Option 2 (home-network-only)** as the first shipped version. It's the fastest path to the daily-quality-of-life win, and Scott's actual usage pattern is overwhelmingly at home. Ship it with a first-use warning screen that says "only use this on networks you trust" and a section in the user guide explaining why.

**Then, as a follow-up, add Option 4 (HTTPS)** to the host. This is the right eventual solution — it fixes not just TOTP but every current and future watch↔host endpoint (voice, memo, reminders, notifications, everything). Let's Encrypt + a publicly-reachable host is the usual path.

**For customers who want more security up front:** Option 3 (pre-shared key) is a reasonable middle ground if you specifically worry about sniffing on public networks and don't want to set up HTTPS yet. Your agent can add it to your watch if you ask.

**For advanced users:** Option 5 (seeds on the watch) turns the watch into a YubiKey-style hardware authenticator, which is a neat architecture but is a much bigger project. If you want this, ask your agent for a full design discussion first.

## Whatever you pick, here's how to stay safe

Regardless of which option you choose (including "don't ship it"), these practices reduce risk:

1. **Use strong, unique passwords** for every service. TOTP is a second factor — it only helps if the first factor (password) is already strong.
2. **Enable 2FA everywhere it's offered**, even if you don't use the watch feature. Authenticator apps on your phone are still better than SMS-based 2FA.
3. **Keep backup codes** for every account, stored somewhere you can reach without your watch or phone (like a printed list in a safe).
4. **Treat the watch like a credit card.** If you lose it, assume someone could use it for a short window and change important passwords.
5. **Check the watch is yours** before tapping sensitive tiles. Physically identify your watch by the serial on the back before using TOTP on it — don't accidentally use a borrowed one.

## If you want to turn this on today

Ask your Jorgenclaw agent: *"Read docs/TOTP_TRADEOFFS.md and help me set up Option 2 on my watch."*

The agent will:
1. Confirm you understand the risk model
2. Ask which accounts you want to add
3. Walk you through copying the seeds from your existing authenticator to the NanoClaw host (this is the one step that requires care — the seeds need to cross from wherever they are today to the host, and that transfer should happen somewhere private, not over a public network)
4. Build and flash the TOTP feature to your watch
5. Show you how to use the tile

If you change your mind later and want Option 3 or Option 4 instead, the agent can upgrade the setup. Nothing you do with Option 2 is permanent.
