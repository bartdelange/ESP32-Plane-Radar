#!/usr/bin/env python3
"""Generate a compact regional terrain pack from Copernicus GLO-30.

The public AWS Open Data copy of the 2021 GLO-30 release contains the DEM COG
and its matching categorical WBM in the same one-degree product directory.
Rasterio/GDAL reads only the COG ranges needed for the configured output grid;
raw GeoTIFFs and a global mosaic are never written to the repository.

Elevation is area-averaged. WBM is resampled categorically (mode), then packed
to one water bit per sample. Pure-ocean geocells absent from GLO-30 remain
water by construction.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import sys
from pathlib import Path

from regions import DEFAULT_REGION, Region, get_region

ROOT = Path(__file__).resolve().parents[1]
OUT_H = ROOT / "include/core/copernicus_terrain_data.h"
OUT_CPP = ROOT / "src/core/copernicus_terrain_data.cpp"

BASE_URL = "https://copernicus-dem-30m.s3.eu-central-1.amazonaws.com"
SOURCE_RELEASE = "Copernicus DEM GLO-30 Public 2021"
def tile_stem(lat: int, lon: int) -> str:
    ns = f"N{lat:02d}" if lat >= 0 else f"S{-lat:02d}"
    ew = f"E{lon:03d}" if lon >= 0 else f"W{-lon:03d}"
    return f"Copernicus_DSM_COG_10_{ns}_00_{ew}_00_DEM"


def tile_urls(lat: int, lon: int) -> tuple[str, str]:
    stem = tile_stem(lat, lon)
    native = stem[:-4]  # strip the trailing _DEM
    root = f"{BASE_URL}/{stem}"
    return (f"{root}/{stem}.tif", f"{root}/AUXFILES/{native}_WBM.tif")


def tile_origins(region: Region):
    for lat in range(math.floor(region.south), math.ceil(region.north)):
        for lon in range(math.floor(region.west), math.ceil(region.east)):
            yield lat, lon


def destination_window(region: Region, width: int, height: int,
                       lat: int, lon: int) -> tuple[int, int, int, int]:
    """Output rows/columns touched by a one-degree source geocell."""
    col0 = max(0, math.floor((lon - region.west) * width /
                             (region.east - region.west)))
    col1 = min(width, math.ceil((lon + 1 - region.west) * width /
                                (region.east - region.west)))
    row0 = max(0, math.floor((region.north - (lat + 1)) * height /
                             (region.north - region.south)))
    row1 = min(height, math.ceil((region.north - lat) * height /
                                 (region.north - region.south)))
    return row0, row1, col0, col1


def pack_water(values) -> bytes:
    """Pack categorical WBM values: zero=land, nonzero=water."""
    flat = values.ravel()
    bits = bytearray((flat.size + 7) // 8)
    for index in range(flat.size):
        if int(flat[index]) != 0:
            bits[index >> 3] |= 1 << (index & 7)
    return bytes(bits)


def generate_arrays(region: Region):
    try:
        import numpy as np
        import rasterio
        from rasterio.enums import Resampling
        from rasterio.transform import from_bounds
        from rasterio.windows import Window, transform as window_transform
        from rasterio.warp import reproject
    except ImportError as error:
        raise SystemExit(
            "terrain generation needs numpy and rasterio; run "
            "`uv run --with-requirements scripts/terrain-requirements.txt "
            "scripts/build_copernicus_terrain.py`"
        ) from error

    elev = np.zeros((region.terrain_height, region.terrain_width), dtype=np.float32)
    water = np.ones((region.water_height, region.water_width), dtype=np.uint8)
    elev_transform = from_bounds(region.west, region.south, region.east,
                                 region.north, region.terrain_width,
                                 region.terrain_height)
    water_transform = from_bounds(region.west, region.south, region.east,
                                  region.north, region.water_width,
                                  region.water_height)

    for lat, lon in tile_origins(region):
        dem_url, wbm_url = tile_urls(lat, lon)
        print(f"Reading {tile_stem(lat, lon)}", file=sys.stderr)
        try:
            source = rasterio.open(dem_url)
        except rasterio.errors.RasterioIOError as error:
            # GLO-30 deliberately omits pure-ocean geocells.
            if "404" not in str(error):
                raise
            print("  absent geocell; retaining ocean defaults", file=sys.stderr)
            continue
        with source:
            row0, row1, col0, col1 = destination_window(
                region, region.terrain_width, region.terrain_height, lat, lon)
            window = Window(col0, row0, col1 - col0, row1 - row0)
            tile_elev = np.zeros((row1 - row0, col1 - col0), dtype=np.float32)
            reproject(
                rasterio.band(source, 1), tile_elev,
                src_transform=source.transform, src_crs=source.crs,
                dst_transform=window_transform(window, elev_transform),
                dst_crs="EPSG:4326",
                resampling=Resampling.average, init_dest_nodata=False,
            )
            elev[row0:row1, col0:col1] = tile_elev
        # A present DEM product must carry its matching categorical WBM. Do not
        # silently turn a packaging/alignment failure into ocean.
        with rasterio.open(wbm_url) as source:
            row0, row1, col0, col1 = destination_window(
                region, region.water_width, region.water_height, lat, lon)
            window = Window(col0, row0, col1 - col0, row1 - row0)
            tile_water = np.ones((row1 - row0, col1 - col0), dtype=np.uint8)
            reproject(
                rasterio.band(source, 1), tile_water,
                src_transform=source.transform, src_crs=source.crs,
                dst_transform=window_transform(window, water_transform),
                dst_crs="EPSG:4326",
                resampling=Resampling.mode, init_dest_nodata=False,
            )
            water[row0:row1, col0:col1] = tile_water

    rounded = np.rint(elev)
    np.clip(rounded, -32768, 32767, out=rounded)
    return rounded.astype("<i2"), pack_water(water)


def byte_lines(data: bytes, indent: str = "  ") -> str:
    return "\n".join(
        indent + ", ".join(f"0x{value:02x}" for value in data[i:i + 16]) + ","
        for i in range(0, len(data), 16)
    )


def int16_lines(values) -> str:
    flat = values.ravel()
    return "\n".join(
        "  " + ", ".join(str(int(v)) for v in flat[i:i + 16]) + ","
        for i in range(0, flat.size, 16)
    )


def payload_digest(elevation_bytes: bytes, water_bits: bytes) -> str:
    return hashlib.sha256(elevation_bytes + water_bits).hexdigest()


def render_header(region: Region, digest: str) -> str:
    return f'''// Generated by scripts/build_copernicus_terrain.py — do not edit.
#pragma once

#include <cstddef>
#include <cstdint>

namespace data::copernicus_terrain {{

constexpr double kWest = {region.west};
constexpr double kSouth = {region.south};
constexpr double kEast = {region.east};
constexpr double kNorth = {region.north};
constexpr uint16_t kElevationWidth = {region.terrain_width};
constexpr uint16_t kElevationHeight = {region.terrain_height};
constexpr uint16_t kWaterWidth = {region.water_width};
constexpr uint16_t kWaterHeight = {region.water_height};
constexpr char kSource[] = "{SOURCE_RELEASE}";
constexpr char kAttribution[] =
    "produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and "
    "© Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS "
    "by the European Union and ESA; all rights reserved";
constexpr char kSha256[] = "{digest}";

extern const int16_t kElevation[];
extern const uint8_t kWaterBits[];
constexpr size_t kElevationCount =
    static_cast<size_t>(kElevationWidth) * kElevationHeight;
constexpr size_t kWaterBytes =
    (static_cast<size_t>(kWaterWidth) * kWaterHeight + 7) / 8;

}}  // namespace data::copernicus_terrain
'''


def write_outputs(region: Region, elevation, water_bits: bytes) -> None:
    raw_elevation = elevation.tobytes(order="C")
    digest = payload_digest(raw_elevation, water_bits)
    OUT_H.write_text(render_header(region, digest), encoding="utf-8")
    OUT_CPP.write_text(
        "// Generated by scripts/build_copernicus_terrain.py — do not edit.\n"
        "#include \"core/copernicus_terrain_data.h\"\n\n"
        "namespace data::copernicus_terrain {\n\n"
        f"const int16_t kElevation[] = {{\n{int16_lines(elevation)}\n}};\n\n"
        f"const uint8_t kWaterBits[] = {{\n{byte_lines(water_bits)}\n}};\n\n"
        "static_assert(sizeof(kElevation) / sizeof(kElevation[0]) == kElevationCount);\n"
        "static_assert(sizeof(kWaterBits) == kWaterBytes);\n\n"
        "}  // namespace data::copernicus_terrain\n",
        encoding="utf-8",
    )
    print(f"Generated {len(raw_elevation)} elevation bytes + "
          f"{len(water_bits)} water bytes; sha256={digest}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--region", default=DEFAULT_REGION)
    args = parser.parse_args()
    region = get_region(args.region)
    elevation, water = generate_arrays(region)
    write_outputs(region, elevation, water)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
