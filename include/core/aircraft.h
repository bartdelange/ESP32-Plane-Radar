#pragma once

#include <cstddef>

#include "core/airlines.h"

namespace core::adsb {

/**
 * One decoded ADS-B target.
 *
 * Tag strings are fixed-size and pre-formatted at parse time so the render path
 * does no allocation and no formatting.
 */
struct Aircraft {
  float lat;
  float lon;
  float nose_deg;   ///< where the airframe points
  float track_deg;  ///< where it is actually going
  float gs_knots;
  char hex[7];
  char callsign[9];
  char type[5];
  char alt[12];
  /** Resolved from the callsign's ICAO prefix, or nullptr when unknown. */
  const airlines::Airline* airline;
  float vertical_rate_fpm;
  char route_origin[20];
  char route_destination[20];
  char route_airline[24];
};

/** Hard cap on tracked targets; the render path allocates arrays of this size. */
constexpr size_t kMaxAircraft = 64;

}  // namespace core::adsb
