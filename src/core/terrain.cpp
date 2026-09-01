#include "core/terrain.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/geo.h"
#include "core/land_water.h"
#include "core/settings.h"

namespace core::terrain {

namespace {

/**
 * Two centres closer than this are the same view. ~1e-7 deg is about 1 cm on
 * the ground — far below a tile pixel — so it only absorbs float noise, never
 * a real relocation.
 */
constexpr double kCenterEpsilonDeg = 1e-7;

/** Web Mercator is undefined at the poles; this is its conventional cut-off. */
constexpr double kMercatorMaxLat = 85.05112878;

constexpr double kPi = 3.14159265358979323846;

/** Stands for "no range preset" in the cache and retry-gate keys below. */
constexpr uint8_t kNoRange = 0xFF;
constexpr uint32_t kCacheMagic = 0x54475244u;  // "TGRD"

#pragma pack(push, 1)
struct PersistedGrid {
  uint32_t magic;
  uint16_t version;
  uint16_t grid_size;
  double center_lat;
  double center_lon;
  float half_span_km;
  uint8_t range_index;
  uint8_t reserved[3];
  int16_t elev_m[kGridPoints];
  uint8_t land_mask[kLandMaskBytes];
  uint32_t checksum;
};
#pragma pack(pop)

// At 61x61 this record is about 8 KiB, larger than the ESP32-C3 loopTask can
// safely spare. Persistence code must acquire it from the heap briefly and
// release it before terrain HTTP/TLS/PNG work; never declare one as a local.
static_assert(sizeof(PersistedGrid) > 4096,
              "PersistedGrid must not be allocated on a task stack");

PersistedGrid* allocatePersistenceScratch() {
  PersistedGrid* record =
      static_cast<PersistedGrid*>(std::calloc(1, sizeof(PersistedGrid)));
  if (record == nullptr) {
    platform::logf("terrain: persistence scratch allocation failed\n");
  }
  return record;
}

uint32_t checksum(const PersistedGrid& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  const size_t len = offsetof(PersistedGrid, checksum);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    hash = (hash ^ bytes[i]) * 16777619u;
  }
  return hash;
}

/**
 * ONE cached grid — the view on screen — and not one per range preset.
 *
 * A 61x61 slot is about 8 KB, so four would be over 30 KB of static RAM. That
 * is not spare change on an ESP32-C3: ADS-B TLS has priority over this
 * decorative cache. Re-fetching after a range change is the cheaper price.
 */
Grid s_grid;
uint8_t s_grid_range_index = kNoRange;

/**
 * Retry gate for the view that last failed. Keyed by preset so that tapping to
 * another range tries immediately instead of inheriting the gate.
 */
unsigned long s_fail_ms = 0;
uint8_t s_fail_range_index = kNoRange;

platform::PollFn s_poll_fn = nullptr;
PngDecodeFn s_png_decode = nullptr;

/**
 * The one download in progress. Only the current view is ever fetched, so a
 * single cursor suffices; a view change mid-download simply restarts it.
 */
struct Progress {
  bool active = false;
  uint8_t range_index = 0;
  double center_lat = 0.0;
  double center_lon = 0.0;
  float half_span_km = 0.0f;
  int zoom = 0;
  TileId tiles[kMaxTiles];
  size_t tile_count = 0;
  size_t next_tile = 0;             ///< index into tiles[]
  int failures = 0;                 ///< consecutive failures on that tile
  int filled = 0;                   ///< grid samples decoded so far
  unsigned long last_request_ms = 0;
};

/** A tile gets this many attempts before the whole download is abandoned. */
constexpr int kMaxTileFailures = 3;
Progress s_prog;

/**
 * Where the grid's samples live in the global Mercator pixel space of the
 * download in progress. Longitude drives x and latitude drives y only, so the
 * grid's 1681 sample positions collapse into these two axes — the whole
 * resampler is built on that.
 *
 * Both are non-decreasing: grid column 0 is the west edge, row 0 the north
 * edge, and Mercator y grows southward.
 */
int32_t s_col_px[kGridSize] = {};
int32_t s_row_py[kGridSize] = {};

/**
 * The same positions expressed inside the tile being decoded, or -1 for a grid
 * row/column that tile does not carry. Keeping the hot loop on tile-local
 * coordinates is what makes it correct across the antimeridian, where global px
 * and the tile's wrapped x no longer share an origin.
 */
int16_t s_col_local[kGridSize];
int16_t s_row_local[kGridSize];

/**
 * Reverse map for the tile being decoded: for each of its 256 pixel rows and
 * columns, the FIRST grid row/column that samples it, or -1 for none.
 *
 * Only the first is stored because equal entries in s_row_local / s_col_local
 * are contiguous, so the decoder can walk forward from here. That matters at
 * high latitude, where Mercator squeezes the view's pixel width enough for two
 * grid columns to share one pixel; storing a single index would leave the other
 * column at sea level.
 */
int16_t s_row_first[kTilePx];
int16_t s_col_first[kTilePx];

/** Decode target, parked here because PixelFn carries no capture. */
Grid* s_tile_grid = nullptr;
int s_tile_filled = 0;

bool progressMatches(const Progress& p, double lat, double lon,
                     uint8_t range_index, float half_span_km) {
  return p.active && p.range_index == range_index &&
         std::fabs(p.center_lat - lat) < kCenterEpsilonDeg &&
         std::fabs(p.center_lon - lon) < kCenterEpsilonDeg &&
         p.half_span_km == half_span_km;
}

/** North-west and south-east corners of the grid, i.e. its bounding box. */
void viewCorners(double center_lat, double center_lon, float half_span_km,
                 double* n_lat, double* w_lon, double* s_lat, double* e_lon) {
  pointLatLon(center_lat, center_lon, half_span_km, 0, 0, n_lat, w_lon);
  pointLatLon(center_lat, center_lon, half_span_km, kGridSize - 1,
              kGridSize - 1, s_lat, e_lon);
}

/** Pins the grid's sample positions to the pixel grid of `zoom`. */
void mapGridToPixels(double center_lat, double center_lon, float half_span_km,
                     int zoom) {
  double lat = 0.0;
  double lon = 0.0;
  double px = 0.0;
  double py = 0.0;
  for (int i = 0; i < kGridSize; ++i) {
    // Row i for the latitude, column i for the longitude: the two axes are
    // independent, so one diagonal walk fills both tables.
    pointLatLon(center_lat, center_lon, half_span_km, i, i, &lat, &lon);
    latLonToTilePixel(lat, lon, zoom, &px, &py);
    s_col_px[i] = static_cast<int32_t>(std::floor(px));
    s_row_py[i] = static_cast<int32_t>(std::floor(py));
  }
}

/** Resolves the grid's samples into this tile's pixels; see s_row_first. */
void beginTile(Grid* g, const TileId& tile) {
  s_tile_grid = g;
  s_tile_filled = 0;
  memset(s_row_first, -1, sizeof(s_row_first));
  memset(s_col_first, -1, sizeof(s_col_first));

  // The x offset is taken modulo the world width because tilesForView() wraps
  // tile x at the antimeridian: such a view has grid columns whose px runs off
  // the east edge while their data lives in the low-numbered tile, and a plain
  // subtraction would place them nowhere and leave them at sea level.
  const int32_t world_px = static_cast<int32_t>(kTilePx)
                           << static_cast<unsigned>(tile.z);
  const int32_t tile_px0 = static_cast<int32_t>(tile.x) * kTilePx;
  const int32_t tile_py0 = static_cast<int32_t>(tile.y) * kTilePx;

  for (int i = 0; i < kGridSize; ++i) {
    int32_t local_x = (s_col_px[i] - tile_px0) % world_px;
    if (local_x < 0) {
      local_x += world_px;
    }
    s_col_local[i] = local_x < kTilePx ? static_cast<int16_t>(local_x) : -1;
    if (s_col_local[i] >= 0 && s_col_first[local_x] < 0) {
      s_col_first[local_x] = static_cast<int16_t>(i);
    }

    const int32_t local_y = s_row_py[i] - tile_py0;
    const bool in_tile = local_y >= 0 && local_y < kTilePx;
    s_row_local[i] = in_tile ? static_cast<int16_t>(local_y) : -1;
    if (in_tile && s_row_first[local_y] < 0) {
      s_row_first[local_y] = static_cast<int16_t>(i);
    }
  }
}

/**
 * Folds one decoded tile pixel into every grid sample that lands on it.
 *
 * Runs up to 65536 times per tile, so the early-out on rows no grid row
 * samples — five out of six at the zoom levels we pick — carries the cost.
 */
void onPixel(void* /*ctx*/, uint32_t x, uint32_t y, uint8_t r, uint8_t g,
             uint8_t b) {
  if (s_tile_grid == nullptr || x >= static_cast<uint32_t>(kTilePx) ||
      y >= static_cast<uint32_t>(kTilePx)) {
    return;
  }
  const int row_first = s_row_first[y];
  if (row_first < 0) {
    return;
  }
  const int col_first = s_col_first[x];
  if (col_first < 0) {
    return;
  }

  const int16_t elev = terrariumElevation(r, g, b);
  const int16_t local_x = static_cast<int16_t>(x);
  const int16_t local_y = static_cast<int16_t>(y);
  for (int row = row_first; row < kGridSize && s_row_local[row] == local_y;
       ++row) {
    for (int col = col_first; col < kGridSize && s_col_local[col] == local_x;
         ++col) {
      s_tile_grid->elev_m[row * kGridSize + col] = elev;
      ++s_tile_filled;
    }
  }
}

bool decodeBody(platform::BodyReader& body) {
  return s_png_decode != nullptr && s_png_decode(body, onPixel, nullptr);
}

bool stageBody(platform::BodyReader& body) {
  return platform::TemporaryBody::store(body);
}

/**
 * Marks the download in flight over.
 */
void endDownload() { s_prog.active = false; }

/** Holds off further attempts at this view for kTerrainRetryIntervalMs. */
void gateRetries(uint8_t range_index) {
  s_fail_ms = platform::nowMs();
  s_fail_range_index = range_index;
}

bool populateLandMask(Grid* grid, double center_lat, double center_lon,
                      float half_span_km) {
  memset(grid->land_mask, 0, sizeof(grid->land_mask));
  for (int row = 0; row < kGridSize; ++row) {
    for (int col = 0; col < kGridSize; ++col) {
      double lat = 0.0, lon = 0.0;
      pointLatLon(center_lat, center_lon, half_span_km, row, col, &lat, &lon);
      bool land = false;
      if (!core::land_water::classify(lat, lon, &land)) return false;
      if (land) {
        const int index = row * kGridSize + col;
        grid->land_mask[index >> 3] |= 1u << (index & 7);
      }
    }
  }
  return true;
}

}  // namespace

