/**
 * Host unit tests for the pure logic in core::settings.
 *
 * These lock the behaviours that the device/native split is most likely to
 * silently change: the never-empty site list, the WiFiManager checkbox quirk,
 * and the ring-3 label rounding that the radar's scale label is drawn from.
 */

#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "core/platform.h"
#include "core/portal_params.h"
#include "core/settings.h"

namespace cs = core::settings;
// --- current location --------------------------------------------------------

/** Must run before anything else writes to the scratch store. */
void test_fresh_store_uses_the_default_current_location(void) {
  cs::init();
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, config::kDefaultRadarLat, cs::lat());
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, config::kDefaultRadarLon, cs::lon());
  TEST_ASSERT_TRUE(cs::useKm());
  TEST_ASSERT_TRUE(cs::showTerrain());
}

void test_current_location_is_persisted_and_validated(void) {
  TEST_ASSERT_TRUE(cs::saveLocationFromPortal("52.123456", "4.654321"));
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 52.123456, cs::lat());
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 4.654321, cs::lon());
  TEST_ASSERT_FALSE(cs::saveLocationFromPortal("91", "4"));
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 52.123456, cs::lat());
}

// --- portalCheckboxChecked ---------------------------------------------------

void test_checkbox_accepts_explicit_true_values(void) {
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("T"));
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("t"));
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("1"));
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("true"));
}

void test_checkbox_accepts_conventional_on(void) {
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("on"));
}

void test_checkbox_rejects_absent_and_unknown(void) {
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked(nullptr));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked(""));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked("TT"));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked("F"));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked("f"));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked("off"));
}

// --- formatRing3Label --------------------------------------------------------

void test_format_nm_is_exact(void) {
  char buf[12];
  const char* expected[] = {"5NM", "11NM", "22NM", "43NM", "86NM"};
  for (size_t i = 0; i < cs::rangeCount(); ++i) {
    cs::formatRing3Label(buf, sizeof(buf), cs::rangePreset(i).ring3_km,
                         /*use_km=*/false);
    TEST_ASSERT_EQUAL_STRING(expected[i], buf);
  }
}

void test_format_km_when_toggled(void) {
  char buf[12];
  const char* expected[] = {"10km", "20km", "40km", "80km", "160km"};
  for (size_t i = 0; i < cs::rangeCount(); ++i) {
    cs::formatRing3Label(buf, sizeof(buf), cs::rangePreset(i).ring3_km,
                         /*use_km=*/true);
    TEST_ASSERT_EQUAL_STRING(expected[i], buf);
  }
}

// --- range presets -----------------------------------------------------------

void test_outer_km_is_ring3_over_three_quarters(void) {
  for (size_t i = 0; i < cs::rangeCount(); ++i) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, cs::rangePreset(i).ring3_km * 4.0f / 3.0f,
                             cs::rangePreset(i).outer_km);
  }
}

void test_range_presets_are_fixed(void) {
  const uint16_t expected[] = {10, 20, 40, 80, 160};
  TEST_ASSERT_EQUAL_UINT(5, cs::rangeCount());
  for (size_t i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_UINT16(expected[i], cs::rangePreset(i).ring3_km);
  }
}

void test_legacy_configurable_ranges_are_retired_safely(void) {
  core::platform::KeyValueStore::putString(cs::kNsRadar, "rangePresets",
                                            "5,30,60");
  core::platform::KeyValueStore::putU8(cs::kNsRadar, "rangeIdx", 3);
  core::platform::KeyValueStore::remove(cs::kNsRadar, "fixedRangeV");
  cs::init();
  TEST_ASSERT_EQUAL_UINT(cs::kRangePresetCount, cs::rangeCount());
  TEST_ASSERT_EQUAL_UINT8(2, cs::rangeIndex());
  TEST_ASSERT_EQUAL_UINT16(40, cs::rangeCurrent().ring3_km);
  TEST_ASSERT_FALSE(
      core::platform::KeyValueStore::has(cs::kNsRadar, "rangePresets"));
  core::platform::KeyValueStore::remove(cs::kNsRadar, "rangeIdx");
  cs::init();
}

