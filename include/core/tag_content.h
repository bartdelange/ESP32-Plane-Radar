#pragma once

#include <cstddef>

namespace core::tag_content {

/** Rich tags are retained through six aircraft tagged inside the outer ring. */
constexpr size_t kTagCompactAboveCount = 6;

bool showRouteLine(bool compact, bool route_setting, const char* origin,
                   const char* destination);
/** Compact mode begins with the seventh aircraft tagged inside the ring. */
bool useCompactMode(size_t inside_ring_tag_count);
/** Altitude remains present in both rich and compact tags when available. */
bool showAltitudeLine(const char* altitude);
int blockHeight(int line_height, int line_count);

}  // namespace core::tag_content
