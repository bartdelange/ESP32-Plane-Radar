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
    RangePreset s_range_presets[kMaxRangePresets] = {};
    size_t s_range_count = 0;
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

    void setDefaultRanges()
    {
      s_range_count = kDefaultRangeCount;
      for (size_t i = 0; i < s_range_count; ++i)
      {
        s_range_presets[i] = {kDefaultRangeKm[i],
                              kDefaultRangeKm[i] * kRing3ToOuterKm};
      }
    }

    void applyRanges(const uint16_t *values, size_t count, uint16_t old_active_km)
    {
      s_range_count = count;
      for (size_t i = 0; i < count; ++i)
      {
        s_range_presets[i] = {values[i], values[i] * kRing3ToOuterKm};
      }
      size_t selected = count - 1;
      for (size_t i = 0; i < count; ++i)
      {
        if (values[i] >= old_active_km)
        {
          selected = i;
          break;
        }
      }
      s_range_index = static_cast<uint8_t>(selected);
    }

  } // namespace

  // --- Lifecycle ---------------------------------------------------------------

  void init()
  {
    setDefaultRanges();
    const std::string persisted =
        KV::getString(kNsRadar, kKeyRangePresets, "");
    uint16_t parsed[kMaxRangePresets] = {};
    size_t parsed_count = 0;
    if (!persisted.empty() &&
        parseRangePresets(persisted.c_str(), parsed, kMaxRangePresets,
                          &parsed_count))
    {
      applyRanges(parsed, parsed_count, parsed[0]);
    }
    const uint8_t saved = KV::getU8(kNsRadar, kKeyRange, kDefaultRangeIndex);
    s_range_index = saved < s_range_count
                        ? saved
                        : static_cast<uint8_t>(kDefaultRangeIndex < s_range_count
                                                   ? kDefaultRangeIndex
                                                   : 0);
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
    s_range_index = static_cast<uint8_t>((s_range_index + 1) % s_range_count);
    KV::putU8(kNsRadar, kKeyRange, s_range_index);
    if (s_range_changed_fn != nullptr)
      s_range_changed_fn();
  }

  const RangePreset &rangeCurrent() { return s_range_presets[s_range_index]; }

  const RangePreset &rangePreset(size_t index)
  {
    return s_range_presets[index < s_range_count ? index : 0];
  }

  size_t rangeCount() { return s_range_count; }

  uint8_t rangeIndex() { return s_range_index; }

  void setRangeChangedFn(RangeChangedFn fn) { s_range_changed_fn = fn; }

  bool saveRangePresetsFromPortal(const char *value)
  {
    uint16_t parsed[kMaxRangePresets] = {};
    size_t count = 0;
    if (!parseRangePresets(value, parsed, kMaxRangePresets, &count))
      return false;
    const uint16_t old_active = rangeCurrent().ring3_km;
    const uint8_t old_index = s_range_index;
    applyRanges(parsed, count, old_active);
    char persisted[48];
    formatRangePresets(persisted, sizeof(persisted));
    KV::putString(kNsRadar, kKeyRangePresets, persisted);
    KV::putU8(kNsRadar, kKeyRange, s_range_index);
    if (s_range_changed_fn != nullptr &&
        (old_active != rangeCurrent().ring3_km || old_index != s_range_index))
    {
      s_range_changed_fn();
    }
    return true;
  }

  void formatRangePresets(char *buf, size_t len)
  {
    if (buf == nullptr || len == 0)
      return;
    size_t used = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < s_range_count && used < len; ++i)
    {
      const int written = snprintf(buf + used, len - used, "%s%u", i ? "," : "",
                                   static_cast<unsigned>(s_range_presets[i].ring3_km));
      if (written < 0 || static_cast<size_t>(written) >= len - used)
      {
        buf[len - 1] = '\0';
        return;
      }
      used += static_cast<size_t>(written);
    }
  }

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

  bool parseRangePresets(const char *text, uint16_t *out, size_t capacity,
                         size_t *count)
  {
    if (count != nullptr)
      *count = 0;
    if (text == nullptr || out == nullptr || count == nullptr || capacity == 0)
      return false;
    const char *p = text;
    size_t n = 0;
    uint16_t previous = 0;
    while (true)
    {
      while (*p == ' ' || *p == '\t')
        ++p;
      if (*p < '0' || *p > '9' || n >= capacity || n >= kMaxRangePresets)
        return false;
      unsigned long value = 0;
      while (*p >= '0' && *p <= '9')
      {
        value = value * 10 + static_cast<unsigned>(*p - '0');
        if (value > kMaxRangeKm)
          return false;
        ++p;
      }
      while (*p == ' ' || *p == '\t')
        ++p;
      if (value == 0 || value <= previous)
        return false;
      out[n++] = static_cast<uint16_t>(value);
      previous = static_cast<uint16_t>(value);
      if (*p == '\0')
        break;
      if (*p++ != ',')
        return false;
    }
    *count = n;
    return true;
  }

} // namespace core::settings
