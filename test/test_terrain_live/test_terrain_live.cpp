/**
 * Live smoke test for the terrain layer against the REAL tile bucket.
 *
 * NEEDS THE INTERNET and is opt-in: `make test-live` only, never `make test`.
 * It runs in env:native_test_live with the real http_curl.cpp transport, the
 * real clock, and — unlike every other host test — the REAL PNG decoder
 * (platform/png_decode.cpp). Nothing is faked, so this is the full path the
 * firmware takes: build a URL, download a terrain-RGB tile off
 * s3.amazonaws.com/elevation-tiles-prod, decode it streaming, and resample it
 * into the elevation grid.
 *
 * The native test injects fixed scratch for determinism; production allocates
 * and releases the same bounded working set after TLS closes.
 *
 * Everything on our side of the wire is covered deterministically in
 * test_terrain_fetch. What only this suite can prove is that the bucket accepts
 * our URLs, that its PNGs decode with the decoder we ship, and that the numbers
 * that come out are real terrain. Hence the deliberately generous elevation
 * bands: the claim under test is "this is the right mountain", not a DEM value.
 *
 * One Netherlands view, so a handful of tiles at most — it finishes quickly.
 */

#include <unity.h>

#include <cstddef>
#include <cstring>

#include "core/platform.h"
#include "core/terrain.h"
#include "platform/png_decode.h"

namespace ct = core::terrain;
namespace pf = core::platform;

namespace {

/** Almere, dry reclaimed land below mean sea level. */
constexpr double kAlmereLat = 52.3508;
constexpr double kAlmereLon = 5.2647;

/** ~40 km to the screen edge, which resolves to a zoom-8 view here. */
constexpr float kSpan = 40.0f;
constexpr uint8_t kRange = 1;

/** kTerrainGridSize is odd, so one sample sits exactly on the centre. */
constexpr int kCenterIndex =
    (ct::kGridSize / 2) * ct::kGridSize + ct::kGridSize / 2;

/**
 * Download the whole grid the way the main loop does. Generous deadline: it
 * has to survive a slow tile, and a tile that fails three times parks the
 * download behind kTerrainRetryIntervalMs.
 */
bool downloadGrid(double lat, double lon) {
  const unsigned long deadline_ms = pf::nowMs() + 120000;
  while (pf::nowMs() < deadline_ms) {
    if (ct::ensureGrid(lat, lon, kRange, kSpan)) {
      return true;
    }
    pf::sleepMs(50);
  }
  return false;
}

/** Fixed test workspace; production uses temporary owned scratch. */
uint8_t s_scratch[platform_png::kScratchBytes];

uint8_t* lendScratch(size_t need_bytes) {
  return need_bytes <= sizeof(s_scratch) ? s_scratch : nullptr;
}

}  // namespace

// --- The URL we ask the bucket for -------------------------------------------

void test_live_tile_url_shape(void) {
  ct::TileId tile;
  tile.z = 8;
  tile.x = 138;
  tile.y = 90;
  char url[160];
  ct::buildTileUrl(url, sizeof(url), tile);

  // The bucket's own layout, z/x/y in that order — the two grid tests below
  // would only tell us "no data" if this were wrong. The URL scheme is
  // deliberately not pinned here; that is config.h's call, not the bucket's.
  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(url, "s3.amazonaws.com/elevation-tiles-prod/terrarium/8/138/90.png"),
      url);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(url, "://"), url);
}

// --- Real tiles, real decoder, plausible terrain -----------------------------

void test_live_netherlands_grid_has_polder_and_water(void) {
  TEST_ASSERT_TRUE_MESSAGE(downloadGrid(kAlmereLat, kAlmereLon),
                           "grid did not complete — offline, or the bucket "
                           "rejected our URL?");
  TEST_ASSERT_TRUE(ct::gridReady(kAlmereLat, kAlmereLon, kRange));

  const ct::Grid* g = ct::grid(kRange);
  TEST_ASSERT_NOT_NULL(g);

  const int16_t center_m = g->elev_m[kCenterIndex];
  TEST_ASSERT_GREATER_THAN_INT16(-20, center_m);
  TEST_ASSERT_LESS_THAN_INT16(20, center_m);
  TEST_ASSERT_TRUE(ct::isLand(*g, ct::kGridSize / 2, ct::kGridSize / 2));

  bool saw_water = false;
  for (int row = 0; row < ct::kGridSize; ++row) {
    for (int col = 0; col < ct::kGridSize; ++col) {
      saw_water = saw_water || !ct::isLand(*g, row, col);
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(saw_water, "Almere view must include mapped water");
}

void setUp(void) {
  ct::clear();
  ct::setPngDecoder(platform_png::decode);
  platform_png::setScratch(lendScratch);
}

void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_live_tile_url_shape);
  RUN_TEST(test_live_netherlands_grid_has_polder_and_water);

  return UNITY_END();
}