void setPollFn(platform::PollFn fn) { s_poll_fn = fn; }

void setPngDecoder(PngDecodeFn fn) { s_png_decode = fn; }

bool downloadActive() { return s_prog.active; }

void clear() {
  s_grid.valid = false;
  s_grid_range_index = kNoRange;
  s_fail_range_index = kNoRange;
  endDownload();
}

bool gridReady(double center_lat, double center_lon, uint8_t range_index) {
  return s_grid.valid && s_grid_range_index == range_index &&
         std::fabs(s_grid.center_lat - center_lat) < kCenterEpsilonDeg &&
         std::fabs(s_grid.center_lon - center_lon) < kCenterEpsilonDeg;
}

bool loadPersistedGrid(double center_lat, double center_lon,
                       uint8_t range_index, float half_span_km) {
  PersistedGrid* record = allocatePersistenceScratch();
  if (record == nullptr) return false;

  const bool valid =
      platform::TerrainCacheStore::load(record, sizeof(*record)) &&
      record->magic == kCacheMagic &&
      record->version == kCacheFormatVersion &&
      record->grid_size == kGridSize && record->range_index == range_index &&
      std::fabs(record->center_lat - center_lat) < kCenterEpsilonDeg &&
      std::fabs(record->center_lon - center_lon) < kCenterEpsilonDeg &&
      record->half_span_km == half_span_km &&
      record->checksum == checksum(*record);
  if (!valid) {
    std::free(record);
    return false;
  }
  s_grid.center_lat = record->center_lat;
  s_grid.center_lon = record->center_lon;
  s_grid.half_span_km = record->half_span_km;
  memcpy(s_grid.elev_m, record->elev_m, sizeof(s_grid.elev_m));
  memcpy(s_grid.land_mask, record->land_mask, sizeof(s_grid.land_mask));
  std::free(record);
  s_grid.valid = true;
  s_grid_range_index = range_index;
  endDownload();
  platform::logf("terrain: loaded persisted grid\n");
  return true;
}

