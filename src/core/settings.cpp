#include "core/settings.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "config.h"
#include "core/platform.h"

namespace core::settings
{

  namespace
  {

    constexpr char kKeyRange[] = "rangeIdx";
    constexpr char kKeyRangePresets[] = "rangePresets";
    constexpr char kKeyFixedRangeSchema[] = "fixedRangeV";
    constexpr uint8_t kFixedRangeSchemaVersion = 1;
    /**
     * Deliberately not the old "useMiles" key. That one meant km-vs-statute-miles;
     * this one means NM-vs-km, so reusing it would silently invert the preference
     * on every already-configured device.
     */
    constexpr char kKeyKm[] = "useKm";
    constexpr char kKeyRunways[] = "showRwys";
    constexpr char kKeyTerrain[] = "showTerrain";
    constexpr char kKeyRoutes[] = "showRoutes";
    constexpr char kKeyAirlineDisplay[] = "airlnDisp";
    constexpr char kKeyLat[] = "lat";
    constexpr char kKeyLon[] = "lon";

    constexpr uint8_t kDefaultRangeIndex = 1; // 20 km ring

    double s_lat = config::kDefaultRadarLat;
    double s_lon = config::kDefaultRadarLon;
    uint8_t s_range_index = kDefaultRangeIndex;
    bool s_use_km = true;
    bool s_show_runways = true;
    bool s_show_terrain = true;
    bool s_show_routes = true;
    AirlineDisplay s_airline_display = AirlineDisplay::kNone;

    CenterChangedFn s_center_changed_fn = nullptr;
    RangeChangedFn s_range_changed_fn = nullptr;

    using KV = platform::KeyValueStore;

    /** Latitude within +/-90 and longitude within +/-180. */
    bool validLatLon(double lat_v, double lon_v)
    {
      return lat_v >= -90.0 && lat_v <= 90.0 && lon_v >= -180.0 && lon_v <= 180.0;
    }

    void applyLocation(double lat_v, double lon_v)
    {
      const double prev_lat = s_lat;
      const double prev_lon = s_lon;
      s_lat = lat_v;
      s_lon = lon_v;

      if (s_center_changed_fn != nullptr &&
          (s_lat != prev_lat || s_lon != prev_lon))
      {
        s_center_changed_fn();
      }
    }

    uint8_t migrateLegacyRangeIndex(uint8_t old_index)
    {
      // Old defaults were 10,15,20,40,80,120 km. Preserve the nearest useful
      // fixed view without ever parsing or applying the old configurable list.
      constexpr uint8_t kOldToFixed[] = {0, 1, 1, 2, 3, 4};
      return old_index < sizeof(kOldToFixed) ? kOldToFixed[old_index]
                                             : kDefaultRangeIndex;
    }

  } // namespace

  // --- Lifecycle ---------------------------------------------------------------

  void init()
  {
    uint8_t saved = KV::getU8(kNsRadar, kKeyRange, kDefaultRangeIndex);
    if (KV::getU8(kNsRadar, kKeyFixedRangeSchema, 0) !=
        kFixedRangeSchemaVersion)
    {
      saved = migrateLegacyRangeIndex(saved);
      KV::putU8(kNsRadar, kKeyRange, saved);
      KV::putU8(kNsRadar, kKeyFixedRangeSchema, kFixedRangeSchemaVersion);
      // Retire the old arbitrary list without touching any other setting.
      KV::remove(kNsRadar, kKeyRangePresets);
    }
    s_range_index = saved < kRangePresetCount ? saved : kDefaultRangeIndex;
    s_use_km = KV::getBool(kNsRadar, kKeyKm, true);
    s_show_runways = KV::getBool(kNsRadar, kKeyRunways, true);
    s_show_terrain = KV::getBool(kNsRadar, kKeyTerrain, true);
    s_show_routes = KV::getBool(kNsRadar, kKeyRoutes, true);
    const uint8_t airline_mode = KV::getU8(
        kNsRadar, kKeyAirlineDisplay, static_cast<uint8_t>(AirlineDisplay::kNone));
    s_airline_display =
        airline_mode <= static_cast<uint8_t>(AirlineDisplay::kAbbrev)
            ? static_cast<AirlineDisplay>(airline_mode)
            : AirlineDisplay::kNone;

    if (KV::has(kNsRadar, kKeyLat) && KV::has(kNsRadar, kKeyLon))
    {
      const double lat_v = KV::getDouble(kNsRadar, kKeyLat, config::kDefaultRadarLat);
      const double lon_v = KV::getDouble(kNsRadar, kKeyLon, config::kDefaultRadarLon);
      if (validLatLon(lat_v, lon_v))
      {
        applyLocation(lat_v, lon_v);
      }
    }
  }

  // --- Radar centre ------------------------------------------------------------

  double lat() { return s_lat; }

  double lon() { return s_lon; }

  void setCenterChangedFn(CenterChangedFn fn)
  {
    s_center_changed_fn = fn;
  }

  void clearLocation()
  {
    KV::remove(kNsRadar, kKeyLat);
    KV::remove(kNsRadar, kKeyLon);
    applyLocation(config::kDefaultRadarLat, config::kDefaultRadarLon);
  }

