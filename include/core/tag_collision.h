#pragma once

#include <cstddef>

namespace core::tag_collision {

struct Bounds {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  bool visible = false;
};

/**
 * Select visible tags for one render phase. Highlight is true only for the
 * selected member of an overlapping group; independent tags stay low-noise.
 */
void select(const Bounds* bounds, size_t count, unsigned long phase,
            bool* tag_visible, bool* aircraft_highlighted);

}  // namespace core::tag_collision
