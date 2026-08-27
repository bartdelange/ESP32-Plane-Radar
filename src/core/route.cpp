#include "core/route.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "core/platform.h"

namespace core::route {
namespace {

struct CacheEntry {
  char callsign[9] = {};
  Info info{};
  bool occupied = false;
  bool negative = false;
  bool transient_failure = false;
  unsigned long written_ms = 0;
  unsigned long used_ms = 0;
};

CacheEntry s_cache[config::kRouteCacheSize];
Info s_parsed;
bool s_has_route = false;

bool looksLikeAirlineCallsign(const char* callsign) {
  if (callsign == nullptr) return false;
  const size_t len = strlen(callsign);
  return len >= 4 && len <= 8 &&
         std::isalpha(static_cast<unsigned char>(callsign[0])) &&
         std::isalpha(static_cast<unsigned char>(callsign[1])) &&
         std::isalpha(static_cast<unsigned char>(callsign[2])) &&
         std::isdigit(static_cast<unsigned char>(callsign[3]));
}

CacheEntry* findEntry(const char* callsign) {
  for (auto& entry : s_cache)
    if (entry.occupied && strcmp(entry.callsign, callsign) == 0) return &entry;
  return nullptr;
}

CacheEntry* claimSlot() {
  CacheEntry* victim = &s_cache[0];
  for (auto& entry : s_cache) {
    if (!entry.occupied) return &entry;
    if (entry.used_ms < victim->used_ms) victim = &entry;
  }
  return victim;
}

void formatEndpoint(JsonObjectConst endpoint, char* out, size_t out_len) {
  foldAscii(endpoint["municipality"].as<const char*>(), out, out_len);
  if (out[0] != '\0') return;
  const char* iata = endpoint["iata_code"].as<const char*>();
  if (iata != nullptr) snprintf(out, out_len, "%s", iata);
}

bool parseResponse(platform::BodyReader& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  const JsonObjectConst flight =
      doc["response"]["flightroute"].as<JsonObjectConst>();
  if (flight.isNull()) return true;
  formatEndpoint(flight["origin"].as<JsonObjectConst>(), s_parsed.origin,
                 sizeof(s_parsed.origin));
  formatEndpoint(flight["destination"].as<JsonObjectConst>(),
                 s_parsed.destination, sizeof(s_parsed.destination));
  foldAscii(flight["airline"]["name"].as<const char*>(), s_parsed.airline,
            sizeof(s_parsed.airline));
  s_has_route = s_parsed.origin[0] != '\0' ||
                s_parsed.destination[0] != '\0' || s_parsed.airline[0] != '\0';
  return true;
}

}  // namespace

void foldAscii(const char* in, char* out, size_t out_len) {
  if (out_len == 0) return;
  size_t written = 0;
  if (in == nullptr) {
    out[0] = '\0';
    return;
  }
  for (size_t i = 0; in[i] != '\0' && written + 1 < out_len;) {
    const uint8_t first = static_cast<uint8_t>(in[i++]);
    if (first < 0x80) {
      out[written++] = static_cast<char>(first);
    } else if (first == 0xC3 && in[i] != '\0') {
      const uint8_t second = static_cast<uint8_t>(in[i++]);
      static constexpr char kLatin1[] =
          "AAAAAAECEEEEIIIIDNOOOOOxOUUUUYPs"
          "aaaaaaeceeeeiiiidnooooo/ouuuuypy";
      if (second >= 0x80 && second <= 0xBF)
        out[written++] = kLatin1[second - 0x80];
    } else if ((first & 0xE0) == 0xC0) {
      if (in[i] != '\0') ++i;
    } else if ((first & 0xF0) == 0xE0) {
      for (int n = 0; n < 2 && in[i] != '\0'; ++n) ++i;
    } else if ((first & 0xF8) == 0xF0) {
      for (int n = 0; n < 3 && in[i] != '\0'; ++n) ++i;
    }
  }
  out[written] = '\0';
}

bool consumeLookupBudget(bool attempted, bool transient_failure,
                         uint8_t* remaining) {
  if (remaining == nullptr) return false;
  if (!attempted) return *remaining > 0;
  if (*remaining > 0) --*remaining;
  if (transient_failure) *remaining = 0;
  return *remaining > 0;
}

bool resolve(const char* callsign, Info* out, bool allow_network,
             bool* network_attempted, void (*poll)(), bool* transient_failure) {
  if (network_attempted != nullptr) *network_attempted = false;
  if (transient_failure != nullptr) *transient_failure = false;
  *out = Info{};
  if (!config::kRouteLookupEnabled || !looksLikeAirlineCallsign(callsign))
    return false;

  const unsigned long now = platform::nowMs();
  CacheEntry* entry = findEntry(callsign);
  if (entry != nullptr) {
    const unsigned long ttl = entry->transient_failure
                                  ? config::kRouteRetryTtlMs
                                  : config::kRouteNegativeTtlMs;
    if (!entry->negative || now - entry->written_ms < ttl) {
      entry->used_ms = now;
      *out = entry->info;
      return !entry->negative;
    }
  }
  if (!allow_network) return false;

  if (network_attempted != nullptr) *network_attempted = true;
  s_parsed = Info{};
  s_has_route = false;
  char url[128];
  snprintf(url, sizeof(url), "%s%s", config::kRouteApiBase, callsign);
  const int status =
      platform::HttpClient::getStatus(url, parseResponse, 8000, poll);
  const bool parsed_response = status == 200 || status == 404;
  if (transient_failure != nullptr) *transient_failure = !parsed_response;

  if (entry == nullptr) entry = claimSlot();
  *entry = CacheEntry{};
  entry->occupied = true;
  snprintf(entry->callsign, sizeof(entry->callsign), "%s", callsign);
  entry->info = s_parsed;
  entry->negative = !s_has_route;
  entry->transient_failure = !parsed_response;
  entry->written_ms = now;
  entry->used_ms = now;
  *out = entry->info;
  return s_has_route;
}

}  // namespace core::route
