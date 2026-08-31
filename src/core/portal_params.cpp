#include "core/portal_params.h"

#include <cstdio>
#include <cstring>

#include "core/settings.h"
#include "core/platform.h"

namespace core::portal
{

  namespace
  {

    constexpr char kCoordAttrs[] = " type=\"number\" step=\"0.000001\"";

    constexpr Field kFields[] = {
        {"radar_lat", "Current Location Latitude (deg)", kCoordAttrs, Kind::kText, 20, false},
        {"radar_lon", "Current Location Longitude (deg)", kCoordAttrs, Kind::kText, 20, false},
        {"range_presets", "Radar range presets (km, comma-separated)",
         "pattern=\"[0-9 ,]+\" type=\"text\"", Kind::kText, 48, false},
        {"use_km", "Display distances in km", "type=\"checkbox\"",
         Kind::kCheckbox, 2, true},
        {"show_runways", "Show airport runways", "type=\"checkbox\"",
         Kind::kCheckbox, 2, true},
        {"airline_mode", "Airline labels", "", Kind::kSelect, 2, false},
        {"route_display", "Route display", "", Kind::kSelect, 2, false},
    };

    char s_pending_lat[24] = {};
    char s_pending_lon[24] = {};

    bool isField(const Field &f, const char *id)
    {
      return f.id != nullptr && id != nullptr && strcmp(f.id, id) == 0;
    }

  } // namespace

  const Field *fields() { return kFields; }

  size_t fieldCount() { return sizeof(kFields) / sizeof(kFields[0]); }

  void currentValue(const Field &field, char *buf, size_t len)
  {
    if (len == 0)
    {
      return;
    }
    if (field.kind == Kind::kCheckbox)
    {
      snprintf(buf, len, "T");
      return;
    }
    if (isField(field, "radar_lat"))
    {
      snprintf(buf, len, "%.6f", settings::lat());
      return;
    }
    if (isField(field, "radar_lon"))
    {
      snprintf(buf, len, "%.6f", settings::lon());
      return;
    }
    if (isField(field, "range_presets"))
    {
      settings::formatRangePresets(buf, len);
      return;
    }
    if (isField(field, "airline_mode"))
    {
      snprintf(buf, len, "%u",
               static_cast<unsigned>(settings::airlineDisplay()));
      return;
    }
    if (isField(field, "route_display"))
    {
      snprintf(buf, len, "%u", settings::showRoutes() ? 1U : 0U);
      return;
    }
    buf[0] = '\0';
  }

  void htmlAttrs(const Field &field, char *buf, size_t len)
  {
    if (len == 0)
    {
      return;
    }
    if (field.kind == Kind::kCheckbox)
    {
      bool on = false;
      if (isField(field, "use_km"))
      {
        on = settings::useKm();
      }
      else if (isField(field, "show_runways"))
      {
        on = settings::showRunways();
      }
      snprintf(buf, len, "%s%s", field.html_attrs, on ? " checked" : "");
      return;
    }

    snprintf(buf, len, "%s", field.html_attrs);
  }

  void applyValue(const Field &field, const char *value)
  {
    if (isField(field, "radar_lat"))
    {
      snprintf(s_pending_lat, sizeof(s_pending_lat), "%s", value ? value : "");
      return;
    }
    if (isField(field, "radar_lon"))
    {
      snprintf(s_pending_lon, sizeof(s_pending_lon), "%s", value ? value : "");
      return;
    }
    if (isField(field, "range_presets"))
    {
      if (!settings::saveRangePresetsFromPortal(value))
      {
        core::platform::logf(
            "Invalid range presets; keeping previous configured ranges\n");
      }
      return;
    }
    if (isField(field, "use_km"))
    {
      settings::saveKmFromPortal(value);
    }
    else if (isField(field, "show_runways"))
    {
      settings::saveRunwaysFromPortal(value);
    }
    else if (isField(field, "airline_mode"))
    {
      settings::saveAirlineDisplayFromPortal(value);
    }
    else if (isField(field, "route_display"))
    {
      settings::saveRouteDisplayFromPortal(value);
    }
  }

  bool applyValueById(const char *id, const char *value)
  {
    if (id == nullptr)
    {
      return false;
    }
    for (size_t i = 0; i < fieldCount(); ++i)
    {
      if (isField(kFields[i], id))
      {
        applyValue(kFields[i], value);
        return true;
      }
    }
    return false;
  }

  void commit()
  {
    if (!settings::saveLocationFromPortal(s_pending_lat, s_pending_lon))
    {
      core::platform::logf("Invalid current location in portal; keeping previous location\n");
    }
    s_pending_lat[0] = '\0';
    s_pending_lon[0] = '\0';
  }

} // namespace core::portal
