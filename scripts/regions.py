"""Single source of truth for generated regional airport/map packs."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Region:
    code: str
    name: str
    countries: tuple[str, ...]
    west: float
    south: float
    east: float
    north: float
    mask_width: int = 512
    mask_height: int = 512


DEFAULT_REGION = "NL"

REGIONS = {
    "NL": Region(
        code="NL",
        name="Netherlands",
        countries=("NL",),
        west=1.0,
        south=48.5,
        east=9.5,
        north=55.8,
    ),
}


def get_region(code: str) -> Region:
    normalized = code.strip().upper()
    if normalized not in REGIONS:
        raise ValueError(
            f"unknown region {code!r}; choose one of {', '.join(sorted(REGIONS))}"
        )
    region = REGIONS[normalized]
    if len(region.code) != 2 or not region.code.isalpha() or not region.code.isupper():
        raise ValueError(f"region code must be uppercase ISO-style alpha-2: {region.code!r}")
    if not region.countries or any(
        len(country) != 2 or not country.isalpha() or not country.isupper()
        for country in region.countries
    ):
        raise ValueError(f"invalid ISO country list for {region.code}")
    if not (region.west < region.east and region.south < region.north):
        raise ValueError(f"invalid bounds for {region.code}")
    if region.mask_width <= 0 or region.mask_height <= 0:
        raise ValueError(f"invalid mask dimensions for {region.code}")
    return region


def render_header(region: Region) -> str:
    countries = ", ".join(f'"{code}"' for code in region.countries)
    return f"""// Generated from scripts/regions.py — do not edit.
#pragma once

#include <cstddef>
#include <cstdint>

namespace data::region_pack {{

constexpr char kCode[] = "{region.code}";
constexpr char kName[] = "{region.name}";
constexpr const char* kCountries[] = {{{countries}}};
constexpr size_t kCountryCount = sizeof(kCountries) / sizeof(kCountries[0]);
constexpr double kWest = {region.west};
constexpr double kSouth = {region.south};
constexpr double kEast = {region.east};
constexpr double kNorth = {region.north};
constexpr uint16_t kMaskWidth = {region.mask_width};
constexpr uint16_t kMaskHeight = {region.mask_height};

}}  // namespace data::region_pack
"""
