#pragma once

// Airline lookup: maps a flight callsign's 3-letter ICAO airline designator
// (e.g. "BAW" in "BAW123") to the airline's IATA acronym and full name.
// Curated list of common/major airlines; unknown or non-airline (GA, military,
// registration) callsigns resolve to nullptr.
namespace core::airlines {

struct Airline {
  const char* icao;        // 3-letter ICAO designator (callsign prefix)
  const char* iata;        // 2-letter IATA code / acronym (may be "")
  const char* name;        // full airline name, e.g. "British Airways"
  const char* short_name;  // friendly short name, e.g. "Virgin", "American"
};

/**
 * Resolve a flight callsign (e.g. "BAW123") to an airline, or nullptr if the
 * callsign does not look like an airline flight (needs 3 letters + a digit) or
 * the ICAO code is not in the table.
 */
const Airline* forCallsign(const char* callsign);

/** Route operator wins for Full Name; local data remains the offline fallback. */
const char* preferredFullName(const Airline* local,
                              const char* route_operator);

}  // namespace core::airlines
