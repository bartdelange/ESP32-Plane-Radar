#pragma once

#include <cstddef>

namespace core::route {

struct Info {
  char origin[20] = {};
  char destination[20] = {};
  char airline[24] = {};
};

/** Resolve a callsign, using a bounded in-memory cache and optional I/O budget. */
bool resolve(const char* callsign, Info* out, bool allow_network,
             bool* network_attempted = nullptr, void (*poll)() = nullptr);
/** ASCII-fold a UTF-8 municipality name for the embedded display. */
void foldAscii(const char* in, char* out, size_t out_len);

}  // namespace core::route
