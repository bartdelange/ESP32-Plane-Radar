#include <unity.h>

#include <cmath>

#include "core/copernicus_terrain_data.h"
#include "core/land_water.h"
#include "core/settings.h"
#include "core/terrain.h"
#include "ui/radar_theme.h"

namespace ct = core::terrain;
void setUp() { ct::clear(); }
void tearDown() {}

void assertLand(double lat, double lon, bool expected) {
  bool land = !expected;
  TEST_ASSERT_TRUE(core::land_water::classify(lat, lon, &land));
  TEST_ASSERT_EQUAL(expected, land);
}

void test_copernicus_known_water_and_land(void) {
  assertLand(52.3508, 5.2647, true);   // reclaimed Flevoland
  assertLand(52.5185, 5.4714, true);   // reclaimed eastern Flevoland
  assertLand(52.50, 5.20, false);      // Markermeer
  assertLand(52.70, 5.40, false);      // IJsselmeer
  assertLand(53.3, 3.5, false);        // North Sea
  assertLand(50.85, 5.69, true);       // elevated inland Limburg
}

void test_negative_elevation_land_is_not_water(void) {
  int16_t elevation = 0;
  TEST_ASSERT_TRUE(ct::elevationAt(52.3508, 5.2647, &elevation));
  assertLand(52.3508, 5.2647, true);
  TEST_ASSERT_LESS_THAN_INT16(1, elevation);
}

void test_positive_inland_elevation(void) {
  int16_t elevation = 0;
  TEST_ASSERT_TRUE(ct::elevationAt(50.85, 5.69, &elevation));
  assertLand(50.85, 5.69, true);
  TEST_ASSERT_GREATER_THAN_INT16(20, elevation);
}

void test_wbm_and_dem_share_bounds(void) {
  using namespace data::copernicus_terrain;
  TEST_ASSERT_EQUAL_UINT16(512, kElevationWidth);
  TEST_ASSERT_EQUAL_UINT16(512, kElevationHeight);
  TEST_ASSERT_EQUAL_UINT16(2048, kWaterWidth);
  TEST_ASSERT_EQUAL_UINT16(2048, kWaterHeight);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, kWest);
  TEST_ASSERT_EQUAL_DOUBLE(55.8, kNorth);
}

void test_active_grid_does_not_change_water_geometry(void) {
  bool before = true, after = true;
  TEST_ASSERT_TRUE(core::land_water::classify(52.50, 5.20, &before));
  TEST_ASSERT_TRUE(ct::ensureGrid(52.3676, 4.9041, 1, 36.0f));
  TEST_ASSERT_TRUE(core::land_water::classify(52.50, 5.20, &after));
  TEST_ASSERT_FALSE(before);
  TEST_ASSERT_EQUAL(before, after);
}

void test_precomputed_raster_cell_matches_geographic_classification(void) {
  constexpr double points[][2] = {
      {52.3508, 5.2647}, {52.50, 5.20}, {52.70, 5.40}, {50.85, 5.69}};
  for (const auto& point : points) {
    bool geographic_land = false;
    bool raster_land = false;
    int row = -1;
    int col = -1;
    TEST_ASSERT_TRUE(
        core::land_water::classify(point[0], point[1], &geographic_land));
    TEST_ASSERT_TRUE(
        core::land_water::rasterCell(point[0], point[1], &row, &col));
    TEST_ASSERT_TRUE(
        core::land_water::classifyRasterCell(row, col, &raster_land));
    TEST_ASSERT_EQUAL(geographic_land, raster_land);
  }
}

void test_terrain_view_has_equal_physical_pixel_scale(void) {
  core::land_water::PixelView view;
  TEST_ASSERT_TRUE(core::land_water::makePixelView(
      52.5, 5.5, 20.0f, 240, &view));
  const double north_km = view.lat_degrees_per_pixel * core::geo::kKmPerDeg;
  const double east_km = view.lon_degrees_per_pixel *
                         core::geo::kKmPerDeg * std::cos(52.5 * M_PI / 180.0);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, north_km, east_km);
}

void test_terrain_center_and_overlay_projection_agree(void) {
  constexpr double kLat = 52.5;
  constexpr double kLon = 5.5;
  constexpr float kHalfSpanKm = 20.0f;
  core::land_water::PixelView terrain_view;
  TEST_ASSERT_TRUE(core::land_water::makePixelView(
      kLat, kLon, kHalfSpanKm, 240, &terrain_view));
  double center_lat = 0.0;
  double center_lon = 0.0;
  TEST_ASSERT_TRUE(core::land_water::pixelLatLon(
      terrain_view, 120, 120, &center_lat, &center_lon));
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, kLat, center_lat);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, kLon, center_lon);

  const core::geo::Viewport overlay_view = core::geo::makeViewport(
      kLat, kLon, 120, 120, 120, kHalfSpanKm);
  constexpr int kX = 176;
  constexpr int kY = 73;
  double point_lat = 0.0;
  double point_lon = 0.0;
  TEST_ASSERT_TRUE(core::land_water::pixelLatLon(
      terrain_view, kX, kY, &point_lat, &point_lon));
  const core::geo::Point overlay = core::geo::latLonToScreen(
      overlay_view, static_cast<float>(point_lat),
      static_cast<float>(point_lon));
  TEST_ASSERT_INT_WITHIN(1, kX, overlay.x);
  TEST_ASSERT_INT_WITHIN(1, kY, overlay.y);
}

