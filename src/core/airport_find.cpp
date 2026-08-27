#include "core/airport_find.h"

#include <cctype>
#include <cstring>

namespace core::airport {

namespace {

using data::large_airports::Airport;
using data::large_airports::kAirportCount;
using data::large_airports::kAirports;
using data::large_airports::kBaseAirportCount;

bool normalizeIcao(const char* icao, char out[5]) {
  if (icao == nullptr) {
    return false;
  }
  size_t len = strnlen(icao, 5);
  if (len != 4) {
    return false;
  }
  for (size_t i = 0; i < 4; ++i) {
    const unsigned char c = static_cast<unsigned char>(icao[i]);
    if (!std::isalpha(c)) {
      return false;
    }
    out[i] = static_cast<char>(std::toupper(c));
  }
  out[4] = '\0';
  return true;
}

int identCompare(const char* a, const char* b) { return strcmp(a, b); }

bool findInRange(const char* key, size_t begin, size_t end, Airport* out) {
  size_t lo = begin;
  size_t hi = end;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const int cmp = identCompare(kAirports[mid].ident, key);
    if (cmp == 0) {
      if (out != nullptr) *out = kAirports[mid];
      return true;
    }
    if (cmp < 0) lo = mid + 1;
    else hi = mid;
  }
  return false;
}

}  // namespace

bool findAirport(const char* icao, Airport* out) {
  char key[5];
  if (!normalizeIcao(icao, key)) {
    return false;
  }

  return findInRange(key, 0, kBaseAirportCount, out) ||
         findInRange(key, kBaseAirportCount, kAirportCount, out);
}

}  // namespace core::airport
