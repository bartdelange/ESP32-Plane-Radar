#!/usr/bin/env python3
"""Offline tests for the shared compiled-region generator configuration."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))

import build_copernicus_terrain
import build_large_airports
import build_region_pack
from regions import get_region, render_header


class RegionPackTests(unittest.TestCase):
    def test_nl_is_the_single_shared_definition(self):
        region = get_region("nl")
        self.assertEqual("NL", region.code)
        self.assertEqual(("NL",), region.countries)
        self.assertEqual((512, 512),
                         (region.terrain_width, region.terrain_height))
        self.assertEqual((2048, 2048),
                         (region.water_width, region.water_height))
        self.assertEqual(build_region_pack.REGION_HEADER,
                         build_large_airports.REGION_HEADER)

    def test_unknown_region_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unknown region"):
            get_region("XX")

    def test_generated_header_contains_portable_metadata(self):
        header = render_header(get_region("NL"))
        self.assertIn('constexpr char kCode[] = "NL";', header)
        self.assertIn("constexpr uint16_t kTerrainWidth = 512;", header)
        self.assertIn("constexpr uint16_t kWaterWidth = 2048;", header)

    def test_generators_target_core_data_locations(self):
        self.assertEqual("include/core/large_airports.h",
                         str(build_large_airports.OUT_H.relative_to(build_large_airports.ROOT)))
        self.assertEqual("src/core/large_airports_data.cpp",
                         str(build_large_airports.OUT_CPP.relative_to(build_large_airports.ROOT)))
        self.assertEqual("include/core/copernicus_terrain_data.h",
                         str(build_copernicus_terrain.OUT_H.relative_to(
                             build_copernicus_terrain.ROOT)))
        self.assertEqual("src/core/copernicus_terrain_data.cpp",
                         str(build_copernicus_terrain.OUT_CPP.relative_to(
                             build_copernicus_terrain.ROOT)))

    def test_copernicus_paths_and_digest_are_deterministic(self):
        dem, wbm = build_copernicus_terrain.tile_urls(52, 5)
        self.assertIn("N52_00_E005_00_DEM.tif", dem)
        self.assertTrue(wbm.endswith("N52_00_E005_00_WBM.tif"))
        first = build_copernicus_terrain.payload_digest(b"elevation", b"water")
        second = build_copernicus_terrain.payload_digest(b"elevation", b"water")
        self.assertEqual(first, second)

    def test_dem_and_wbm_windows_are_geographically_aligned(self):
        region = get_region("NL")
        dem = build_copernicus_terrain.destination_window(
            region, region.terrain_width, region.terrain_height, 52, 5)
        wbm = build_copernicus_terrain.destination_window(
            region, region.water_width, region.water_height, 52, 5)
        for coarse, fine in zip(dem, wbm):
            self.assertLessEqual(abs(fine / 4 - coarse), 1)


if __name__ == "__main__":
    unittest.main()
