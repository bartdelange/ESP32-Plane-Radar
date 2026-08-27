#include <cstring>

#include <unity.h>

#include "core/airport_find.h"
#include "core/land_mask.h"
#include "core/large_airports.h"
#include "core/region_pack.h"

namespace airports = data::large_airports;

namespace {

size_t airportIndex(const char* ident) {
  for (size_t i = 0; i < airports::kAirportCount; ++i) {
    if (strcmp(airports::kAirports[i].ident, ident) == 0) return i;
  }
  return airports::kAirportCount;
}

}  // namespace

void test_compiled_pack_metadata_matches_land_mask(void) {
  TEST_ASSERT_EQUAL_STRING("NL", data::region_pack::kCode);
  TEST_ASSERT_EQUAL_STRING("Netherlands", data::region_pack::kName);
  TEST_ASSERT_EQUAL_UINT32(1, data::region_pack::kCountryCount);
  TEST_ASSERT_EQUAL_STRING("NL", data::region_pack::kCountries[0]);
  TEST_ASSERT_EQUAL_UINT32(1, data::land_mask::kRegionCount);
  TEST_ASSERT_EQUAL_STRING(data::region_pack::kCode,
                           data::land_mask::kRegions[0].name);
  TEST_ASSERT_EQUAL_UINT16(data::region_pack::kMaskWidth,
                           data::land_mask::kRegions[0].width);
  TEST_ASSERT_EQUAL_UINT16(data::region_pack::kMaskHeight,
                           data::land_mask::kRegions[0].height);
}

void test_airport_data_has_base_and_regional_partitions(void) {
  TEST_ASSERT_TRUE(airports::kBaseAirportCount > 0);
  TEST_ASSERT_TRUE(airports::kRegionalAirportCount > 0);
  TEST_ASSERT_EQUAL_UINT32(airports::kAirportCount,
                           airports::kBaseAirportCount +
                               airports::kRegionalAirportCount);
  TEST_ASSERT_TRUE(airportIndex("KJFK") < airports::kBaseAirportCount);
  TEST_ASSERT_TRUE(airportIndex("EHLE") >= airports::kBaseAirportCount);
  TEST_ASSERT_TRUE(airportIndex("EHLE") < airports::kAirportCount);
}

void test_lookup_searches_both_sorted_partitions(void) {
  data::large_airports::Airport airport{};
  TEST_ASSERT_TRUE(core::airport::findAirport("kjfk", &airport));
  TEST_ASSERT_EQUAL_STRING("KJFK", airport.ident);
  TEST_ASSERT_TRUE(core::airport::findAirport("ehle", &airport));
  TEST_ASSERT_EQUAL_STRING("EHLE", airport.ident);
  TEST_ASSERT_FALSE(core::airport::findAirport("ZZZZ", &airport));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_compiled_pack_metadata_matches_land_mask);
  RUN_TEST(test_airport_data_has_base_and_regional_partitions);
  RUN_TEST(test_lookup_searches_both_sorted_partitions);
  return UNITY_END();
}