bool persistGrid() {
  if (!s_grid.valid || s_grid_range_index == kNoRange) return false;
  PersistedGrid* record = allocatePersistenceScratch();
  if (record == nullptr) return false;

  record->magic = kCacheMagic;
  record->version = kCacheFormatVersion;
  record->grid_size = kGridSize;
  record->center_lat = s_grid.center_lat;
  record->center_lon = s_grid.center_lon;
  record->half_span_km = s_grid.half_span_km;
  record->range_index = s_grid_range_index;
  memcpy(record->elev_m, s_grid.elev_m, sizeof(record->elev_m));
  memcpy(record->land_mask, s_grid.land_mask, sizeof(record->land_mask));
  record->checksum = checksum(*record);
  const bool saved =
      platform::TerrainCacheStore::save(record, sizeof(*record));
  std::free(record);
  return saved;
}

bool ensureGridPersisted(double center_lat, double center_lon,
                         uint8_t range_index, float half_span_km) {
  if (gridReady(center_lat, center_lon, range_index)) return true;
  if (loadPersistedGrid(center_lat, center_lon, range_index, half_span_km)) {
    return true;
  }
  do {
    if (ensureGrid(center_lat, center_lon, range_index, half_span_km)) {
      if (!persistGrid()) {
        platform::logf("terrain: could not persist decoded grid\n");
      }
      return true;
    }
    if (!downloadActive()) return false;
    if (s_poll_fn != nullptr) s_poll_fn();
    platform::sleepMs(10);
  } while (true);
}

