#!/usr/bin/env python3
"""Offline tests for the shared compiled-region generator configuration."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))

import build_large_airports
import build_region_pack
from regions import get_region, render_header


class RegionPackTests(unittest.TestCase):
    def test_nl_is_the_single_shared_definition(self):
        region = get_region("nl")
        self.assertEqual("NL", region.code)
        self.assertEqual(("NL",), region.countries)
        self.assertEqual(build_region_pack.REGION_HEADER,
                         build_large_airports.REGION_HEADER)

    def test_unknown_region_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unknown region"):
            get_region("XX")

    def test_generated_header_contains_portable_metadata(self):
        header = render_header(get_region("NL"))
        self.assertIn('constexpr char kCode[] = "NL";', header)

    def test_generators_target_core_data_locations(self):
        self.assertEqual("include/core/large_airports.h",
                         str(build_large_airports.OUT_H.relative_to(build_large_airports.ROOT)))
        self.assertEqual("src/core/large_airports_data.cpp",
                         str(build_large_airports.OUT_CPP.relative_to(build_large_airports.ROOT)))


if __name__ == "__main__":
    unittest.main()