void test_longitude_coverage_expands_by_inverse_cosine(void) {
  core::land_water::PixelView view;
  TEST_ASSERT_TRUE(core::land_water::makePixelView(
      52.5, 5.5, 20.0f, 240, &view));
  const double ratio = view.lon_degrees_per_pixel /
                       view.lat_degrees_per_pixel;
  const double expected = 1.0 / std::cos(52.5 * M_PI / 180.0);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, expected, ratio);
}

void test_fixed_radar_presets_remain_physical_kilometres(void) {
  constexpr uint16_t expected[] = {10, 20, 40, 80, 160};
  TEST_ASSERT_EQUAL_UINT(core::settings::kRangePresetCount,
                         sizeof(expected) / sizeof(expected[0]));
  for (size_t i = 0; i < core::settings::kRangePresetCount; ++i) {
    TEST_ASSERT_EQUAL_UINT16(expected[i],
                            core::settings::kRangePresets[i].ring3_km);
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, expected[i] * core::settings::kRing3ToOuterKm,
        core::settings::kRangePresets[i].outer_km);
  }
}

void test_corrected_pixel_keeps_wbm_and_elevation_geographically_aligned(void) {
  core::land_water::PixelView view;
  TEST_ASSERT_TRUE(core::land_water::makePixelView(
      52.5, 5.5, 20.0f, 240, &view));
  double lat = 0.0;
  double lon = 0.0;
  TEST_ASSERT_TRUE(core::land_water::pixelLatLon(view, 80, 160, &lat, &lon));
  bool pixel_land = false;
  bool coordinate_land = false;
  int16_t elevation = 0;
  TEST_ASSERT_TRUE(core::land_water::classifyPixel(view, 80, 160,
                                                   &pixel_land));
  TEST_ASSERT_TRUE(core::land_water::classify(lat, lon, &coordinate_land));
  TEST_ASSERT_TRUE(ct::elevationAt(lat, lon, &elevation));
  TEST_ASSERT_EQUAL(pixel_land, coordinate_land);
}

void test_fixed_range_relief_steps(void) {
  constexpr uint16_t expected[] = {5, 10, 20, 50, 100};
  TEST_ASSERT_EQUAL_UINT8(core::settings::kRangePresetCount,
                          sizeof(expected) / sizeof(expected[0]));
  for (uint8_t i = 0; i < core::settings::kRangePresetCount; ++i)
    TEST_ASSERT_EQUAL_UINT16(expected[i], ct::verticalStepForRangeIndex(i));
}

void test_local_relief_floors_support_negative_land(void) {
  int16_t floors[7];
  TEST_ASSERT_TRUE(ct::localReliefBandFloors(-2, 5, floors, 7));
  const int16_t expected[] = {-17, -12, -7, -2, 3, 8, 13};
  TEST_ASSERT_EQUAL_INT16_ARRAY(expected, floors, 7);
}

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8u) << 8) |
                               ((g & 0xFCu) << 3) | (b >> 3));
}

void test_terrain_palette_is_dark_and_rgb565_distinct(void) {
  constexpr uint16_t expected[] = {0x08A2, 0x08E2, 0x1123, 0x1163,
                                   0x1983, 0x29A3, 0x39C5};
  for (int i = 0; i < ui::radar::kTerrainBandCount; ++i) {
    const uint16_t actual = rgb565(ui::radar::kTerrainBandR[i],
                                   ui::radar::kTerrainBandG[i],
                                   ui::radar::kTerrainBandB[i]);
    TEST_ASSERT_EQUAL_HEX16(expected[i], actual);
    if (i > 0) TEST_ASSERT_NOT_EQUAL(expected[i - 1], actual);
  }
  // Water stays the existing blue-black background, below every land shade.
  TEST_ASSERT_EQUAL_HEX16(0x0043,
      rgb565(ui::radar::kBgR, ui::radar::kBgG, ui::radar::kBgB));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_copernicus_known_water_and_land);
  RUN_TEST(test_negative_elevation_land_is_not_water);
  RUN_TEST(test_positive_inland_elevation);
  RUN_TEST(test_wbm_and_dem_share_bounds);
  RUN_TEST(test_active_grid_does_not_change_water_geometry);
  RUN_TEST(test_precomputed_raster_cell_matches_geographic_classification);
  RUN_TEST(test_terrain_view_has_equal_physical_pixel_scale);
  RUN_TEST(test_terrain_center_and_overlay_projection_agree);
  RUN_TEST(test_longitude_coverage_expands_by_inverse_cosine);
  RUN_TEST(test_fixed_radar_presets_remain_physical_kilometres);
  RUN_TEST(test_corrected_pixel_keeps_wbm_and_elevation_geographically_aligned);
  RUN_TEST(test_fixed_range_relief_steps);
  RUN_TEST(test_local_relief_floors_support_negative_land);
  RUN_TEST(test_terrain_palette_is_dark_and_rgb565_distinct);
  return UNITY_END();
}
