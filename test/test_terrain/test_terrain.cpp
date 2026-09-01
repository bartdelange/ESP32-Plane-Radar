#include <unity.h>

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
  RUN_TEST(test_fixed_range_relief_steps);
  RUN_TEST(test_local_relief_floors_support_negative_land);
  RUN_TEST(test_terrain_palette_is_dark_and_rgb565_distinct);
  return UNITY_END();
}
