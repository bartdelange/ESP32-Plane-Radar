#include "core/terrain.h"

#include <cmath>

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
  for (int row = 0; row < kGridSize; ++row) {
    for (int col = 0; col < kGridSize; ++col) {
      double sample_lat = 0.0, sample_lon = 0.0;
      pointLatLon(lat, lon, half_span_km, row, col, &sample_lat, &sample_lon);
      if (!elevationAt(sample_lat, sample_lon,
                       &s_grid.elev_m[row * kGridSize + col])) {
        clear();
        return false;
      }
    }
  }
  s_grid.valid = true;
  s_grid_range_index = range_index;
  return true;
}

const Grid* grid(uint8_t range_index) {
  return s_grid.valid && s_grid_range_index == range_index ? &s_grid : nullptr;
}

bool isLand(const Grid& grid, int row, int col) {
  if (row < 0 || row >= kGridSize || col < 0 || col >= kGridSize) return false;
  double lat = 0.0, lon = 0.0;
  pointLatLon(grid.center_lat, grid.center_lon, grid.half_span_km, row, col,
              &lat, &lon);
  bool land = false;
  return core::land_water::classify(lat, lon, &land) && land;
}

void pointLatLon(double center_lat, double center_lon, float half_span_km,
                 int row, int col, double* lat, double* lon) {
  const double span = static_cast<double>(half_span_km);
  *lat = center_lat + (1.0 - 2.0 * row / (kGridSize - 1)) * span /
                          core::geo::kKmPerDeg;
  *lon = center_lon + (2.0 * col / (kGridSize - 1) - 1.0) * span /
                          core::geo::kKmPerDeg;
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
  int land_count = 0;
  for (int row = 0; row < kGridSize; ++row)
    for (int col = 0; col < kGridSize; ++col)
      if (isLand(grid, row, col)) ++land_count;
  if (land_count == 0) return false;
  const int target = (land_count - 1) / 2;
  int32_t lo = INT16_MIN, hi = INT16_MAX;
  while (lo < hi) {
    const int32_t mid = lo + (hi - lo) / 2;
    int count = 0;
    for (int row = 0; row < kGridSize; ++row)
      for (int col = 0; col < kGridSize; ++col)
        if (isLand(grid, row, col) &&
            grid.elev_m[row * kGridSize + col] <= mid) ++count;
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