void invalidatePersistedGrid() { platform::TerrainCacheStore::remove(); }

const Grid* grid(uint8_t range_index) {
  if (!s_grid.valid || s_grid_range_index != range_index) {
    return nullptr;
  }
  return &s_grid;
}

bool isLand(const Grid& grid, int row, int col) {
  if (row < 0 || row >= kGridSize || col < 0 || col >= kGridSize) return false;
  const int index = row * kGridSize + col;
  return (grid.land_mask[index >> 3] & (1u << (index & 7))) != 0;
}

void pointLatLon(double center_lat, double center_lon, float half_span_km,
                 int row, int col, double* lat, double* lon) {
  const double span = static_cast<double>(half_span_km);
  const double dx_km = (2.0 * col / (kGridSize - 1) - 1.0) * span;
  const double dy_km = (1.0 - 2.0 * row / (kGridSize - 1)) * span;
  *lat = center_lat + dy_km / core::geo::kKmPerDeg;
  *lon = center_lon + dx_km / core::geo::kKmPerDeg;
}

int16_t terrariumElevation(uint8_t r, uint8_t g, uint8_t b) {
  const double meters =
      static_cast<double>(r) * 256.0 + g + b / 256.0 - 32768.0;
  // The encoding's ceiling is 32767.996 m, half a metre past what the return
  // type holds. Real terrain never approaches it (Everest encodes as R=162),
  // but saturating keeps a corrupt pixel reading as a peak instead of wrapping
  // into a -32768 m hole.
  if (meters > INT16_MAX) {
    return INT16_MAX;
  }
  return static_cast<int16_t>(lround(meters));
}

