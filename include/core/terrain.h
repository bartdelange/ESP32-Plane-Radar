#pragma once

/** Flash-backed regional terrain; only the 61x61 active view lives in RAM. */

#include <cstdint>
#include "config.h"
#include "core/geo.h"

namespace core::terrain {
constexpr int kGridSize = config::kTerrainGridSize;
constexpr int kGridPoints = kGridSize * kGridSize;

struct Grid {
  bool valid = false;
  double center_lat = 0.0;
  double center_lon = 0.0;
  float half_span_km = 0.0f;
  float lon_km_per_deg = core::geo::kKmPerDeg;
  int16_t land_median_m = 0;
  bool land_median_valid = false;
  int16_t elev_m[kGridPoints] = {};
};

void clear();
bool gridReady(double center_lat, double center_lon, uint8_t range_index);
bool ensureGrid(double center_lat, double center_lon, uint8_t range_index,
                float half_span_km);
const Grid* grid(uint8_t range_index);
bool isLand(const Grid& grid, int row, int col);
void pointLatLon(double center_lat, double center_lon, float half_span_km,
                 int row, int col, double* lat, double* lon);
bool elevationAt(double lat, double lon, int16_t* elevation_m);
int bandForElevation(int16_t elev_m, const int16_t* band_min_m,
                     int band_count);
int bandForSample(int16_t elev_m, bool is_land, const int16_t* band_min_m,
                  int band_count);
bool landMedianElevation(const Grid& grid, int16_t* median_m);
uint16_t verticalStepForRangeIndex(uint8_t range_index);
bool localReliefBandFloors(int16_t reference_m, uint16_t step_m,
                           int16_t* band_min_m, int band_count);
}  // namespace core::terrain
