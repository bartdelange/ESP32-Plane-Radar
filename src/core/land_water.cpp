#include "core/land_water.h"

#include <cmath>
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

bool maskBit(const data::land_mask::Region& region, int row, int col) {
  if (row < 0) row = 0;
  if (col < 0) col = 0;
  if (row >= region.height) row = region.height - 1;
  if (col >= region.width) col = region.width - 1;
  const size_t index = static_cast<size_t>(row) * region.width + col;
  return (region.bits[index >> 3] & (1u << (index & 7))) != 0;
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

bool coverage(double lat, double lon, uint8_t* land_coverage) {
  if (land_coverage == nullptr) return false;
  const auto* region = regionAt(lat, lon);
  if (region == nullptr) return false;

  // Coordinates address mask-cell centres. Interpolating the four surrounding
  // binary cells removes the visible nearest-neighbour stair steps without
  // allocating a screen-sized coverage bitmap.
  const double x = (lon - region->west) * region->width /
                       (region->east - region->west) -
                   0.5;
  const double y = (region->north - lat) * region->height /
                       (region->north - region->south) -
                   0.5;
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const double fx = x - x0;
  const double fy = y - y0;
  const double north = maskBit(*region, y0, x0) * (1.0 - fx) +
                       maskBit(*region, y0, x0 + 1) * fx;
  const double south = maskBit(*region, y0 + 1, x0) * (1.0 - fx) +
                       maskBit(*region, y0 + 1, x0 + 1) * fx;
  const double value = north * (1.0 - fy) + south * fy;
  *land_coverage = static_cast<uint8_t>(value * 255.0 + 0.5);
  return true;
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
