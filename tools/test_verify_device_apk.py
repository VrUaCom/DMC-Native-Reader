#!/usr/bin/env python3
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import unittest

import verify_device_apk as verifier

MEASURE_PATH = Path(__file__).with_name("measure_installed_footprint.py")
_measure_spec = spec_from_file_location("measure_installed_footprint", MEASURE_PATH)
assert _measure_spec is not None and _measure_spec.loader is not None
measure = module_from_spec(_measure_spec)
_measure_spec.loader.exec_module(measure)


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

    def test_apk_and_installed_app_limits_are_four_mib(self):
        four_mib = 4 * 1024 * 1024
        self.assertEqual(verifier.MAX_APK_BYTES, four_mib)
        self.assertEqual(verifier.MAX_INSTALLED_PACKAGE_CODE_BYTES, four_mib)
        self.assertEqual(measure.MAX_INSTALLED_APP_BYTES, four_mib)

    def test_historical_v26_growth_constants_are_not_acceptance_api(self):
        self.assertFalse(hasattr(verifier, "ACCEPTED_V26_APK_BYTES"))
        self.assertFalse(hasattr(verifier, "ACCEPTED_V26_NATIVE_BYTES"))
        self.assertFalse(hasattr(verifier, "growth_from_baseline"))

    def test_installed_storage_stats_parser(self):
        stats = measure.parse_storage_stats(
            "code: 4194304 bytes (4 Mb)\n"
            "data: 12345 bytes (12 Kb)\n"
            "cache: 2048 bytes (2 Kb)\n"
            "apk: 3000000 bytes (2.8 Mb)\n"
            "lib: 0 bytes\n"
            "dexopt artifacts: 1194304 bytes (1.1 Mb)\n"
        )
        self.assertEqual(stats["code"], 4 * 1024 * 1024)
        self.assertEqual(stats["data"], 12345)
        self.assertEqual(stats["cache"], 2048)
        self.assertEqual(stats["dexopt_artifacts"], 1194304)
        self.assertLessEqual(stats["code"], measure.MAX_INSTALLED_APP_BYTES)
        self.assertGreater(4194305, measure.MAX_INSTALLED_APP_BYTES)

    def test_storage_stats_parser_fails_closed_without_code_bytes(self):
        with self.assertRaises(SystemExit):
            measure.parse_storage_stats(
                "data: 123 bytes\ncache: 0 bytes\n"
            )
        with self.assertRaises(SystemExit):
            measure.parse_storage_stats(
                "Error: get_package_storage_stats flag is not enabled\n"
            )

    def test_installed_package_requires_one_base_apk(self):
        paths = measure.parse_pm_paths(
            "package:/data/app/~~abc/pkg-xyz/base.apk\n"
        )
        self.assertEqual(
            measure.require_single_base_apk(paths),
            "/data/app/~~abc/pkg-xyz/base.apk",
        )
        with self.assertRaises(SystemExit):
            measure.require_single_base_apk([
                "/data/app/a/base.apk",
                "/data/app/a/split_config.arm64_v8a.apk",
            ])
        with self.assertRaises(SystemExit):
            measure.require_single_base_apk([
                "/data/app/a/not-base.apk",
            ])
        with self.assertRaises(SystemExit):
            measure.parse_pm_paths("")

    def test_installed_version_parser(self):
        version_code, version_name = measure.parse_package_version(
            "  versionCode=33 minSdk=26 targetSdk=36\n"
            "  versionName=1.0.6\n"
        )
        self.assertEqual(version_code, "33")
        self.assertEqual(version_name, "1.0.6")


if __name__ == "__main__":
    unittest.main()
