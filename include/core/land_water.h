#pragma once

#include <cstdint>

namespace core::land_water {

/** True when the generated regional mask can classify the complete view. */
bool coversView(double center_lat, double center_lon, float half_span_km);

/** Look up one coordinate. Returns false outside all maintained regions. */
bool classify(double lat, double lon, bool* is_land);

/**
 * Bilinearly filtered land coverage from the compiled regional mask.
 * 0 is water, 255 is land, and intermediate values soften mask-cell edges.
 * Returns false outside all maintained regions.
 */
bool coverage(double lat, double lon, uint8_t* land_coverage);

}  // namespace core::land_water
