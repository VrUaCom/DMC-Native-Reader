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

    def test_apk_and_installed_package_limits_are_four_mib(self):
        four_mib = 4 * 1024 * 1024
        self.assertEqual(verifier.MAX_APK_BYTES, four_mib)
        self.assertEqual(verifier.MAX_INSTALLED_PACKAGE_CODE_BYTES, four_mib)
        self.assertEqual(measure.MAX_INSTALLED_CODE_KIB, 4096)
        self.assertEqual(measure.MAX_INSTALLED_CODE_BYTES, four_mib)

    def test_historical_v26_growth_constants_are_not_acceptance_api(self):
        self.assertFalse(hasattr(verifier, "ACCEPTED_V26_APK_BYTES"))
        self.assertFalse(hasattr(verifier, "ACCEPTED_V26_NATIVE_BYTES"))
        self.assertFalse(hasattr(verifier, "growth_from_baseline"))

    def test_installed_footprint_parsing_is_fail_closed(self):
        paths = measure.parse_pm_paths(
            "package:/data/app/~~abc/pkg-xyz/base.apk\n"
            "package:/data/app/~~abc/pkg-xyz/split_config.arm64_v8a.apk\n"
        )
        self.assertEqual(
            measure.package_code_dir(paths),
            "/data/app/~~abc/pkg-xyz",
        )
        self.assertEqual(
            measure.parse_du_kib(
                "4096\t/data/app/~~abc/pkg-xyz\n",
                "/data/app/~~abc/pkg-xyz",
            ),
            4096,
        )
        self.assertLessEqual(4096 * 1024, measure.MAX_INSTALLED_CODE_BYTES)
        self.assertGreater(4097 * 1024, measure.MAX_INSTALLED_CODE_BYTES)

        with self.assertRaises(SystemExit):
            measure.parse_pm_paths("")
        with self.assertRaises(SystemExit):
            measure.package_code_dir([
                "/data/app/a/base.apk",
                "/data/app/b/split.apk",
            ])
        with self.assertRaises(SystemExit):
            measure.package_code_dir(["/data/user/0/pkg/base.apk"])
        with self.assertRaises(SystemExit):
            measure.parse_du_kib(
                "4096\t/data/app/other\n",
                "/data/app/expected",
            )

    def test_installed_version_parser(self):
        version_code, version_name = measure.parse_package_version(
            "  versionCode=33 minSdk=26 targetSdk=36\n"
            "  versionName=1.0.6\n"
        )
        self.assertEqual(version_code, "33")
        self.assertEqual(version_name, "1.0.6")


if __name__ == "__main__":
    unittest.main()