void test_range_presets_are_absent_from_portal(void) {
  for (size_t i = 0; i < core::portal::fieldCount(); ++i) {
    TEST_ASSERT_FALSE(strcmp("range_presets", core::portal::fields()[i].id) ==
                      0);
  }
  TEST_ASSERT_FALSE(core::portal::applyValueById("range_presets", "5,30,60"));
}

void test_rangeNext_cycles_and_wraps(void) {
  const uint8_t start = cs::rangeIndex();
  for (size_t i = 0; i < cs::rangeCount(); ++i) {
    cs::rangeNext();
  }
  TEST_ASSERT_EQUAL_UINT8(start, cs::rangeIndex());

  cs::rangeNext();
  TEST_ASSERT_EQUAL_UINT8((start + 1) % cs::rangeCount(), cs::rangeIndex());
}

// --- unitsReset asymmetry ----------------------------------------------------

void test_unitsReset_leaves_range_alone(void) {
  // Deliberate asymmetry: a Wi-Fi wipe resets units and the runway overlay but
  // keeps the user's chosen zoom.
  cs::saveKmFromPortal(nullptr);
  cs::saveRunwaysFromPortal(nullptr);
  cs::saveTerrainFromPortal(nullptr);
  cs::rangeNext();
  const uint8_t range_before = cs::rangeIndex();

  cs::unitsReset();

  TEST_ASSERT_TRUE(cs::useKm());
  TEST_ASSERT_TRUE(cs::showRunways());
  TEST_ASSERT_TRUE(cs::showTerrain());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(cs::AirlineDisplay::kNone),
                          static_cast<uint8_t>(cs::airlineDisplay()));
  TEST_ASSERT_EQUAL_UINT8(range_before, cs::rangeIndex());
}

void test_airline_display_accepts_only_known_selector_values(void) {
  cs::saveAirlineDisplayFromPortal("2");
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(cs::AirlineDisplay::kAbbrev),
                          static_cast<uint8_t>(cs::airlineDisplay()));
  cs::saveAirlineDisplayFromPortal("bad");
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(cs::AirlineDisplay::kNone),
                          static_cast<uint8_t>(cs::airlineDisplay()));
}

void test_route_display_defaults_persists_and_resets(void) {
  cs::unitsReset();
  cs::init();
  TEST_ASSERT_TRUE(cs::showRoutes());
  cs::saveRouteDisplayFromPortal("0");
  TEST_ASSERT_FALSE(cs::showRoutes());
  cs::init();
  TEST_ASSERT_FALSE(cs::showRoutes());
  cs::unitsReset();
  TEST_ASSERT_TRUE(cs::showRoutes());
  cs::init();
  TEST_ASSERT_TRUE(cs::showRoutes());
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  // Redirect the settings store to a scratch file before anything touches it,
  // so a test run never reads or clobbers the developer's real
  // ~/.plane-radar/settings.json. The path is resolved lazily on first use, so
  // this must happen before the first core::settings call.
  static const char* const kScratch = "/tmp/plane-radar-test-settings.json";
  setenv("PLANE_RADAR_SETTINGS", kScratch, 1);
  std::remove(kScratch);

  UNITY_BEGIN();

  RUN_TEST(test_fresh_store_uses_the_default_current_location);
  RUN_TEST(test_current_location_is_persisted_and_validated);

  RUN_TEST(test_checkbox_accepts_explicit_true_values);
  RUN_TEST(test_checkbox_accepts_conventional_on);
  RUN_TEST(test_checkbox_rejects_absent_and_unknown);

  RUN_TEST(test_format_nm_is_exact);
  RUN_TEST(test_format_km_when_toggled);

  RUN_TEST(test_outer_km_is_ring3_over_three_quarters);
  RUN_TEST(test_range_presets_are_fixed);
  RUN_TEST(test_legacy_configurable_ranges_are_retired_safely);
  RUN_TEST(test_range_presets_are_absent_from_portal);
  RUN_TEST(test_rangeNext_cycles_and_wraps);
  RUN_TEST(test_unitsReset_leaves_range_alone);
  RUN_TEST(test_airline_display_accepts_only_known_selector_values);
  RUN_TEST(test_route_display_defaults_persists_and_resets);


  return UNITY_END();
}
