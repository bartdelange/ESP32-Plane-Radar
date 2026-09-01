// Generated from scripts/regions.py — do not edit.
#pragma once

#include <cstddef>
#include <cstdint>

namespace data::region_pack {

constexpr char kCode[] = "NL";
constexpr char kName[] = "Netherlands";
constexpr const char* kCountries[] = {"NL"};
constexpr size_t kCountryCount = sizeof(kCountries) / sizeof(kCountries[0]);
constexpr double kWest = 1.0;
constexpr double kSouth = 48.5;
constexpr double kEast = 9.5;
constexpr double kNorth = 55.8;
constexpr uint16_t kTerrainWidth = 512;
constexpr uint16_t kTerrainHeight = 512;
constexpr uint16_t kWaterWidth = 2048;
constexpr uint16_t kWaterHeight = 2048;

}  // namespace data::region_pack
