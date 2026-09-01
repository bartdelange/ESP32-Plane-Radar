#pragma once

namespace core::land_water {

/** Geographic mapping for a square radar view; independent of terrain grid. */
struct PixelView {
  double north_lat;
  double west_lon;
  double degrees_per_pixel;
  int size;
};

/** True when the generated regional mask can classify the complete view. */
bool coversView(double center_lat, double center_lon, float half_span_km);

/** Look up one coordinate. Returns false outside all maintained regions. */
bool classify(double lat, double lon, bool* is_land);

/** Build the screen-pixel mapping for the radar's flat-earth terrain view. */
bool makePixelView(double center_lat, double center_lon, float half_span_km,
                   int size, PixelView* view);

/** Classify one display pixel directly against the compiled regional mask. */
bool classifyPixel(const PixelView& view, int x, int y, bool* is_land);

}  // namespace core::land_water
