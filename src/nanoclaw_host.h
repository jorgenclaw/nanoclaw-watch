// =============================================================================
// nanoclaw_host — mDNS-based NanoClaw host discovery
// =============================================================================
//
// Resolves NANOCLAW_HOSTNAME (`nanoclaw.local` by default) via:
//   1. mDNS lookup against the LAN
//   2. Fallback to lwIP DNS (works on networks with a real DNS server)
//   3. Fallback to NANOCLAW_FALLBACK_IP (compile-time literal in config.h)
//
// Result is cached for CACHE_TTL_MS so we don't pay the mDNS round-trip on
// every HTTP request, but re-resolved often enough to follow IP changes
// caused by DHCP rotation or the host moving networks.
//
// Designed to be called from network.cpp wherever NANOCLAW_HOST_URL used to
// be string-concatenated. The replacement is `NanoclawHost::baseURL() + path`.
//
// =============================================================================

#pragma once
#include <Arduino.h>
#include <IPAddress.h>

namespace NanoclawHost {
    // Initialize once after WiFi has an IP address (i.e. WL_CONNECTED).
    // Idempotent — safe to call again on reconnect. Brings up the watch's own
    // mDNS responder (so it announces as `nanoclaw-watch.local` for host-side
    // debugging) and primes the resolver state.
    void begin();

    // Returns the resolved IP for NANOCLAW_HOSTNAME. Order:
    //   1. cached value if newer than CACHE_TTL_MS
    //   2. mDNS query for <NANOCLAW_HOSTNAME>.local
    //   3. lwIP DNS query for <NANOCLAW_HOSTNAME>.local (rare but free)
    //   4. NANOCLAW_FALLBACK_IP literal
    // Returns IPAddress(0,0,0,0) only if step 4 also fails to parse, which
    // shouldn't happen with a valid config.h.
    IPAddress resolveHost();

    // Convenience for HTTP callers. Returns "http://1.2.3.4:3000" with no
    // trailing slash. Returns "" (empty String) if resolveHost() failed.
    // Callers should null-check via `url.length() > 7` (because "http://" is
    // 7 chars).
    String baseURL();

    // Forces the next resolveHost() call to re-query, ignoring cache. Call
    // this on errors (e.g. HTTP request failed) so we don't keep retrying a
    // stale cached IP.
    void invalidateCache();
}
