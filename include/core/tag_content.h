#pragma once

namespace core::tag_content {

bool showRouteLine(bool compact, bool route_setting, const char* origin,
                   const char* destination);
int blockHeight(int line_height, int line_count);

}  // namespace core::tag_content
