#pragma once

namespace core::land_water {

/** Geographic mapping for a square radar view; independent of terrain grid. */
struct PixelView {
  double north_lat;
  double west_lon;
  double lat_degrees_per_pixel;
  double lon_degrees_per_pixel;
  int center_x;
  int center_y;
  int size;
};

/** True when the generated regional mask can classify the complete view. */
bool coversView(double center_lat, double center_lon, float half_span_km);

/** Look up one coordinate. Returns false outside all maintained regions. */
bool classify(double lat, double lon, bool* is_land);

/** Map a covered coordinate to the generated WBM raster cell. */
bool rasterCell(double lat, double lon, int* row, int* col);

/** Classify an already-mapped WBM raster cell without geographic math. */
bool classifyRasterCell(int row, int col, bool* is_land);

/** Build the screen-pixel mapping for the radar's flat-earth terrain view. */
bool makePixelView(double center_lat, double center_lon, float half_span_km,
                   int size, PixelView* view);

/** Geographic coordinate represented by a display pixel in this view. */
bool pixelLatLon(const PixelView& view, int x, int y, double* lat, double* lon);

/** Classify one display pixel directly against the compiled regional mask. */
bool classifyPixel(const PixelView& view, int x, int y, bool* is_land);

}  // namespace core::land_water
