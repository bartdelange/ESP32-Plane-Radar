/**
 * Terrain elevation background: bilinearly upsample the cached elevation
 * grid (config::kTerrainGridSize per side) to the full 240x240 frame and
 * paint hypsometric green bands behind the radar grid.
 *
 * Every pixel of the frame passes through here, which on a core without an FPU
 * makes the arithmetic below the redraw's hot loop; see the note on kFracBits.
 */

#include "ui/terrain_overlay.h"

#include <algorithm>

#include "core/geo.h"
#include "core/land_water.h"
#include "core/settings.h"
#include "core/terrain.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::terrain {
namespace {

constexpr int kGrid = core::terrain::kGridSize;

/**
 * Interpolation weights are fixed-point, not float, and that is a performance
 * decision rather than a stylistic one: the ESP32-C3 core has no FPU, so every
 * float operation is a library call. Upsampling the grid touches 57,600 pixels
 * per frame, which in floats measured 209 ms of a 297 ms frame — the single
 * biggest cost in the whole redraw.
 *
 * 8 fractional bits put the weight error below 1/256 of a grid cell, i.e. far
 * under a pixel, and elevations are carried in whole metres: the bands are
 * hundreds of metres apart, so sub-metre precision would be measuring nothing.
 */
constexpr int kFracBits = 8;
constexpr int32_t kFracOne = 1 << kFracBits;

// Pixel -> grid-cell mapping is identical on both axes and for every frame
// (pixel 0 -> grid line 0, pixel kSize-1 -> grid line kGrid-1), so the cell
// index and fractional weight per pixel are computed once and reused.
int s_cell[radar::kSize];
int32_t s_frac[radar::kSize];  ///< 0..kFracOne
bool s_map_ready = false;
int16_t s_band_min_m[radar::kTerrainBandCount] = {};

void initPixelToGridMap() {
  if (s_map_ready) {
    return;
  }
  for (int i = 0; i < radar::kSize; ++i) {
    const int32_t position = static_cast<int32_t>(i) * (kGrid - 1) * kFracOne /
                             (radar::kSize - 1);
    int cell = static_cast<int>(position >> kFracBits);
    int32_t frac = position & (kFracOne - 1);
    // The last pixel lands exactly on the final grid line; keep the cell index
    // one short so cell+1 stays in range, with the weight fully on it.
    if (cell > kGrid - 2) {
      cell = kGrid - 2;
      frac = kFracOne;
    }
    s_cell[i] = cell;
    s_frac[i] = frac;
  }
  s_map_ready = true;
}

uint16_t blend565(uint16_t water, uint16_t land, uint8_t land_coverage) {
  if (land_coverage == 0) return water;
  if (land_coverage == 255) return land;
  const uint16_t water_weight = 255 - land_coverage;
  const uint16_t r = (((water >> 11) & 0x1F) * water_weight +
                      ((land >> 11) & 0x1F) * land_coverage + 127) /
                     255;
  const uint16_t g = (((water >> 5) & 0x3F) * water_weight +
                      ((land >> 5) & 0x3F) * land_coverage + 127) /
                     255;
  const uint16_t b = ((water & 0x1F) * water_weight +
                      (land & 0x1F) * land_coverage + 127) /
                     255;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

uint16_t colorAtPixel(const int32_t* row_elev, int x, double lat,
                      double lon) {
  uint8_t land_coverage = 0;
  if (!core::land_water::coverage(lat, lon, &land_coverage) ||
      land_coverage == 0) {
    return radar::kColorBackground;
  }
  const int c = s_cell[x];
  const int32_t west = row_elev[c];
  const int32_t elev_m =
      west + (((row_elev[c + 1] - west) * s_frac[x]) >> kFracBits);
  const int band = std::max(
      0, core::terrain::bandForElevation(static_cast<int16_t>(elev_m),
                                         s_band_min_m,
                                         radar::kTerrainBandCount));
  return blend565(radar::kColorBackground, radar::kColorTerrain[band],
                  land_coverage);
}

void drawScanline(lgfx::LGFXBase& gfx, const core::terrain::Grid& grid,
                  int y, double lat, double west_lon, double pixel_deg) {
  // The row weight is constant across the scanline: blend the two bracketing
  // grid rows into one kGrid-entry row up front so the per-pixel work is a
  // single horizontal lerp. Whole metres keep both lerps inside int32 for any
  // pair of terrarium values, so neither can overflow on real or absurd data.
  const int r = s_cell[y];
  const int32_t wy = s_frac[y];
  const int16_t* north = &grid.elev_m[r * kGrid];
  const int16_t* south = &grid.elev_m[(r + 1) * kGrid];
  int32_t row_elev[kGrid];
  for (int c = 0; c < kGrid; ++c) {
    const int32_t n = north[c];
    row_elev[c] = n + (((static_cast<int32_t>(south[c]) - n) * wy) >> kFracBits);
  }

  // Neighbouring pixels usually share a final blended color, so coalesce them
  // into horizontal runs. Land coverage comes directly from the higher-
  // resolution compiled regional mask, not the nearest elevation-grid sample.
  int run_start = 0;
  uint16_t run_color = colorAtPixel(row_elev, 0, lat, west_lon);
  for (int x = 1; x < radar::kSize; ++x) {
    const uint16_t color =
        colorAtPixel(row_elev, x, lat, west_lon + x * pixel_deg);
    if (color == run_color) {
      continue;
    }
    gfx.drawFastHLine(run_start, y, x - run_start, run_color);
    run_start = x;
    run_color = color;
  }
  gfx.drawFastHLine(run_start, y, radar::kSize - run_start, run_color);
}

}  // namespace

void drawTerrainBackground(lgfx::LGFXBase& gfx) {
  if (!radar::showTerrain()) {
    return;
  }
  const double lat = core::settings::lat();
  const double lon = core::settings::lon();
  const uint8_t range_index = core::settings::rangeIndex();
  if (!core::terrain::gridReady(lat, lon, range_index)) {
    return;
  }
  const core::terrain::Grid* grid = core::terrain::grid(range_index);
  if (grid == nullptr) {
    return;
  }
  int16_t reference_m = 0;
  if (!core::terrain::landMedianElevation(*grid, &reference_m) ||
      !core::terrain::localReliefBandFloors(
          reference_m,
          core::terrain::verticalStepForRangeKm(
              core::settings::rangeCurrent().ring3_km),
          s_band_min_m, radar::kTerrainBandCount)) {
    return;
  }

  initPixelToGridMap();
  const double half_span_deg =
      static_cast<double>(grid->half_span_km) / core::geo::kKmPerDeg;
  const double pixel_deg = 2.0 * half_span_deg / (radar::kSize - 1);
  const double north_lat = grid->center_lat + half_span_deg;
  const double west_lon = grid->center_lon - half_span_deg;
  // Grid row 0 is the north edge and column 0 the west edge, matching screen
  // y/x directly, so scanlines sample the grid without any axis flip.
  for (int y = 0; y < radar::kSize; ++y) {
    drawScanline(gfx, *grid, y, north_lat - y * pixel_deg, west_lon,
                 pixel_deg);
  }
}

}  // namespace ui::terrain
