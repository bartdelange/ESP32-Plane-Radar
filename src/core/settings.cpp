#include "core/settings.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "config.h"
#include "core/platform.h"

namespace core::settings {

namespace {

constexpr char kKeyRange[] = "rangeIdx";
/**
 * Deliberately not the old "useMiles" key. That one meant km-vs-statute-miles;
 * this one means NM-vs-km, so reusing it would silently invert the preference
 * on every already-configured device.
 */
constexpr char kKeyKm[] = "useKm";
constexpr char kKeyRunways[] = "showRwys";
constexpr char kKeyTerrain[] = "showTerr";
constexpr char kKeyAirlineDisplay[] = "airlnDisp";
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";

constexpr uint8_t kDefaultRangeIndex = 1;  // 40 NM ring

double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;
uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_km = false;  // default is nautical miles
bool s_show_runways = true;
bool s_show_terrain = true;
AirlineDisplay s_airline_display = AirlineDisplay::kNone;

CenterChangedFn s_center_changed_fn = nullptr;

using KV = platform::KeyValueStore;

/** Latitude within +/-90 and longitude within +/-180. */
bool validLatLon(double lat_v, double lon_v) {
  return lat_v >= -90.0 && lat_v <= 90.0 && lon_v >= -180.0 && lon_v <= 180.0;
}

void applyLocation(double lat_v, double lon_v) {
  const double prev_lat = s_lat;
  const double prev_lon = s_lon;
  s_lat = lat_v;
  s_lon = lon_v;

  if (s_center_changed_fn != nullptr &&
      (s_lat != prev_lat || s_lon != prev_lon)) {
    s_center_changed_fn();
  }
}

}  // namespace

// --- Lifecycle ---------------------------------------------------------------

void init() {
  const uint8_t saved = KV::getU8(kNsRadar, kKeyRange, kDefaultRangeIndex);
  s_range_index = (saved < kRangePresetCount) ? saved : kDefaultRangeIndex;
  s_use_km = KV::getBool(kNsRadar, kKeyKm, false);
  s_show_runways = KV::getBool(kNsRadar, kKeyRunways, true);
  s_show_terrain = KV::getBool(kNsRadar, kKeyTerrain, true);
  const uint8_t airline_mode = KV::getU8(
      kNsRadar, kKeyAirlineDisplay, static_cast<uint8_t>(AirlineDisplay::kNone));
  s_airline_display =
      airline_mode <= static_cast<uint8_t>(AirlineDisplay::kAbbrev)
          ? static_cast<AirlineDisplay>(airline_mode)
          : AirlineDisplay::kNone;

  if (KV::has(kNsRadar, kKeyLat) && KV::has(kNsRadar, kKeyLon)) {
    const double lat_v = KV::getDouble(kNsRadar, kKeyLat, config::kDefaultRadarLat);
    const double lon_v = KV::getDouble(kNsRadar, kKeyLon, config::kDefaultRadarLon);
    if (validLatLon(lat_v, lon_v)) {
      applyLocation(lat_v, lon_v);
    }
  }
}

// --- Radar centre ------------------------------------------------------------

double lat() { return s_lat; }

double lon() { return s_lon; }

void setCenterChangedFn(CenterChangedFn fn) {
  s_center_changed_fn = fn;
}

void clearLocation() {
  KV::remove(kNsRadar, kKeyLat);
  KV::remove(kNsRadar, kKeyLon);
  applyLocation(config::kDefaultRadarLat, config::kDefaultRadarLon);
}

bool saveLocationFromPortal(const char* lat_str, const char* lon_str) {
  if (lat_str == nullptr || lon_str == nullptr) return false;
  char* lat_end = nullptr;
  char* lon_end = nullptr;
  const double lat_v = strtod(lat_str, &lat_end);
  const double lon_v = strtod(lon_str, &lon_end);
  if (lat_end == lat_str || lon_end == lon_str || *lat_end != '\0' ||
      *lon_end != '\0' || !validLatLon(lat_v, lon_v)) return false;
  KV::putDouble(kNsRadar, kKeyLat, lat_v);
  KV::putDouble(kNsRadar, kKeyLon, lon_v);
  applyLocation(lat_v, lon_v);
  platform::logf("Current location: %.6f, %.6f\n", lat_v, lon_v);
  return true;
}

// --- Range preset ------------------------------------------------------------

void rangeNext() {
  s_range_index = static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
  KV::putU8(kNsRadar, kKeyRange, s_range_index);
}

const RangePreset& rangeCurrent() { return kRangePresets[s_range_index]; }

uint8_t rangeIndex() { return s_range_index; }

// --- Units and overlays ------------------------------------------------------

bool useKm() { return s_use_km; }

bool showRunways() { return s_show_runways; }

bool showTerrain() { return s_show_terrain; }

AirlineDisplay airlineDisplay() { return s_airline_display; }

void saveKmFromPortal(const char* checkbox_value) {
  s_use_km = portalCheckboxChecked(checkbox_value);
  KV::putBool(kNsRadar, kKeyKm, s_use_km);
  platform::logf("Distance units: %s\n", s_use_km ? "km" : "NM");
}

void saveRunwaysFromPortal(const char* checkbox_value) {
  s_show_runways = portalCheckboxChecked(checkbox_value);
  KV::putBool(kNsRadar, kKeyRunways, s_show_runways);
  platform::logf("Runway overlay: %s\n", s_show_runways ? "on" : "off");
}

void saveTerrainFromPortal(const char* checkbox_value) {
  s_show_terrain = portalCheckboxChecked(checkbox_value);
  KV::putBool(kNsRadar, kKeyTerrain, s_show_terrain);
  platform::logf("Terrain layer: %s\n", s_show_terrain ? "on" : "off");
}

void saveAirlineDisplayFromPortal(const char* select_value) {
  uint8_t mode = static_cast<uint8_t>(AirlineDisplay::kNone);
  if (select_value != nullptr && select_value[0] >= '0' &&
      select_value[0] <= '2' && select_value[1] == '\0') {
    mode = static_cast<uint8_t>(select_value[0] - '0');
  }
  s_airline_display = static_cast<AirlineDisplay>(mode);
  KV::putU8(kNsRadar, kKeyAirlineDisplay, mode);
  const char* labels[] = {"none", "full name", "abbreviation"};
  platform::logf("Airline display: %s\n", labels[mode]);
}

void unitsReset() {
  s_use_km = false;
  s_show_runways = true;
  s_show_terrain = true;
  s_airline_display = AirlineDisplay::kNone;
  KV::remove(kNsRadar, kKeyKm);
  KV::remove(kNsRadar, kKeyRunways);
  KV::remove(kNsRadar, kKeyTerrain);
  KV::remove(kNsRadar, kKeyAirlineDisplay);
  // rangeIdx is intentionally left alone; see the header.
}

// --- Pure helpers ------------------------------------------------------------

bool portalCheckboxChecked(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  if ((value[0] == 'T' || value[0] == 't' || value[0] == 'F' ||
       value[0] == 'f') &&
      value[1] == '\0') {
    return true;
  }
  return strcmp(value, "on") == 0;
}

void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_km) {
  if (use_km) {
    const int km = static_cast<int>(lroundf(ring3_km));
    snprintf(buf, len, "%dkm", km);
  } else {
    const int nm = static_cast<int>(lroundf(ring3_km / kKmPerNauticalMile));
    snprintf(buf, len, "%dNM", nm);
  }
}

void formatCurrentRing3Label(char* buf, size_t len) {
  formatRing3Label(buf, len, rangeCurrent().ring3_km, s_use_km);
}

}  // namespace core::settings
