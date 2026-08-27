#include "core/track_history.h"

#include <cmath>
#include <cstring>

#include "config.h"
#include "core/platform.h"

namespace core::track {
namespace {

struct Track {
  char hex[7] = {};
  Point points[config::kTrackHistoryDepth] = {};
  uint8_t count = 0;
  uint8_t head = 0;
  unsigned long last_ms = 0;
  bool occupied = false;
};

Track s_tracks[config::kTrackHistoryMax];

Track* find(const char* hex) {
  for (auto& track : s_tracks) {
    if (track.occupied && strcmp(track.hex, hex) == 0) return &track;
  }
  return nullptr;
}

Track* claim() {
  Track* victim = &s_tracks[0];
  for (auto& track : s_tracks) {
    if (!track.occupied) return &track;
    if (track.last_ms < victim->last_ms) victim = &track;
  }
  return victim;
}

void reset(Track* track) { *track = Track{}; }

}  // namespace

void recordAt(const char* hex, float lat, float lon, unsigned long now_ms) {
  if (hex == nullptr || hex[0] == '\0') return;
  Track* track = find(hex);
  if (track == nullptr) {
    track = claim();
    reset(track);
    track->occupied = true;
    strncpy(track->hex, hex, sizeof(track->hex) - 1);
  }
  if (track->count > 0) {
    const size_t last =
        (track->head + config::kTrackHistoryDepth - 1) % config::kTrackHistoryDepth;
    const float dlat = lat - track->points[last].lat;
    const float dlon = lon - track->points[last].lon;
    if (dlat * dlat + dlon * dlon < config::kTrackHistoryMinStepDeg2) {
      track->last_ms = now_ms;
      return;
    }
  }
  track->points[track->head] = Point{lat, lon};
  track->head = static_cast<uint8_t>((track->head + 1) % config::kTrackHistoryDepth);
  if (track->count < config::kTrackHistoryDepth) ++track->count;
  track->last_ms = now_ms;
}

void record(const char* hex, float lat, float lon) {
  recordAt(hex, lat, lon, platform::nowMs());
}

void expireStaleAt(unsigned long now_ms) {
  for (auto& track : s_tracks) {
    if (track.occupied && now_ms - track.last_ms >= config::kTrackHistoryTtlMs)
      reset(&track);
  }
}

void expireStale() { expireStaleAt(platform::nowMs()); }

size_t path(const char* hex, const Point** points) {
  static Point ordered[config::kTrackHistoryDepth];
  *points = ordered;
  Track* track = hex == nullptr ? nullptr : find(hex);
  if (track == nullptr) return 0;
  const size_t start =
      (track->head + config::kTrackHistoryDepth - track->count) %
      config::kTrackHistoryDepth;
  for (size_t i = 0; i < track->count; ++i)
    ordered[i] = track->points[(start + i) % config::kTrackHistoryDepth];
  return track->count;
}

void clear() {
  for (auto& track : s_tracks) reset(&track);
}

}  // namespace core::track