void latLonToTilePixel(double lat, double lon, int zoom, double* px,
                       double* py) {
  if (lat > kMercatorMaxLat) {
    lat = kMercatorMaxLat;
  } else if (lat < -kMercatorMaxLat) {
    lat = -kMercatorMaxLat;
  }
  const double scale = static_cast<double>(kTilePx) *
                       static_cast<double>(1u << static_cast<unsigned>(zoom));
  *px = (lon + 180.0) / 360.0 * scale;
  // atanh(sin lat) is the numerically friendly form of ln(tan + sec).
  const double s = std::sin(lat * kPi / 180.0);
  *py = (0.5 - std::log((1.0 + s) / (1.0 - s)) / (4.0 * kPi)) * scale;

  // At the clamp latitude the closed form lands a fraction of a pixel outside
  // the projection, which would have floor() name a tile row that does not
  // exist and drop the grid rows of a high-Arctic view. Longitude is
  // deliberately NOT clamped: a view crossing the antimeridian depends on px
  // running past the edge so tilesForView() can wrap it back with a modulo.
  const double last_py = std::nextafter(scale, 0.0);
  if (*py < 0.0) {
    *py = 0.0;
  } else if (*py > last_py) {
    *py = last_py;
  }
}

int zoomForView(double center_lat, double center_lon, float half_span_km) {
  double n_lat = 0.0;
  double w_lon = 0.0;
  double s_lat = 0.0;
  double e_lon = 0.0;
  viewCorners(center_lat, center_lon, half_span_km, &n_lat, &w_lon, &s_lat,
              &e_lon);

  for (int zoom = kMaxZoom; zoom > 0; --zoom) {
    double px0 = 0.0;
    double py0 = 0.0;
    double px1 = 0.0;
    double py1 = 0.0;
    latLonToTilePixel(n_lat, w_lon, zoom, &px0, &py0);
    latLonToTilePixel(s_lat, e_lon, zoom, &px1, &py1);
    // A span of at most one tile crosses at most one tile boundary per axis,
    // so the view needs no more than a 2x2 block. The ordering checks reject a
    // box that wrapped the antimeridian, which falls through to zoom 0 — one
    // tile of the whole world, coarse but correct.
    if (px1 >= px0 && py1 >= py0 && px1 - px0 <= kTilePx &&
        py1 - py0 <= kTilePx) {
      return zoom;
    }
  }
  return 0;
}

size_t tilesForView(double center_lat, double center_lon, float half_span_km,
                    int zoom, TileId* out, size_t max_out) {
  if (out == nullptr || max_out == 0) {
    return 0;
  }
  double n_lat = 0.0;
  double w_lon = 0.0;
  double s_lat = 0.0;
  double e_lon = 0.0;
  viewCorners(center_lat, center_lon, half_span_km, &n_lat, &w_lon, &s_lat,
              &e_lon);

  double px0 = 0.0;
  double py0 = 0.0;
  double px1 = 0.0;
  double py1 = 0.0;
  latLonToTilePixel(n_lat, w_lon, zoom, &px0, &py0);
  latLonToTilePixel(s_lat, e_lon, zoom, &px1, &py1);

  const int span = 1 << static_cast<unsigned>(zoom);
  const int tx0 = static_cast<int>(std::floor(px0 / kTilePx));
  const int tx1 = static_cast<int>(std::floor(px1 / kTilePx));
  const int ty0 = static_cast<int>(std::floor(py0 / kTilePx));
  const int ty1 = static_cast<int>(std::floor(py1 / kTilePx));

  size_t count = 0;
  for (int ty = ty0; ty <= ty1; ++ty) {
    if (ty < 0 || ty >= span) {
      continue;  // above the north or below the south edge of the projection
    }
    for (int tx = tx0; tx <= tx1; ++tx) {
      if (count >= max_out) {
        return count;
      }
      out[count].z = zoom;
      out[count].x = ((tx % span) + span) % span;  // longitude wraps
      out[count].y = ty;
      ++count;
    }
  }
  return count;
}

void buildTileUrl(char* buf, size_t len, const TileId& tile) {
  snprintf(buf, len, config::kTerrainTileUrlFmt, tile.z, tile.x, tile.y);
}

