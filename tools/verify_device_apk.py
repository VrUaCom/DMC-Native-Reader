"""Verify the modular 1.0.6/v33 DMC Native Reader device APK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zipfile

# Accepted v26 device APK baseline:
#   APK 597,665 bytes; one ARM64 libdmcviewer.so 1,676,448 bytes uncompressed.
# v33 keeps its single mmap-ready DSO uncompressed, so the APK is expected to
# be larger on disk, but it must still stay far below the rejected recovery
# chain. These bounds deliberately leave substantial feature headroom while
# making runtime duplication/bloat a hard build failure instead of a device-side
# surprise.
ACCEPTED_V26_APK_BYTES = 597_665
ACCEPTED_V26_NATIVE_BYTES = 1_676_448
MAX_APK_BYTES = 8 * 1024 * 1024
MAX_NATIVE_BYTES = 4 * 1024 * 1024
MAX_DEX_BYTES = 1024 * 1024


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def run(*command):
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)


def zip_data_offset(apk: Path, info: zipfile.ZipInfo) -> int:
    with apk.open("rb") as stream:
        stream.seek(info.header_offset)
        header = stream.read(30)
    require(len(header) == 30 and header[:4] == b"PK\x03\x04",
            "Invalid local ZIP header for native library")
    filename_len, extra_len = struct.unpack_from("<HH", header, 26)
    return info.header_offset + 30 + filename_len + extra_len


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--sdk", default=os.environ.get("ANDROID_SDK_ROOT") or
                        os.environ.get("ANDROID_HOME"))
    args = parser.parse_args()
    require(args.sdk, "Provide --sdk or ANDROID_SDK_ROOT")
    require(args.apk.stat().st_size <= MAX_APK_BYTES,
            f"APK exceeds modular size budget: {args.apk.stat().st_size} > {MAX_APK_BYTES}")

    build_tools = Path(args.sdk) / "build-tools/36.0.0"
    aapt2 = build_tools / "aapt2"
    apksigner = build_tools / "apksigner"

    badging = run(str(aapt2), "dump", "badging", str(args.apk))
    require("package: name='com.dmcrengine.nativereader'" in badging,
            "Wrong application ID")
    require("versionCode='33' versionName='1.0.6'" in badging,
            "Wrong release identity")
    require("native-code: 'arm64-v8a'" in badging, "Wrong ABI")

    manifest = run(str(aapt2), "dump", "xmltree", str(args.apk),
                   "--file", "AndroidManifest.xml")
    require(re.search(
        r"extractNativeLibs[^\n]*=(?:false\b|(?:\(type 0x12\))?0x00000000\b)",
        manifest), "extractNativeLibs must be false for the modular APK")

    signing = run(str(apksigner), "verify", "--verbose", "--print-certs",
                  str(args.apk))
    expected_signer = (
        "f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac")
    digests = re.findall(
        r"Signer #\d+ certificate SHA-256 digest: ([0-9a-f]+)", signing)
    require(digests == [expected_signer],
            "Stable device-test signing certificate mismatch")
    require("Verified using v2 scheme (APK Signature Scheme v2): true" in signing,
            "APK v2 signature missing")

    with zipfile.ZipFile(args.apk) as archive:
        require(archive.testzip() is None, "ZIP integrity failure")
        libs = sorted(
            n for n in archive.namelist()
            if n.startswith("lib/") and n.endswith(".so"))
        expected_libs = ["lib/arm64-v8a/libdmcviewer.so"]
        require(libs == expected_libs,
                "Modular APK must contain exactly one native DSO: libdmcviewer.so; "
                "found: " + ", ".join(libs))
        # Reject duplicates even if a malformed ZIP repeats the exact same path;
        # ZipFile.getinfo() would otherwise hide that architectural error.
        native_entries = [
            i for i in archive.infolist()
            if i.filename.startswith("lib/") and i.filename.endswith(".so")
        ]
        require(len(native_entries) == 1 and
                native_entries[0].filename == expected_libs[0],
                "APK must contain exactly one physical native library entry")
        require(not any(
            marker in name.lower()
            for name in archive.namelist()
            for marker in ("dmcshim", "dmccore00")),
            "Recovery shim/core duplicate leaked into canonical APK")

        native_info = native_entries[0]
        require(native_info.compress_type == zipfile.ZIP_STORED,
                "libdmcviewer.so must be stored uncompressed for direct mmap")
        require(native_info.file_size <= MAX_NATIVE_BYTES,
                f"Native DSO exceeds modular size budget: {native_info.file_size} > {MAX_NATIVE_BYTES}")
        native_offset = zip_data_offset(args.apk, native_info)
        require(native_offset % 16384 == 0,
                f"libdmcviewer.so is not 16 KiB ZIP-aligned: offset={native_offset}")
        native_bytes = archive.read(native_info)

        require("classes.dex" in archive.namelist(), "Java shell missing")
        dex_files = [i for i in archive.infolist() if i.filename.endswith(".dex")]
        dex_bytes = sum(i.file_size for i in dex_files)
        require(dex_bytes <= MAX_DEX_BYTES,
                f"Java shell exceeds size budget: {dex_bytes} > {MAX_DEX_BYTES}")
        require(not any(b"Lkotlin/" in archive.read(i) for i in dex_files),
                "Unexpected Kotlin runtime in Java-only shell")

    for forbidden in (b"libdmcshim", b"libdmccore00"):
        require(forbidden not in native_bytes,
                "Recovery runtime dependency leaked into libdmcviewer.so")

    for marker in (
        "spider.crusader", "native.texture-set", "native.uv-projection",
        "formats.mod.mesh-reader", "formats.scm.mesh-reader",
        "formats.texture.spider-reader", "formats.evt.structural-reader"):
        require(marker.encode() in native_bytes, "Missing native module: " + marker)

    root = Path(__file__).resolve().parents[1]
    bridge = (root / "app/src/main/java/com/dmcrengine/nativeviewer/NativeBridge.java").read_text()
    native_cpp = (root / "app/src/main/cpp/app_native.cpp").read_text()
    cmake = (root / "app/src/main/cpp/CMakeLists.txt").read_text()

    require("import android.graphics.Bitmap;" in bridge,
            "NativeBridge must use android.graphics.Bitmap")
    for required_method in (
        "composeMods", "compositePartCount", "compositePartName",
        "attachPtxToPart", "render"):
        require(re.search(r"\bnative\b[^;]*\b" + re.escape(required_method) +
                          r"\s*\(", bridge, re.DOTALL),
                "Current Java/JNI API missing method: " + required_method)

    require("AndroidBitmap_lockPixels" in native_cpp and
            "AndroidBitmap_unlockPixels" in native_cpp,
            "Direct-Bitmap JNI path missing")
    require("dlopen(" not in native_cpp and "dlsym(" not in native_cpp,
            "Runtime shim delegation is forbidden in canonical JNI source")

    android_link = re.search(
        r"target_link_libraries\s*\(\s*dmcviewer\s+PRIVATE(?P<body>.*?)\)",
        cmake, re.DOTALL)
    core_link = re.search(
        r"target_link_libraries\s*\(\s*dmc_native_reader_core(?P<body>.*?)\)",
        cmake, re.DOTALL)
    require(android_link and "DMCNativeReader::Core" in android_link.group("body") and
            "jnigraphics" in android_link.group("body"),
            "Android JNI target must link the portable core and jnigraphics")
    require(core_link and "DMCRengine::ReaderCore" in core_link.group("body"),
            "Portable core must statically consume canonical Rengine ReaderCore")

    methods = re.findall(r"public\s+static\s+native\s+\S+\s+(\w+)\s*\(", bridge)
    with tempfile.TemporaryDirectory() as temp:
        native_path = Path(temp) / "libdmcviewer.so"
        native_path.write_bytes(native_bytes)
        symbols = run("readelf", "--dyn-syms", "--wide", str(native_path))
        header = run("readelf", "-h", str(native_path))
        require("AArch64" in header, "Native library is not AArch64")
        exports = {
            line.split()[-1] for line in symbols.splitlines()
            if len(line.split()) >= 8 and line.split()[4] in ("GLOBAL", "WEAK")
            and line.split()[5] == "DEFAULT" and line.split()[6] != "UND"
        }
        expected_exports = {
            "Java_com_dmcrengine_nativeviewer_NativeBridge_" + method
            for method in methods
        }
        require(exports == expected_exports,
                "Native public ABI and NativeBridge.java are out of sync")

    print(json.dumps({
        "apk": str(args.apk),
        "versionName": "1.0.6",
        "versionCode": 33,
        "abi": "arm64-v8a",
        "signer_sha256": expected_signer,
        "sha256": hashlib.sha256(args.apk.read_bytes()).hexdigest(),
        "native_dso_count": 1,
        "native_dso": "lib/arm64-v8a/libdmcviewer.so",
        "native_zip_alignment": 16384,
        "extractNativeLibs": False,
        "jni_exports_checked": len(methods),
        "scm_authority": "dmc-rengine-main-809824882c60487962e99ee41f16bca7e3ccbc83",
        "zip_integrity": "pass",
        "apk_bytes": args.apk.stat().st_size,
        "native_bytes": len(native_bytes),
        "dex_bytes": dex_bytes,
        "accepted_v26_apk_bytes": ACCEPTED_V26_APK_BYTES,
        "accepted_v26_native_bytes": ACCEPTED_V26_NATIVE_BYTES,
        "max_apk_bytes": MAX_APK_BYTES,
        "max_native_bytes": MAX_NATIVE_BYTES,
        "max_dex_bytes": MAX_DEX_BYTES,
        "modular_native_architecture": "pass",
        "device_test": "pending",
    }, indent=2))


if __name__ == "__main__":
    main()
