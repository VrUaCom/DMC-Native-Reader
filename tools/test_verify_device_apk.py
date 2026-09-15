#!/usr/bin/env python3
import unittest

import verify_device_apk as verifier


class VerifyDeviceApkPolicyTest(unittest.TestCase):
    def test_unique_zip_names_are_not_reported(self):
        self.assertEqual(
            verifier.find_duplicate_names([
                "AndroidManifest.xml",
                "classes.dex",
                "lib/arm64-v8a/libdmcviewer.so",
            ]),
            [],
        )

    def test_duplicate_zip_names_are_reported_once_and_sorted(self):
        self.assertEqual(
            verifier.find_duplicate_names([
                "classes.dex",
                "res/a.xml",
                "classes.dex",
                "lib/arm64-v8a/libdmcviewer.so",
                "res/a.xml",
                "classes.dex",
            ]),
            ["classes.dex", "res/a.xml"],
        )

    def test_growth_from_baseline_records_bytes_and_percent(self):
        self.assertEqual(
            verifier.growth_from_baseline(150, 100),
            {"bytes": 50, "percent": 50.0},
        )
        self.assertEqual(
            verifier.growth_from_baseline(75, 100),
            {"bytes": -25, "percent": -25.0},
        )


if __name__ == "__main__":
    unittest.main()