bool ensureGrid(double center_lat, double center_lon, uint8_t range_index,
                float half_span_km) {
  if (gridReady(center_lat, center_lon, range_index)) {
    return true;
  }
  if (range_index >= core::settings::kRangePresetCount ||
      s_png_decode == nullptr) {
    return false;
  }
  if (!core::land_water::coversView(center_lat, center_lon, half_span_km)) {
    if (!gridReady(center_lat, center_lon, range_index)) {
      s_grid.valid = false;
      s_grid_range_index = kNoRange;
      endDownload();
    }
    return false;
  }
  if (s_fail_range_index == range_index &&
      platform::nowMs() - s_fail_ms < config::kTerrainRetryIntervalMs) {
    return false;
  }

  Grid& g = s_grid;
  if (!progressMatches(s_prog, center_lat, center_lon, range_index,
                       half_span_km)) {
    s_prog.range_index = range_index;
    s_prog.center_lat = center_lat;
    s_prog.center_lon = center_lon;
    s_prog.half_span_km = half_span_km;
    s_prog.zoom = zoomForView(center_lat, center_lon, half_span_km);
    s_prog.tile_count = tilesForView(center_lat, center_lon, half_span_km,
                                     s_prog.zoom, s_prog.tiles, kMaxTiles);
    s_prog.next_tile = 0;
    s_prog.failures = 0;
    s_prog.filled = 0;
    s_prog.last_request_ms = 0;  // first tile goes out immediately
    s_prog.active = true;
    g.valid = false;
    s_grid_range_index = kNoRange;
    memset(g.elev_m, 0, sizeof(g.elev_m));
    if (!populateLandMask(&g, center_lat, center_lon, half_span_km)) {
      platform::logf("terrain: land mask does not cover this view\n");
      endDownload();
      return false;
    }
    if (s_prog.tile_count == 0) {
      // Only a view outside the projection's latitude range gets here. Take the
      // retry gate so this is diagnosed once rather than every loop.
      platform::logf("terrain: no tiles cover this view\n");
      gateRetries(range_index);
      endDownload();
      return false;
    }
    mapGridToPixels(center_lat, center_lon, half_span_km, s_prog.zoom);
  }

  // Hand control back to the main loop between tiles; see the interval's note.
  if ((s_prog.next_tile > 0 || s_prog.failures > 0) &&
      platform::nowMs() - s_prog.last_request_ms <
          config::kTerrainTileIntervalMs) {
    return false;
  }

  const TileId& tile = s_prog.tiles[s_prog.next_tile];
  char url[160];
  buildTileUrl(url, sizeof(url), tile);

  beginTile(&g, tile);
  s_prog.last_request_ms = platform::nowMs();
  platform::TemporaryBody::clear();
  if (!platform::TemporaryBody::prepare()) {
    platform::logf("terrain: staging storage unavailable — deferring\n");
    gateRetries(range_index);
    endDownload();
    return false;
  }
  int status = platform::HttpClient::getStatus(
      url, stageBody, config::kTerrainRequestTimeoutMs, s_poll_fn);
  if (status == 200 && !platform::TemporaryBody::replay(decodeBody)) {
    status = 0;
  }
  platform::TemporaryBody::clear();
  s_tile_grid = nullptr;

  if (status != 200) {
    // Status zero is a transport/decoder failure. In particular, retrying an
    // mbedTLS allocation failure immediately only fragments the same heap
    // further, so defer the whole decorative layer to its normal retry gate.
    if (status == 0) {
      platform::logf("terrain: tile %d/%d/%d transport failed — deferring\n",
                     tile.z, tile.x, tile.y);
      gateRetries(range_index);
      endDownload();
      return false;
    }
    // A single failed tile should not scrap the ones already decoded: keep the
    // cursor and retry recoverable HTTP responses on a later call.
    ++s_prog.failures;
    platform::logf("terrain: tile %d/%d/%d failed (attempt %d)\n", tile.z,
                   tile.x, tile.y, s_prog.failures);
    if (s_prog.failures >= kMaxTileFailures) {
      gateRetries(range_index);
      endDownload();
    }
    return false;
  }

  s_prog.failures = 0;
  s_prog.filled += s_tile_filled;
  ++s_prog.next_tile;
  if (s_prog.next_tile < s_prog.tile_count) {
    return false;  // more tiles to go on later calls
  }

  endDownload();
  if (s_prog.filled < kGridPoints) {
    // Every grid sample sits inside one of the tiles we asked for, so a short
    // count means the tile selection and the sample mapping disagree. Publishing
    // it anyway would paint the gaps as a hard sea-level band, which reads as
    // real terrain; leaving the layer off is the honest failure.
    platform::logf("terrain: only %d/%d samples decoded — discarding\n",
                   s_prog.filled, kGridPoints);
    gateRetries(range_index);
    return false;
  }
  g.center_lat = center_lat;
  g.center_lon = center_lon;
  g.half_span_km = half_span_km;
  g.valid = true;
  s_grid_range_index = range_index;
  s_fail_range_index = kNoRange;
  platform::logf("terrain: grid ready (zoom %d, %u tile%s)\n", s_prog.zoom,
                 static_cast<unsigned>(s_prog.tile_count),
                 s_prog.tile_count == 1 ? "" : "s");
  return true;
}

