#include "core/land_water.h"

#include <cstddef>

#include "core/geo.h"
#include "core/land_mask.h"

namespace core::land_water {
namespace {

const data::land_mask::Region* regionAt(double lat, double lon) {
  for (size_t i = 0; i < data::land_mask::kRegionCount; ++i) {
    const auto& region = data::land_mask::kRegions[i];
    if (lon >= region.west && lon <= region.east && lat >= region.south &&
        lat <= region.north) {
      return &region;
    }
  }
  return nullptr;
}

}  // namespace

bool classify(double lat, double lon, bool* is_land) {
  if (is_land == nullptr) return false;
  const auto* region = regionAt(lat, lon);
  if (region == nullptr) return false;
  int col = static_cast<int>((lon - region->west) * region->width /
                             (region->east - region->west));
  int row = static_cast<int>((region->north - lat) * region->height /
                             (region->north - region->south));
  if (col >= region->width) col = region->width - 1;
  if (row >= region->height) row = region->height - 1;
  const size_t index = static_cast<size_t>(row) * region->width + col;
  *is_land = (region->bits[index >> 3] & (1u << (index & 7))) != 0;
  return true;
}

bool makePixelView(double center_lat, double center_lon, float half_span_km,
                   int size, PixelView* view) {
  if (view == nullptr || size < 2 || half_span_km <= 0.0f) return false;
  const double half_span_deg =
      static_cast<double>(half_span_km) / core::geo::kKmPerDeg;
  view->north_lat = center_lat + half_span_deg;
  view->west_lon = center_lon - half_span_deg;
  view->degrees_per_pixel = 2.0 * half_span_deg / (size - 1);
  view->size = size;
  return true;
}

bool classifyPixel(const PixelView& view, int x, int y, bool* is_land) {
  if (x < 0 || x >= view.size || y < 0 || y >= view.size) return false;
  return classify(view.north_lat - y * view.degrees_per_pixel,
                  view.west_lon + x * view.degrees_per_pixel, is_land);
}

bool coversView(double center_lat, double center_lon, float half_span_km) {
  const double delta = half_span_km / core::geo::kKmPerDeg;
  const auto* region = regionAt(center_lat, center_lon);
  return region != nullptr && center_lon - delta >= region->west &&
         center_lon + delta <= region->east &&
         center_lat - delta >= region->south &&
         center_lat + delta <= region->north;
}

}  // namespace core::land_water
