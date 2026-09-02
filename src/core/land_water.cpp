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
  int row = 0;
  int col = 0;
  return rasterCell(lat, lon, &row, &col) &&
         classifyRasterCell(row, col, is_land);
}

bool rasterCell(double lat, double lon, int* row, int* col) {
  if (row == nullptr || col == nullptr || !covered(lat, lon)) return false;
  using namespace data::copernicus_terrain;
  int mapped_col =
      static_cast<int>((lon - kWest) * kWaterWidth / (kEast - kWest));
  int mapped_row =
      static_cast<int>((kNorth - lat) * kWaterHeight / (kNorth - kSouth));
  if (mapped_col >= kWaterWidth) mapped_col = kWaterWidth - 1;
  if (mapped_row >= kWaterHeight) mapped_row = kWaterHeight - 1;
  *col = mapped_col;
  *row = mapped_row;
  return true;
}

bool classifyRasterCell(int row, int col, bool* is_land) {
  if (is_land == nullptr) return false;
  using namespace data::copernicus_terrain;
  if (row < 0 || row >= kWaterHeight || col < 0 || col >= kWaterWidth) {
    return false;
  }
  const size_t index = static_cast<size_t>(row) * kWaterWidth + col;
  *is_land = (kWaterBits[index >> 3] & (1u << (index & 7))) == 0;
  return true;
}

bool makePixelView(double center_lat, double center_lon, float half_span_km,
                   int size, PixelView* view) {
  if (view == nullptr || size < 2 || half_span_km <= 0.0f) return false;
  const int center = size / 2;
  const core::geo::Viewport geo_view = core::geo::makeViewport(
      center_lat, center_lon, center, center, center, half_span_km);
  const core::geo::Coordinate northwest =
      core::geo::screenToLatLon(geo_view, 0.0f, 0.0f);
  view->north_lat = northwest.lat;
  view->west_lon = northwest.lon;
  view->lat_degrees_per_pixel =
      static_cast<double>(half_span_km) / center / core::geo::kKmPerDeg;
  view->lon_degrees_per_pixel =
      static_cast<double>(half_span_km) / center / geo_view.lon_km_per_deg;
  view->center_x = center;
  view->center_y = center;
  view->size = size;
  return true;
}

bool pixelLatLon(const PixelView& view, int x, int y, double* lat, double* lon) {
  if (lat == nullptr || lon == nullptr || x < 0 || x >= view.size ||
      y < 0 || y >= view.size) return false;
  *lat = view.north_lat - y * view.lat_degrees_per_pixel;
  *lon = view.west_lon + x * view.lon_degrees_per_pixel;
  return true;
}

bool classifyPixel(const PixelView& view, int x, int y, bool* is_land) {
  double lat = 0.0;
  double lon = 0.0;
  return pixelLatLon(view, x, y, &lat, &lon) && classify(lat, lon, is_land);
}

bool coversView(double center_lat, double center_lon, float half_span_km) {
  const core::geo::Viewport view = core::geo::makeViewport(
      center_lat, center_lon, 0, 0, 1, half_span_km);
  const double lat_delta = half_span_km / core::geo::kKmPerDeg;
  const double lon_delta = half_span_km / view.lon_km_per_deg;
  using namespace data::copernicus_terrain;
  return covered(center_lat, center_lon) && center_lon - lon_delta >= kWest &&
         center_lon + lon_delta <= kEast && center_lat - lat_delta >= kSouth &&
         center_lat + lat_delta <= kNorth;
}

}  // namespace core::land_water
