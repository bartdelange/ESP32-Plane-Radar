#!/usr/bin/env python3
"""Regenerate the compiled airport + Copernicus terrain regional pack."""

from __future__ import annotations

import argparse
from pathlib import Path

import build_copernicus_terrain
import build_large_airports
from regions import DEFAULT_REGION, get_region, render_header

ROOT = Path(__file__).resolve().parents[1]
REGION_HEADER = ROOT / "include/core/region_pack.h"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--region", default=DEFAULT_REGION)
    args = parser.parse_args()
    try:
        region = get_region(args.region)
    except ValueError as error:
        parser.error(str(error))
    REGION_HEADER.write_text(render_header(region), encoding="utf-8")
    build_large_airports.generate(region)
    elevation, water = build_copernicus_terrain.generate_arrays(region)
    build_copernicus_terrain.write_outputs(region, elevation, water)
    print(f"Compiled region pack: {region.name} ({region.code})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
