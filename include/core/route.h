#pragma once

#include <cstddef>
#include <cstdint>

namespace core::route {

struct Info {
  char origin[20] = {};
  char destination[20] = {};
  char airline[24] = {};
};

/** Resolve a callsign, using a bounded in-memory cache and optional I/O budget. */
bool resolve(const char* callsign, Info* out, bool allow_network,
             bool* network_attempted = nullptr, void (*poll)() = nullptr,
             bool* transient_failure = nullptr);
/** ASCII-fold a UTF-8 municipality name for the embedded display. */
void foldAscii(const char* in, char* out, size_t out_len);

/** Consume one cycle lookup and stop the cycle after a transient failure. */
bool consumeLookupBudget(bool attempted, bool transient_failure,
                         uint8_t* remaining);

}  // namespace core::route
