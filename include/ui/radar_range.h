#pragma once

/**
 * Range / units accessors for the UI layer.
 *
 * The state and its persistence now live in core::settings; everything here is
 * a forwarder so existing call sites read unchanged. fetchRadiusKm() stays in
 * this layer because it is screen geometry — it depends on radar_theme.h, which
 * core/ deliberately does not.
 */

#include <cstddef>
#include <cstdint>

#include "core/settings.h"
#include "ui/radar_theme.h"

namespace ui::radar {

using RangePreset = core::settings::RangePreset;
using AirlineDisplay = core::settings::AirlineDisplay;

inline void rangeInit() { core::settings::init(); }
inline void rangeNext() { core::settings::rangeNext(); }
inline const RangePreset& rangeCurrent() { return core::settings::rangeCurrent(); }
inline const RangePreset& rangePreset(size_t index) {
  return core::settings::rangePreset(index);
}
inline size_t rangeCount() { return core::settings::rangeCount(); }
inline uint8_t rangeIndex() { return core::settings::rangeIndex(); }

inline bool useKm() { return core::settings::useKm(); }
inline bool showRunways() { return core::settings::showRunways(); }
inline AirlineDisplay airlineDisplay() { return core::settings::airlineDisplay(); }

inline void saveKmFromPortal(const char* v) {
  core::settings::saveKmFromPortal(v);
}
inline void saveRunwaysFromPortal(const char* v) {
  core::settings::saveRunwaysFromPortal(v);
}
inline void saveAirlineDisplayFromPortal(const char* v) {
  core::settings::saveAirlineDisplayFromPortal(v);
}
inline bool saveRangePresetsFromPortal(const char* v) {
  return core::settings::saveRangePresetsFromPortal(v);
}
inline void unitsReset() { core::settings::unitsReset(); }

inline void formatRing3Label(char* buf, size_t len, float ring3_km,
                             bool use_km) {
  core::settings::formatRing3Label(buf, len, ring3_km, use_km);
}
inline void formatCurrentRing3Label(char* buf, size_t len) {
  core::settings::formatCurrentRing3Label(buf, len);
}

/**
 * ADS-B fetch radius (km), scaled out to the screen edge rather than the outer
 * ring, so aircraft shown as rim dots beyond the ring still have data.
 */
inline float fetchRadiusKm() {
  const float outer_km = rangeCurrent().outer_km;
  const float screen_r_px =
      static_cast<float>(kCenterX - kBeyondRingScreenMarginPx);
  return outer_km * (screen_r_px / static_cast<float>(kGridOuterRadius));
}

}  // namespace ui::radar
