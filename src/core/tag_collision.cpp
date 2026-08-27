#include "core/tag_collision.h"

namespace core::tag_collision {
namespace {

bool overlaps(const Bounds& a, const Bounds& b) {
  constexpr int kPad = 2;
  return a.visible && b.visible && a.x0 - kPad < b.x1 + kPad &&
         a.x1 + kPad > b.x0 - kPad && a.y0 - kPad < b.y1 + kPad &&
         a.y1 + kPad > b.y0 - kPad;
}

int root(int* parent, int i) {
  while (parent[i] != i) {
    parent[i] = parent[parent[i]];
    i = parent[i];
  }
  return i;
}

}  // namespace

void select(const Bounds* bounds, size_t count, unsigned long phase,
            bool* tag_visible, bool* aircraft_highlighted) {
  if (bounds == nullptr || tag_visible == nullptr ||
      aircraft_highlighted == nullptr) return;
  // Aircraft storage is bounded by core::adsb::kMaxAircraft (32). Keeping the
  // helper independent of ADS-B avoids coupling selection to network models.
  constexpr size_t kCapacity = 32;
  if (count > kCapacity) count = kCapacity;
  int parent[kCapacity];
  int group_size[kCapacity] = {};
  for (size_t i = 0; i < count; ++i) {
    parent[i] = static_cast<int>(i);
    tag_visible[i] = false;
    aircraft_highlighted[i] = false;
  }
  for (size_t i = 0; i < count; ++i) {
    for (size_t j = i + 1; j < count; ++j) {
      if (!overlaps(bounds[i], bounds[j])) continue;
      const int ri = root(parent, static_cast<int>(i));
      const int rj = root(parent, static_cast<int>(j));
      if (ri != rj) parent[rj] = ri;
    }
  }
  for (size_t i = 0; i < count; ++i) {
    if (bounds[i].visible) ++group_size[root(parent, static_cast<int>(i))];
  }
  for (size_t i = 0; i < count; ++i) {
    if (!bounds[i].visible) continue;
    const int r = root(parent, static_cast<int>(i));
    if (group_size[r] == 1) {
      tag_visible[i] = true;
      continue;
    }
    int position = 0;
    for (size_t j = 0; j < i; ++j)
      if (bounds[j].visible && root(parent, static_cast<int>(j)) == r) ++position;
    if (position == static_cast<int>(phase % group_size[r])) {
      tag_visible[i] = true;
      aircraft_highlighted[i] = true;
    }
  }
}

}  // namespace core::tag_collision
