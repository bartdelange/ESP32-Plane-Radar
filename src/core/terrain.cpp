#include "core/terrain.h"

#include <cmath>
#include <cstddef>

#include "core/copernicus_terrain_data.h"
#include "core/geo.h"
#include "core/land_water.h"
#include "core/settings.h"

namespace core::terrain {
namespace {
constexpr double kCenterEpsilonDeg = 1e-7;
constexpr uint8_t kNoRange = 0xFF;
Grid s_grid;
static_assert(sizeof(Grid) > 7000, "Grid must remain static, never stack-local");
uint8_t s_grid_range_index = kNoRange;

bool coordinateToRaster(double lat, double lon, double* row, double* col) {
  using namespace data::copernicus_terrain;
  if (row == nullptr || col == nullptr || lon < kWest || lon > kEast ||
      lat < kSouth || lat > kNorth) return false;
  *col = (lon - kWest) * (kElevationWidth - 1) / (kEast - kWest);
  *row = (kNorth - lat) * (kElevationHeight - 1) / (kNorth - kSouth);
  return true;
}
}  // namespace

void clear() {
  s_grid.valid = false;
  s_grid.land_median_valid = false;
  s_grid_range_index = kNoRange;
}

bool gridReady(double lat, double lon, uint8_t range_index) {
  return s_grid.valid && s_grid_range_index == range_index &&
         std::fabs(s_grid.center_lat - lat) < kCenterEpsilonDeg &&
         std::fabs(s_grid.center_lon - lon) < kCenterEpsilonDeg;
}

bool ensureGrid(double lat, double lon, uint8_t range_index,
                float half_span_km) {
  if (gridReady(lat, lon, range_index)) return true;
  if (range_index >= core::settings::kRangePresetCount ||
      !core::land_water::coversView(lat, lon, half_span_km)) {
    clear();
    return false;
  }
  // Grid is ~7.5 KiB at 61x61. Populate the single static slot directly;
  // never put a terrain-sized temporary on the ESP32-C3 loopTask stack.
  clear();
  s_grid.center_lat = lat;
  s_grid.center_lon = lon;
  s_grid.half_span_km = half_span_km;
  const core::geo::Viewport terrain_view = core::geo::makeViewport(
      lat, lon, (kGridSize - 1) / 2, (kGridSize - 1) / 2,
      (kGridSize - 1) / 2, half_span_km);
  s_grid.lon_km_per_deg = terrain_view.lon_km_per_deg;
  for (int row = 0; row < kGridSize; ++row) {
    for (int col = 0; col < kGridSize; ++col) {
      const core::geo::Coordinate sample = core::geo::screenToLatLon(
          terrain_view, static_cast<float>(col), static_cast<float>(row));
      if (!elevationAt(sample.lat, sample.lon,
                       &s_grid.elev_m[row * kGridSize + col])) {
        clear();
        return false;
      }
    }
  }
  int16_t median_m = 0;
  if (!landMedianElevation(s_grid, &median_m)) {
    clear();
    return false;
  }
  s_grid.land_median_m = median_m;
  s_grid.land_median_valid = true;
  s_grid.valid = true;
  s_grid_range_index = range_index;
  return true;
}

const Grid* grid(uint8_t range_index) {
  return s_grid.valid && s_grid_range_index == range_index ? &s_grid : nullptr;
}

bool isLand(const Grid& grid, int row, int col) {
  if (row < 0 || row >= kGridSize || col < 0 || col >= kGridSize) return false;
  const int center = (kGridSize - 1) / 2;
  const double km_per_cell = static_cast<double>(grid.half_span_km) / center;
  const double lat = grid.center_lat -
      (row - center) * km_per_cell / core::geo::kKmPerDeg;
  const double lon = grid.center_lon +
      (col - center) * km_per_cell / grid.lon_km_per_deg;
  bool land = false;
  return core::land_water::classify(lat, lon, &land) && land;
}

void pointLatLon(double center_lat, double center_lon, float half_span_km,
                 int row, int col, double* lat, double* lon) {
  const int center = (kGridSize - 1) / 2;
  const core::geo::Viewport view = core::geo::makeViewport(
      center_lat, center_lon, center, center, center, half_span_km);
  const core::geo::Coordinate coordinate = core::geo::screenToLatLon(
      view, static_cast<float>(col), static_cast<float>(row));
  *lat = coordinate.lat;
  *lon = coordinate.lon;
}

bool elevationAt(double lat, double lon, int16_t* elevation_m) {
  if (elevation_m == nullptr) return false;
  double source_row = 0.0, source_col = 0.0;
  if (!coordinateToRaster(lat, lon, &source_row, &source_col)) return false;
  using namespace data::copernicus_terrain;
  int row = static_cast<int>(source_row), col = static_cast<int>(source_col);
  double fy = source_row - row, fx = source_col - col;
  if (row >= kElevationHeight - 1) { row = kElevationHeight - 2; fy = 1.0; }
  if (col >= kElevationWidth - 1) { col = kElevationWidth - 2; fx = 1.0; }
  const int32_t nw = kElevation[row * kElevationWidth + col];
  const int32_t ne = kElevation[row * kElevationWidth + col + 1];
  const int32_t sw = kElevation[(row + 1) * kElevationWidth + col];
  const int32_t se = kElevation[(row + 1) * kElevationWidth + col + 1];
  const double north = nw + (ne - nw) * fx;
  const double south = sw + (se - sw) * fx;
  *elevation_m = static_cast<int16_t>(std::lround(north + (south - north) * fy));
  return true;
}

int bandForElevation(int16_t elevation, const int16_t* floors, int count) {
  int band = -1;
  for (int i = 0; i < count; ++i) {
    if (elevation >= floors[i]) band = i;
    else break;
  }
  return band;
}

int bandForSample(int16_t elevation, bool land, const int16_t* floors,
                  int count) {
  return land ? bandForElevation(elevation, floors, count) : -1;
}

bool landMedianElevation(const Grid& grid, int16_t* median_m) {
  if (median_m == nullptr) return false;
  if (grid.land_median_valid) {
    *median_m = grid.land_median_m;
    return true;
  }

  // One bit per 61x61 sample is only 466 bytes and lives briefly on the stack
  // while a new active grid is initialized (before framebuffer/TLS work). It
  // avoids repeating expensive double-precision geographic classification in
  // every iteration of the exact median selection.
  constexpr size_t kLandBytes = (kGridPoints + 7) / 8;
  static_assert(kLandBytes < 512, "median scratch must remain small");
  uint8_t land_bits[kLandBytes] = {};
  int land_count = 0;
  for (int row = 0; row < kGridSize; ++row)
    for (int col = 0; col < kGridSize; ++col)
      if (isLand(grid, row, col)) {
        const int index = row * kGridSize + col;
        land_bits[index >> 3] |= static_cast<uint8_t>(1u << (index & 7));
        ++land_count;
      }
  if (land_count == 0) return false;
  const int target = (land_count - 1) / 2;
  int32_t lo = INT16_MIN, hi = INT16_MAX;
  while (lo < hi) {
    const int32_t mid = lo + (hi - lo) / 2;
    int count = 0;
    for (int index = 0; index < kGridPoints; ++index)
      if ((land_bits[index >> 3] & (1u << (index & 7))) != 0 &&
          grid.elev_m[index] <= mid)
        ++count;
    if (count > target) hi = mid;
    else lo = mid + 1;
  }
  *median_m = static_cast<int16_t>(lo);
  return true;
}

uint16_t verticalStepForRangeIndex(uint8_t range_index) {
  constexpr uint16_t kSteps[] = {5, 10, 20, 50, 100};
  static_assert(sizeof(kSteps) / sizeof(kSteps[0]) ==
                core::settings::kRangePresetCount);
  return kSteps[range_index < core::settings::kRangePresetCount
                    ? range_index : core::settings::kRangePresetCount - 1];
}

bool localReliefBandFloors(int16_t reference, uint16_t step, int16_t* floors,
                           int count) {
  if (floors == nullptr || count <= 0 || (count & 1) == 0 || step == 0)
    return false;
  const int centre = count / 2;
  for (int i = 0; i < count; ++i) {
    const int32_t value = static_cast<int32_t>(reference) + (i - centre) * step;
    floors[i] = static_cast<int16_t>(value < INT16_MIN ? INT16_MIN :
                                    value > INT16_MAX ? INT16_MAX : value);
  }
  return true;
}
}  // namespace core::terrain
