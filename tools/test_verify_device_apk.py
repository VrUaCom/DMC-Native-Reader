#!/usr/bin/env python3
from importlib.util import module_from_spec, spec_from_file_location
import io
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

import verify_device_apk as verifier

MEASURE_PATH = Path(__file__).with_name("measure_installed_footprint.py")
_measure_spec = spec_from_file_location("measure_installed_footprint", MEASURE_PATH)
assert _measure_spec is not None and _measure_spec.loader is not None
measure = module_from_spec(_measure_spec)
_measure_spec.loader.exec_module(measure)


def make_test_zip() -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr("classes.dex", b"dex\n")
    return output.getvalue()


def add_empty_apk_signing_block(apk_bytes: bytes) -> bytes:
    data = bytearray(apk_bytes)
    eocd_offset = verifier.find_eocd_offset(data)
    central_dir_offset = struct.unpack_from("<I", data, eocd_offset + 16)[0]
    block_size = 24
    signing_block = (
        struct.pack("<Q", block_size)
        + struct.pack("<Q", block_size)
        + verifier.APK_SIGNING_BLOCK_MAGIC
    )
    data[central_dir_offset:central_dir_offset] = signing_block
    shifted_eocd = eocd_offset + len(signing_block)
    struct.pack_into(
        "<I",
        data,
        shifted_eocd + 16,
        central_dir_offset + len(signing_block),
    )
    return bytes(data)


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
        self.assertEqual(verifier.MAX_INSTALLED_APP_BYTES, four_mib)
        self.assertEqual(measure.MAX_INSTALLED_APP_BYTES, four_mib)

    def test_historical_v26_growth_constants_are_not_acceptance_api(self):
        self.assertFalse(hasattr(verifier, "ACCEPTED_V26_APK_BYTES"))
        self.assertFalse(hasattr(verifier, "ACCEPTED_V26_NATIVE_BYTES"))
        self.assertFalse(hasattr(verifier, "growth_from_baseline"))

    def test_stable_debug_signing_policy(self):
        signing = (
            "Signer #1 certificate SHA-256 digest: "
            + verifier.EXPECTED_DEBUG_SIGNER_SHA256
            + "\nVerified using v2 scheme (APK Signature Scheme v2): true\n"
        )
        signed, digest = verifier.validate_signing_result(
            verifier.SIGNING_STABLE_DEBUG, 0, signing)
        self.assertTrue(signed)
        self.assertEqual(digest, verifier.EXPECTED_DEBUG_SIGNER_SHA256)

        with self.assertRaises(SystemExit):
            verifier.validate_signing_result(
                verifier.SIGNING_STABLE_DEBUG,
                1,
                "DOES NOT VERIFY\n",
            )

    def test_unsigned_release_signing_policy(self):
        signed, digest = verifier.validate_signing_result(
            verifier.SIGNING_UNSIGNED_RELEASE,
            1,
            "DOES NOT VERIFY\n",
        )
        self.assertFalse(signed)
        self.assertIsNone(digest)

        with self.assertRaises(SystemExit):
            verifier.validate_signing_result(
                verifier.SIGNING_UNSIGNED_RELEASE,
                0,
                "Signer #1 certificate SHA-256 digest: "
                + verifier.EXPECTED_DEBUG_SIGNER_SHA256
                + "\nVerified using v2 scheme (APK Signature Scheme v2): true\n",
            )

    def test_apk_signing_block_detection_distinguishes_unsigned_structure(self):
        unsigned = make_test_zip()
        signed = add_empty_apk_signing_block(unsigned)
        with tempfile.TemporaryDirectory() as temp:
            unsigned_path = Path(temp) / "unsigned.apk"
            signed_path = Path(temp) / "signed.apk"
            unsigned_path.write_bytes(unsigned)
            signed_path.write_bytes(signed)
            self.assertFalse(verifier.has_apk_signing_block(unsigned_path))
            self.assertTrue(verifier.has_apk_signing_block(signed_path))

    def test_jar_signature_material_detection(self):
        self.assertEqual(
            verifier.find_jar_signature_entries([
                "META-INF/MANIFEST.MF",
                "META-INF/CERT.SF",
                "META-INF/CERT.RSA",
                "META-INF/other.txt",
                "classes.dex",
            ]),
            ["META-INF/CERT.RSA", "META-INF/CERT.SF"],
        )
        self.assertEqual(
            verifier.find_jar_signature_entries([
                "META-INF/MANIFEST.MF",
                "classes.dex",
            ]),
            [],
        )

    def test_android_user_scope_is_explicit_and_single_user(self):
        self.assertEqual(measure.normalize_user_arg("current"), "current")
        self.assertEqual(measure.normalize_user_arg("0"), "0")
        self.assertEqual(measure.normalize_user_arg("010"), "10")
        for invalid in ("all", "-1", "owner", ""):
            with self.subTest(invalid=invalid):
                with self.assertRaises(SystemExit):
                    measure.normalize_user_arg(invalid)

    def test_android_user_scope_is_applied_to_both_pm_commands(self):
        self.assertEqual(
            measure.package_path_command(
                "adb", "SERIAL", "current", "com.dmcrengine.nativereader"),
            [
                "adb", "-s", "SERIAL", "shell", "pm", "path",
                "--user", "current", "com.dmcrengine.nativereader",
            ],
        )
        self.assertEqual(
            measure.storage_stats_command(
                "adb", "SERIAL", "current", "com.dmcrengine.nativereader"),
            [
                "adb", "-s", "SERIAL", "shell", "pm",
                "get-package-storage-stats", "--user", "current",
                "com.dmcrengine.nativereader",
            ],
        )
        self.assertEqual(
            measure.package_path_command("adb", "SERIAL", "10", "pkg"),
            [
                "adb", "-s", "SERIAL", "shell", "pm", "path",
                "--user", "10", "pkg",
            ],
        )
        self.assertEqual(
            measure.storage_stats_command("adb", "SERIAL", "10", "pkg"),
            [
                "adb", "-s", "SERIAL", "shell", "pm",
                "get-package-storage-stats", "--user", "10", "pkg",
            ],
        )

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
