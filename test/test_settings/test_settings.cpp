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
#include "core/settings.h"

namespace cs = core::settings;
// --- current location --------------------------------------------------------

/** Must run before anything else writes to the scratch store. */
void test_fresh_store_uses_the_default_current_location(void) {
  cs::init();
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, config::kDefaultRadarLat, cs::lat());
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, config::kDefaultRadarLon, cs::lon());
}

void test_current_location_is_persisted_and_validated(void) {
  TEST_ASSERT_TRUE(cs::saveLocationFromPortal("52.123456", "4.654321"));
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 52.123456, cs::lat());
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 4.654321, cs::lon());
  TEST_ASSERT_FALSE(cs::saveLocationFromPortal("91", "4"));
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 52.123456, cs::lat());
}

// --- portalCheckboxChecked ---------------------------------------------------

void test_checkbox_single_TF_means_submitted(void) {
  // WiFiManager submits the value= attribute, which the portal prefills with
  // "T" regardless of state; the browser only sends the field at all when the
  // box is ticked. So a bare T or F both mean "checked".
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("T"));
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("t"));
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("F"));
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("f"));
}

void test_checkbox_accepts_conventional_on(void) {
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("on"));
}

void test_checkbox_rejects_absent_and_unknown(void) {
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked(nullptr));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked(""));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked("TT"));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked("off"));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked("true"));
}

// --- formatRing3Label --------------------------------------------------------

void test_format_nm_is_the_default_and_is_exact(void) {
  char buf[12];
  const char* expected[] = {"5NM", "11NM", "22NM", "43NM", "65NM"};
  for (size_t i = 0; i < cs::rangeCount(); ++i) {
    cs::formatRing3Label(buf, sizeof(buf), cs::rangePreset(i).ring3_km,
                         /*use_km=*/false);
    TEST_ASSERT_EQUAL_STRING(expected[i], buf);
  }
}

void test_format_km_when_toggled(void) {
  char buf[12];
  const char* expected[] = {"10km", "20km", "40km", "80km", "120km"};
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

void test_default_presets_are_metric_fork_defaults(void) {
  const uint16_t expected[] = {10, 20, 40, 80, 120};
  TEST_ASSERT_EQUAL_UINT(5, cs::rangeCount());
  for (size_t i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_UINT16(expected[i], cs::rangePreset(i).ring3_km);
  }
}

void test_range_parser_accepts_strict_ascending_km(void) {
  uint16_t values[cs::kMaxRangePresets] = {};
  size_t count = 0;
  TEST_ASSERT_TRUE(cs::parseRangePresets("5, 15,30, 200", values,
                                         cs::kMaxRangePresets, &count));
  TEST_ASSERT_EQUAL_UINT(4, count);
  TEST_ASSERT_EQUAL_UINT16(5, values[0]);
  TEST_ASSERT_EQUAL_UINT16(200, values[3]);
}

void test_range_parser_rejects_malformed_lists(void) {
  uint16_t values[cs::kMaxRangePresets] = {};
  size_t count = 0;
  const char* invalid[] = {"", "0,10", "10,10", "20,10", "10,,20",
                           "10,wat", "10,501",
                           "10,20,30,40,50,60,70,80,90"};
  for (const char* text : invalid) {
    TEST_ASSERT_FALSE(cs::parseRangePresets(text, values,
                                            cs::kMaxRangePresets, &count));
  }
}

void test_configured_presets_persist_with_active_range(void) {
  TEST_ASSERT_TRUE(cs::saveRangePresetsFromPortal("15,30,60"));
  cs::rangeNext();
  TEST_ASSERT_EQUAL_UINT16(60, cs::rangeCurrent().ring3_km);
  cs::init();
  TEST_ASSERT_EQUAL_UINT(3, cs::rangeCount());
  TEST_ASSERT_EQUAL_UINT16(60, cs::rangeCurrent().ring3_km);
  TEST_ASSERT_TRUE(cs::saveRangePresetsFromPortal("10,20,40,80,120"));
}

void test_invalid_persisted_ranges_fall_back_to_defaults(void) {
  core::platform::KeyValueStore::putString(cs::kNsRadar, "rangePresets",
                                            "40,20");
  core::platform::KeyValueStore::putU8(cs::kNsRadar, "rangeIdx", 7);
  cs::init();
  TEST_ASSERT_EQUAL_UINT(cs::kDefaultRangeCount, cs::rangeCount());
  TEST_ASSERT_EQUAL_UINT8(1, cs::rangeIndex());
  TEST_ASSERT_EQUAL_UINT16(20, cs::rangeCurrent().ring3_km);
  core::platform::KeyValueStore::remove(cs::kNsRadar, "rangePresets");
  core::platform::KeyValueStore::remove(cs::kNsRadar, "rangeIdx");
  cs::init();
}

void test_list_change_uses_first_range_not_smaller_than_old_value(void) {
  while (cs::rangeCurrent().ring3_km != 40) cs::rangeNext();
  TEST_ASSERT_TRUE(cs::saveRangePresetsFromPortal("10,30,50"));
  TEST_ASSERT_EQUAL_UINT16(50, cs::rangeCurrent().ring3_km);
  TEST_ASSERT_TRUE(cs::saveRangePresetsFromPortal("10,20,40,80,120"));
}

void test_range_configuration_does_not_move_current_location(void) {
  const double old_lat = cs::lat();
  const double old_lon = cs::lon();
  TEST_ASSERT_TRUE(cs::saveRangePresetsFromPortal("10,25,100"));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, old_lat, cs::lat());
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, old_lon, cs::lon());
  TEST_ASSERT_TRUE(cs::saveRangePresetsFromPortal("10,20,40,80,120"));
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
  cs::saveKmFromPortal("T");
  cs::saveRunwaysFromPortal(nullptr);
  cs::rangeNext();
  const uint8_t range_before = cs::rangeIndex();

  cs::unitsReset();

  TEST_ASSERT_FALSE(cs::useKm());
  TEST_ASSERT_TRUE(cs::showRunways());
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

  RUN_TEST(test_checkbox_single_TF_means_submitted);
  RUN_TEST(test_checkbox_accepts_conventional_on);
  RUN_TEST(test_checkbox_rejects_absent_and_unknown);

  RUN_TEST(test_format_nm_is_the_default_and_is_exact);
  RUN_TEST(test_format_km_when_toggled);

  RUN_TEST(test_outer_km_is_ring3_over_three_quarters);
  RUN_TEST(test_default_presets_are_metric_fork_defaults);
  RUN_TEST(test_range_parser_accepts_strict_ascending_km);
  RUN_TEST(test_range_parser_rejects_malformed_lists);
  RUN_TEST(test_configured_presets_persist_with_active_range);
  RUN_TEST(test_invalid_persisted_ranges_fall_back_to_defaults);
  RUN_TEST(test_list_change_uses_first_range_not_smaller_than_old_value);
  RUN_TEST(test_range_configuration_does_not_move_current_location);
  RUN_TEST(test_rangeNext_cycles_and_wraps);
  RUN_TEST(test_unitsReset_leaves_range_alone);
  RUN_TEST(test_airline_display_accepts_only_known_selector_values);


  return UNITY_END();
}
