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

    def test_installed_package_code_limit_is_four_mib(self):
        self.assertEqual(
            verifier.MAX_INSTALLED_PACKAGE_CODE_BYTES,
            4 * 1024 * 1024,
        )

    def test_historical_v26_growth_constants_are_not_acceptance_api(self):
        self.assertFalse(hasattr(verifier, "ACCEPTED_V26_APK_BYTES"))
        self.assertFalse(hasattr(verifier, "ACCEPTED_V26_NATIVE_BYTES"))
        self.assertFalse(hasattr(verifier, "growth_from_baseline"))


if __name__ == "__main__":
    unittest.main()
