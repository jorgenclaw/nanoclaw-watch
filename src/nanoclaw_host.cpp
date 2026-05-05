// =============================================================================
// nanoclaw_host — implementation
// =============================================================================

#include "nanoclaw_host.h"
#include "config.h"

#include <WiFi.h>
#include <ESPmDNS.h>

namespace {
    // ---- Tunables (could move to config.h if you ever want to tweak) ----
    constexpr uint32_t CACHE_TTL_MS = 30 * 1000;    // re-resolve every 30s
    constexpr uint32_t MDNS_TIMEOUT = 2000;         // ms to wait for mDNS reply
    // ---------------------------------------------------------------------

    IPAddress  cachedIP;
    uint32_t   cachedAt   = 0;
    bool       mdnsStarted = false;

    bool isZero(const IPAddress& ip) {
        return ip == IPAddress(0, 0, 0, 0);
    }
}

void NanoclawHost::begin() {
    if (mdnsStarted) return;

    // The watch announces itself on the LAN. Optional but useful: when EVO
    // runs `avahi-browse -ar` you'll see "nanoclaw-watch.local" appear, which
    // confirms the watch and the host are on the same broadcast domain.
    if (MDNS.begin("nanoclaw-watch")) {
        mdnsStarted = true;
        Serial.println("[mdns] up: nanoclaw-watch.local");
    } else {
        Serial.println("[mdns] init failed; falling back to DNS / literal");
    }
}

IPAddress NanoclawHost::resolveHost() {
    // 1. Cached
    if (!isZero(cachedIP) && (millis() - cachedAt) < CACHE_TTL_MS) {
        return cachedIP;
    }

    // 2. mDNS lookup
    if (mdnsStarted) {
        IPAddress mdnsIP = MDNS.queryHost(NANOCLAW_HOSTNAME, MDNS_TIMEOUT);
        if (!isZero(mdnsIP)) {
            Serial.printf("[mdns] %s.local -> %s\n",
                          NANOCLAW_HOSTNAME, mdnsIP.toString().c_str());
            cachedIP = mdnsIP;
            cachedAt = millis();
            return cachedIP;
        }
        Serial.printf("[mdns] query for %s.local timed out\n", NANOCLAW_HOSTNAME);
    }

    // 3. lwIP DNS — works if the LAN has a real DNS server with the entry.
    //    Most home networks don't, but it's a free fallback before the literal.
    {
        String fqdn = String(NANOCLAW_HOSTNAME) + ".local";
        IPAddress dnsIP;
        if (WiFi.hostByName(fqdn.c_str(), dnsIP) == 1 && !isZero(dnsIP)) {
            Serial.printf("[dns] %s -> %s\n", fqdn.c_str(), dnsIP.toString().c_str());
            cachedIP = dnsIP;
            cachedAt = millis();
            return cachedIP;
        }
    }

    // 4. Fallback literal
    {
        IPAddress fb;
        if (fb.fromString(NANOCLAW_FALLBACK_IP)) {
            Serial.printf("[host] fallback to %s\n", NANOCLAW_FALLBACK_IP);
            cachedIP = fb;
            cachedAt = millis();
            return cachedIP;
        }
    }

    Serial.println("[host] all resolution paths failed");
    return IPAddress(0, 0, 0, 0);
}

String NanoclawHost::baseURL() {
    IPAddress ip = resolveHost();
    if (isZero(ip)) return String();
    return String("http://") + ip.toString() + ":" + String(NANOCLAW_HOST_PORT);
}

void NanoclawHost::invalidateCache() {
    cachedAt = 0;
    cachedIP = IPAddress(0, 0, 0, 0);
}
