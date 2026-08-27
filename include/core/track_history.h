#pragma once

#include <cstddef>

namespace core::track {

struct Point {
  float lat;
  float lon;
};

void record(const char* hex, float lat, float lon);
void expireStale();
size_t path(const char* hex, const Point** points);
void clear();

/** Deterministic variants used by host tests. */
void recordAt(const char* hex, float lat, float lon, unsigned long now_ms);
void expireStaleAt(unsigned long now_ms);

}  // namespace core::track