  bool saveLocationFromPortal(const char *lat_str, const char *lon_str)
  {
    if (lat_str == nullptr || lon_str == nullptr)
      return false;
    char *lat_end = nullptr;
    char *lon_end = nullptr;
    const double lat_v = strtod(lat_str, &lat_end);
    const double lon_v = strtod(lon_str, &lon_end);
    if (lat_end == lat_str || lon_end == lon_str || *lat_end != '\0' ||
        *lon_end != '\0' || !validLatLon(lat_v, lon_v))
      return false;
    KV::putDouble(kNsRadar, kKeyLat, lat_v);
    KV::putDouble(kNsRadar, kKeyLon, lon_v);
    applyLocation(lat_v, lon_v);
    platform::logf("Current location: %.6f, %.6f\n", lat_v, lon_v);
    return true;
  }

  // --- Range preset ------------------------------------------------------------

  void rangeNext()
  {
    s_range_index =
        static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
    KV::putU8(kNsRadar, kKeyRange, s_range_index);
    if (s_range_changed_fn != nullptr)
      s_range_changed_fn();
  }

  const RangePreset &rangeCurrent() { return kRangePresets[s_range_index]; }

  const RangePreset &rangePreset(size_t index)
  {
    return kRangePresets[index < kRangePresetCount ? index : 0];
  }

  size_t rangeCount() { return kRangePresetCount; }

  uint8_t rangeIndex() { return s_range_index; }

  void setRangeChangedFn(RangeChangedFn fn) { s_range_changed_fn = fn; }

  // --- Units and overlays ------------------------------------------------------

  bool useKm() { return s_use_km; }

  bool showRunways() { return s_show_runways; }

  bool showTerrain() { return s_show_terrain; }

  bool showRoutes() { return s_show_routes; }

  AirlineDisplay airlineDisplay() { return s_airline_display; }

  void saveKmFromPortal(const char *checkbox_value)
  {
    s_use_km = portalCheckboxChecked(checkbox_value);
    KV::putBool(kNsRadar, kKeyKm, s_use_km);
    platform::logf("Distance units: %s\n", s_use_km ? "km" : "NM");
  }

  void saveRunwaysFromPortal(const char *checkbox_value)
  {
    s_show_runways = portalCheckboxChecked(checkbox_value);
    KV::putBool(kNsRadar, kKeyRunways, s_show_runways);
    platform::logf("Runway overlay: %s\n", s_show_runways ? "on" : "off");
  }

  void saveTerrainFromPortal(const char *checkbox_value)
  {
    s_show_terrain = portalCheckboxChecked(checkbox_value);
    KV::putBool(kNsRadar, kKeyTerrain, s_show_terrain);
    platform::logf("Terrain: %s\n", s_show_terrain ? "on" : "off");
  }

  void saveRouteDisplayFromPortal(const char *select_value)
  {
    s_show_routes = select_value != nullptr && strcmp(select_value, "1") == 0;
    KV::putBool(kNsRadar, kKeyRoutes, s_show_routes);
    platform::logf("Route display: %s\n", s_show_routes ? "on" : "off");
  }

  void saveAirlineDisplayFromPortal(const char *select_value)
  {
    uint8_t mode = static_cast<uint8_t>(AirlineDisplay::kNone);
    if (select_value != nullptr && select_value[0] >= '0' &&
        select_value[0] <= '2' && select_value[1] == '\0')
    {
      mode = static_cast<uint8_t>(select_value[0] - '0');
    }
    s_airline_display = static_cast<AirlineDisplay>(mode);
    KV::putU8(kNsRadar, kKeyAirlineDisplay, mode);
    const char *labels[] = {"none", "full name", "abbreviation"};
    platform::logf("Airline display: %s\n", labels[mode]);
  }

  void unitsReset()
  {
    s_use_km = true;
    s_show_runways = true;
    s_show_terrain = true;
    s_show_routes = true;
    s_airline_display = AirlineDisplay::kNone;
    KV::remove(kNsRadar, kKeyKm);
    KV::remove(kNsRadar, kKeyRunways);
    KV::remove(kNsRadar, kKeyTerrain);
    KV::remove(kNsRadar, kKeyRoutes);
    KV::remove(kNsRadar, kKeyAirlineDisplay);
    // rangeIdx is intentionally left alone; see the header.
  }

  // --- Pure helpers ------------------------------------------------------------

  bool portalCheckboxChecked(const char *value)
  {
    return value != nullptr &&
           (strcmp(value, "T") == 0 ||
            strcmp(value, "t") == 0 ||
            strcmp(value, "1") == 0 ||
            strcmp(value, "on") == 0 ||
            strcmp(value, "true") == 0);
  }

  void formatRing3Label(char *buf, size_t len, float ring3_km, bool use_km)
  {
    if (use_km)
    {
      const int km = static_cast<int>(lroundf(ring3_km));
      snprintf(buf, len, "%dkm", km);
    }
    else
    {
      const int nm = static_cast<int>(lroundf(ring3_km / kKmPerNauticalMile));
      snprintf(buf, len, "%dNM", nm);
    }
  }

  void formatCurrentRing3Label(char *buf, size_t len)
  {
    formatRing3Label(buf, len, rangeCurrent().ring3_km, s_use_km);
  }

} // namespace core::settings
