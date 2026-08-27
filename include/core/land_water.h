#pragma once

namespace core::land_water {

/** True when the generated regional mask can classify the complete view. */
bool coversView(double center_lat, double center_lon, float half_span_km);

/** Look up one coordinate. Returns false outside all maintained regions. */
bool classify(double lat, double lon, bool* is_land);

}  // namespace core::land_water
