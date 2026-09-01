#!/usr/bin/env python3
"""Generate a compact regional land/water mask from Natural Earth GeoJSON.

The maintained region list is intentionally data-driven so another country can
be added without changing firmware logic. Natural Earth 1:10m land and European
lakes are public domain: https://www.naturalearthdata.com/about/terms-of-use/
"""

from __future__ import annotations

import json
import sys
import urllib.request
from pathlib import Path

from regions import DEFAULT_REGION, Region, get_region, render_header as render_region_header

ROOT = Path(__file__).resolve().parents[1]
OUT_H = ROOT / "include/core/land_mask.h"
OUT_CPP = ROOT / "src/core/land_mask_data.cpp"
REGION_HEADER = ROOT / "include/core/region_pack.h"

LAND_URL = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_10m_land.geojson"
LAKES_URL = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_10m_lakes.geojson"

def load_geojson(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=60) as response:
        return json.load(response)


def polygons(collection: dict):
    for feature in collection["features"]:
        geometry = feature.get("geometry") or {}
        coords = geometry.get("coordinates", [])
        if geometry.get("type") == "Polygon":
            yield coords
        elif geometry.get("type") == "MultiPolygon":
            yield from coords


def ring_bounds(ring):
    xs = [p[0] for p in ring]
    ys = [p[1] for p in ring]
    return min(xs), min(ys), max(xs), max(ys)


def intersects(bounds, region):
    west, south, east, north = bounds
    return not (east < region["west"] or west > region["east"] or
                north < region["south"] or south > region["north"])


def relevant_polygons(collection: dict, region: dict):
    return [polygon for polygon in polygons(collection)
            if polygon and intersects(ring_bounds(polygon[0]), region)]


def rasterize_polygons(region: dict, source_polygons) -> bytearray:
    width, height = region["width"], region["height"]
    bits = bytearray((width * height + 7) // 8)
    dx = (region["east"] - region["west"]) / width
    dy = (region["north"] - region["south"]) / height

    # Scan-convert each polygon. XORing its exterior and holes produces that
    # polygon's fill; polygons are then unioned into the layer bitset.
    for polygon in source_polygons:
        polygon_bits = bytearray(len(bits))
        for ring in polygon:
            intersections = [[] for _ in range(height)]
            previous = ring[-1]
            for current in ring:
                x0, y0 = previous
                x1, y1 = current
                if y0 != y1:
                    first = max(0, int((region["north"] - max(y0, y1)) / dy) - 1)
                    last = min(height - 1,
                               int((region["north"] - min(y0, y1)) / dy) + 1)
                    for row in range(first, last + 1):
                        lat = region["north"] - (row + 0.5) * dy
                        if (y0 > lat) != (y1 > lat):
                            intersections[row].append(
                                (x1 - x0) * (lat - y0) / (y1 - y0) + x0)
                previous = current
            for row, crossings in enumerate(intersections):
                crossings.sort()
                for i in range(0, len(crossings) - 1, 2):
                    first_col = max(0, int((crossings[i] - region["west"]) / dx))
                    last_col = min(width - 1,
                                   int((crossings[i + 1] - region["west"]) / dx))
                    for col in range(first_col, last_col + 1):
                        lon = region["west"] + (col + 0.5) * dx
                        if crossings[i] <= lon < crossings[i + 1]:
                            index = row * width + col
                            polygon_bits[index >> 3] ^= 1 << (index & 7)
        for i, value in enumerate(polygon_bits):
            bits[i] |= value
    return bits


def rasterize(region: dict, land_data: dict, lake_data: dict) -> bytes:
    land = rasterize_polygons(region, relevant_polygons(land_data, region))
    lakes = rasterize_polygons(region, relevant_polygons(lake_data, region))
    return bytes(land_byte & ~lake_byte for land_byte, lake_byte in zip(land, lakes))


def byte_lines(data: bytes) -> str:
    return "\n".join(
        "  " + ", ".join(f"0x{value:02x}" for value in data[i:i + 16]) + ","
        for i in range(0, len(data), 16)
    )


def region_dict(region: Region) -> dict:
    return {"name": region.code, "west": region.west, "south": region.south,
            "east": region.east, "north": region.north,
            "width": region.mask_width, "height": region.mask_height}


def generate(region_config: Region) -> None:
    land_data = load_geojson(LAND_URL)
    lake_data = load_geojson(LAKES_URL)
    region = region_dict(region_config)
    generated = [(region, rasterize(region, land_data, lake_data))]

    OUT_H.write_text("""// Generated by scripts/build_land_mask.py — do not edit.
#pragma once

#include <cstddef>
#include <cstdint>

namespace data::land_mask {

struct Region {
  const char* name;
  double west, south, east, north;
  uint16_t width, height;
  const uint8_t* bits;
};

extern const Region kRegions[];
extern const size_t kRegionCount;

}  // namespace data::land_mask
""", encoding="utf-8")

    arrays = []
    entries = []
    for region, bits in generated:
        symbol = f'k{region["name"]}Bits'
        arrays.append(f"const uint8_t {symbol}[] = {{\n{byte_lines(bits)}\n}};")
        entries.append(
            f'  {{region_pack::kCode, region_pack::kWest, region_pack::kSouth, '
            f'region_pack::kEast, region_pack::kNorth, region_pack::kMaskWidth, '
            f'region_pack::kMaskHeight, {symbol}}},')
    OUT_CPP.write_text(
        "// Generated by scripts/build_land_mask.py — do not edit.\n"
        "#include \"core/land_mask.h\"\n\n"
        "#include \"core/region_pack.h\"\n\n"
        "namespace data::land_mask {\nnamespace {\n\n" +
        "\n\n".join(arrays) +
        "\n\n}  // namespace\n\nconst Region kRegions[] = {\n" +
        "\n".join(entries) +
        "\n};\nconst size_t kRegionCount = sizeof(kRegions) / sizeof(kRegions[0]);\n\n"
        "}  // namespace data::land_mask\n", encoding="utf-8")

    total = sum(len(bits) for _, bits in generated)
    print(f"Generated {len(generated)} region(s), {total} mask bytes")


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--region", default=DEFAULT_REGION)
    args = parser.parse_args()
    try:
        region = get_region(args.region)
    except ValueError as error:
        parser.error(str(error))
    REGION_HEADER.write_text(render_region_header(region), encoding="utf-8")
    generate(region)
    return 0


if __name__ == "__main__":
    sys.exit(main())

