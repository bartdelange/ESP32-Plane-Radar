#include "core/land_water.h"

#include <cstddef>

#include "core/geo.h"
#include "core/copernicus_terrain_data.h"

namespace core::land_water {
namespace {

bool covered(double lat, double lon) {
  using namespace data::copernicus_terrain;
  return lon >= kWest && lon <= kEast && lat >= kSouth && lat <= kNorth;
}

}  // namespace

bool classify(double lat, double lon, bool* is_land) {
  if (is_land == nullptr) return false;
  using namespace data::copernicus_terrain;
  if (!covered(lat, lon)) return false;
  int col = static_cast<int>((lon - kWest) * kWaterWidth / (kEast - kWest));
  int row = static_cast<int>((kNorth - lat) * kWaterHeight / (kNorth - kSouth));
  if (col >= kWaterWidth) col = kWaterWidth - 1;
  if (row >= kWaterHeight) row = kWaterHeight - 1;
  const size_t index = static_cast<size_t>(row) * kWaterWidth + col;
  *is_land = (kWaterBits[index >> 3] & (1u << (index & 7))) == 0;
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
  using namespace data::copernicus_terrain;
  return covered(center_lat, center_lon) && center_lon - delta >= kWest &&
         center_lon + delta <= kEast && center_lat - delta >= kSouth &&
         center_lat + delta <= kNorth;
}

}  // namespace core::land_water
