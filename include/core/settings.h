#pragma once

/**
 * Persistent user settings: radar centre, range preset, distance units and the
 * runway overlay toggle.
 *
 * Merged from the former services/radar_location.cpp and ui/radar_range.cpp,
 * which shared no code but the same job. Storage goes through
 * core::platform::KeyValueStore, so the device keeps using NVS and the native
 * harness uses a file.
 *
 * The radar centre has one source: the user's persisted current/home latitude
 * and longitude. Airport data is display/reference data only.
 */

#include <cstddef>
#include <cstdint>

namespace core::settings {

/** How aircraft tags should identify a resolved airline. */
enum class AirlineDisplay : uint8_t {
  kNone = 0,
  kFullName = 1,
  kAbbrev = 2,
};

/**
 * Range presets use integer kilometres as their canonical persisted unit.
 * The value is the ring-3 label (3/4 of the outer radius); UI labels convert
 * it to nautical miles only when requested.
 */
struct RangePreset {
  /** Distance shown on ring 3 (3/4 of outer radius), always stored in km. */
  uint16_t ring3_km;
  float outer_km;
};

constexpr float kRing3ToOuterKm = 4.0f / 3.0f;
constexpr float kKmPerNauticalMile = 1.852f;
constexpr size_t kMaxRangePresets = 8;
constexpr uint16_t kMaxRangeKm = 500;
constexpr uint16_t kDefaultRangeKm[] = {10, 20, 40, 80, 120};
constexpr size_t kDefaultRangeCount =
    sizeof(kDefaultRangeKm) / sizeof(kDefaultRangeKm[0]);

/**
 * Storage namespace. The name and its keys are frozen — changing either would
 * strand every already-configured device on the next firmware update.
 *
 * Devices flashed before the manual coordinates were removed still carry an
 * orphaned "radar" namespace holding lat/lon. Nothing reads it.
 */
constexpr char kNsRadar[] = "planeradar";  ///< location, display and range keys

// --- Lifecycle ---------------------------------------------------------------

/** Load everything from storage, falling back to config defaults. */
void init();

// --- Radar centre ------------------------------------------------------------

double lat();
double lon();

using CenterChangedFn = void (*)();

/**
 * Called whenever lat()/lon() actually move, from any path — the BOOT double
 * tap, a portal save, or a credential wipe.
 *
 * The hook exists so cache invalidation stops depending on which code path
 * moved the centre: the tap handler used to clear the ADS-B store by hand and
 * the portal path did not, so a portal-driven move plotted stale traffic
 * against the new centre. Kept as a function pointer to keep core::settings
 * free of core/adsb.h and UI headers.
 */
void setCenterChangedFn(CenterChangedFn fn);

/** Drop stored current-location coordinates and revert to config defaults. */
void clearLocation();
/** Parse, validate, persist and apply portal coordinate fields. */
bool saveLocationFromPortal(const char* lat_str, const char* lon_str);

// --- Range preset ------------------------------------------------------------

/** Advance to the next preset and persist the index. */
void rangeNext();
const RangePreset& rangeCurrent();
const RangePreset& rangePreset(size_t index);
size_t rangeCount();
uint8_t rangeIndex();

using RangeChangedFn = void (*)();
void setRangeChangedFn(RangeChangedFn fn);

/** Validate, persist, and apply a comma-separated list of integer kilometres. */
bool saveRangePresetsFromPortal(const char* value);
void formatRangePresets(char* buf, size_t len);

// --- Units and overlays ------------------------------------------------------

/** False (the default) means nautical miles. */
bool useKm();
bool showRunways();
bool showRoutes();
AirlineDisplay airlineDisplay();

/** Apply a WiFi portal checkbox value and persist it. */
void saveKmFromPortal(const char* checkbox_value);
void saveRunwaysFromPortal(const char* checkbox_value);
/** Apply the portal selector: 0 off, 1 on. */
void saveRouteDisplayFromPortal(const char* select_value);
/** Apply the portal selector: 0 none, 1 full name, 2 friendly abbreviation. */
void saveAirlineDisplayFromPortal(const char* select_value);

/**
 * Reset units, overlays, airline labels and route display to their defaults.
 *
 * Note the asymmetry with clearLocation(), which resets the current location:
 * this deliberately does NOT reset the range preset. A Wi-Fi credential wipe
 * returns the display to NM with runways and routes on, but leaves
 * the user's chosen zoom alone.
 */
void unitsReset();

// --- Pure helpers (exposed for unit testing) ---------------------------------

/**
 * Interpret a WiFiManager checkbox submission.
 *
 * WiFiManager submits the field's value= attribute rather than a conventional
 * "on", and the portal prefills that attribute with "T" (or "F"), encoding the
 * real state in the presence of the `checked` attribute. So any single T/F
 * means "the box was submitted", i.e. checked.
 */
bool portalCheckboxChecked(const char* value);

/** Render a ring-3 label, e.g. "40NM" or "74km". */
void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_km);

/** formatRing3Label for the active preset and unit setting. */
void formatCurrentRing3Label(char* buf, size_t len);

/** Strict parser used by portal saves and persisted-data validation. */
bool parseRangePresets(const char* text, uint16_t* out, size_t capacity,
                       size_t* count);

}  // namespace core::settings
