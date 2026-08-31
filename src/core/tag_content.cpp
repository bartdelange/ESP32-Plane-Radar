#include "core/tag_content.h"

namespace core::tag_content {

bool showRouteLine(bool compact, bool route_setting, const char* origin,
                   const char* destination) {
  const bool has_origin = origin != nullptr && origin[0] != '\0';
  const bool has_destination = destination != nullptr && destination[0] != '\0';
  return !compact && route_setting && (has_origin || has_destination);
}

bool useCompactMode(size_t inside_ring_tag_count) {
  return inside_ring_tag_count > kTagCompactAboveCount;
}

bool showAltitudeLine(const char* altitude) {
  return altitude != nullptr && altitude[0] != '\0';
}

int blockHeight(int line_height, int line_count) {
  return line_height > 0 && line_count > 0 ? line_height * line_count : 0;
}

}  // namespace core::tag_content
