#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/platform.h"

namespace pf = core::platform;

namespace {
constexpr char kPath[] = "/tmp/plane-radar-terrain-store-test.bin";
}

void test_native_store_round_trip_and_remove(void) {
  const uint8_t source[] = {0x54, 0x47, 0x52, 0x44, 1, 2, 3, 4};
  uint8_t loaded[sizeof(source)] = {};
  TEST_ASSERT_TRUE(pf::TerrainCacheStore::save(source, sizeof(source)));
  TEST_ASSERT_TRUE(pf::TerrainCacheStore::load(loaded, sizeof(loaded)));
  TEST_ASSERT_EQUAL_MEMORY(source, loaded, sizeof(source));
  pf::TerrainCacheStore::remove();
  TEST_ASSERT_FALSE(pf::TerrainCacheStore::load(loaded, sizeof(loaded)));
}

void setUp(void) {
  setenv("PLANE_RADAR_TERRAIN_CACHE", kPath, 1);
  std::remove(kPath);
  std::remove("/tmp/plane-radar-terrain-store-test.bin.tmp");
}

void tearDown(void) { pf::TerrainCacheStore::remove(); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_native_store_round_trip_and_remove);
  return UNITY_END();
}