int bandForElevation(int16_t elev_m, const int16_t* band_min_m,
                     int band_count) {
  int band = -1;
  for (int i = 0; i < band_count; ++i) {
    if (elev_m >= band_min_m[i]) {
      band = i;
    } else {
      break;
    }
  }
  return band;
}

int bandForSample(int16_t elev_m, bool is_land,
                  const int16_t* band_min_m, int band_count) {
  return is_land ? bandForElevation(elev_m, band_min_m, band_count) : -1;
}

bool landMedianElevation(const Grid& grid, int16_t* median_m) {
  if (median_m == nullptr) return false;
  int land_count = 0;
  for (int row = 0; row < kGridSize; ++row) {
    for (int col = 0; col < kGridSize; ++col) {
      if (isLand(grid, row, col)) ++land_count;
    }
  }
  if (land_count == 0) return false;

  // Find the lower median in the int16 value domain. Sixteen passes over the
  // compact grid are cheap during a static rebuild and avoid placing a 7.4 KiB
  // sample array on loopTask's stack (or allocating one from its scarce heap).
  const int target = (land_count - 1) / 2;
  int32_t lo = INT16_MIN;
  int32_t hi = INT16_MAX;
  while (lo < hi) {
    const int32_t mid = lo + (hi - lo) / 2;
    int at_or_below = 0;
    for (int row = 0; row < kGridSize; ++row) {
      for (int col = 0; col < kGridSize; ++col) {
        if (isLand(grid, row, col) &&
            grid.elev_m[row * kGridSize + col] <= mid) {
          ++at_or_below;
        }
      }
    }
    if (at_or_below > target) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  *median_m = static_cast<int16_t>(lo);
  return true;
}

uint16_t verticalStepForRangeIndex(uint8_t range_index) {
  constexpr uint16_t kStepM[] = {5, 10, 20, 50, 100};
  static_assert(sizeof(kStepM) / sizeof(kStepM[0]) ==
                core::settings::kRangePresetCount);
  return kStepM[range_index < core::settings::kRangePresetCount
                    ? range_index
                    : core::settings::kRangePresetCount - 1];
}

bool localReliefBandFloors(int16_t reference_m, uint16_t step_m,
                           int16_t* band_min_m, int band_count) {
  if (band_min_m == nullptr || band_count <= 0 || (band_count & 1) == 0 ||
      step_m == 0) {
    return false;
  }
  const int centre = band_count / 2;
  for (int i = 0; i < band_count; ++i) {
    const int32_t floor_m =
        static_cast<int32_t>(reference_m) + (i - centre) * step_m;
    band_min_m[i] = static_cast<int16_t>(
        floor_m < INT16_MIN ? INT16_MIN
                            : (floor_m > INT16_MAX ? INT16_MAX : floor_m));
  }
  return true;
}

}  // namespace core::terrain
