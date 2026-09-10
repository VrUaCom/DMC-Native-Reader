"""Verify the 1.0/v25 device APK, not production signing or device behaviour."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def run(*command):
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--sdk", default=os.environ.get("ANDROID_SDK_ROOT") or
                        os.environ.get("ANDROID_HOME"), required=False)
    args = parser.parse_args()
    require(args.sdk, "Provide --sdk or ANDROID_SDK_ROOT")
    build_tools = Path(args.sdk) / "build-tools/36.0.0"
    badging = run(str(build_tools / "aapt2"), "dump", "badging", str(args.apk))
    require("package: name='com.dmcrengine.nativereader'" in badging, "Wrong application ID")
    require("versionCode='25' versionName='1.0'" in badging, "Wrong release identity")
    require("native-code: 'arm64-v8a'" in badging, "Wrong ABI")
    manifest = run(str(build_tools / "aapt2"), "dump", "xmltree", str(args.apk),
                   "--file", "AndroidManifest.xml")
    require(re.search(r"extractNativeLibs[^\n]*=(?:true\b|(?:\(type 0x12\))?0xffffffff\b)", manifest),
            "extractNativeLibs must be true")
    signing = run(str(build_tools / "apksigner"), "verify", "--verbose",
                  "--print-certs", str(args.apk))
    expected = "f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac"
    digests = re.findall(r"Signer #\d+ certificate SHA-256 digest: ([0-9a-f]+)", signing)
    require(digests == [expected], "Stable device-test signing certificate mismatch")
    require("Verified using v2 scheme (APK Signature Scheme v2): true" in signing,
            "APK v2 signature missing")
    with zipfile.ZipFile(args.apk) as archive:
        require(archive.testzip() is None, "ZIP integrity failure")
        libs = [n for n in archive.namelist() if n.startswith("lib/") and n.endswith(".so")]
        require(libs and all(n.startswith("lib/arm64-v8a/") for n in libs), "Unexpected native ABI")
        native = archive.getinfo("lib/arm64-v8a/libdmcviewer.so")
        require(native.compress_type == zipfile.ZIP_DEFLATED, "Expected legacy JNI packaging")
        require("classes.dex" in archive.namelist(), "Java shell missing")
        dex_files = [i for i in archive.infolist() if i.filename.endswith(".dex")]
        dex_bytes = sum(i.file_size for i in dex_files)
        require(not any(b"Lkotlin/" in archive.read(i) for i in dex_files),
                "Unexpected Kotlin runtime in the Java-only shell")
        library = archive.read(native)
    for marker in ("spider.crusader", "native.texture-set", "native.uv-projection",
                   "formats.mod.mesh-reader", "formats.scm.mesh-reader",
                   "formats.texture.spider-reader"):
        require(marker.encode() in library, "Missing native module: " + marker)
    root = Path(__file__).resolve().parents[1]
    bridge = (root / "app/src/main/java/com/dmcrengine/nativeviewer/NativeBridge.java").read_text()
    methods = re.findall(r"public static native\s+\S+\s+(\w+)\(", bridge)
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "libdmcviewer.so"
        path.write_bytes(library)
        symbols = run("readelf", "--dyn-syms", "--wide", str(path))
        header = run("readelf", "-h", str(path))
        require("AArch64" in header, "Native library is not AArch64")
        exports = {line.split()[-1] for line in symbols.splitlines()
                   if len(line.split()) >= 8 and line.split()[4] in ("GLOBAL", "WEAK")
                   and line.split()[5] == "DEFAULT" and line.split()[6] != "UND"}
        expected_exports = {"Java_com_dmcrengine_nativeviewer_NativeBridge_" + m for m in methods}
        require(exports == expected_exports, "Native public ABI must contain only declared JNI exports")
        for method in methods:
            symbol = "Java_com_dmcrengine_nativeviewer_NativeBridge_" + method
            require(any(symbol == line.split()[-1] and " UND " not in line
                        for line in symbols.splitlines() if line.split()),
                    "Missing JNI export: " + method)
    print(json.dumps({"apk": str(args.apk), "versionName": "1.0", "versionCode": 25,
                      "abi": "arm64-v8a", "signer_sha256": expected,
                      "sha256": hashlib.sha256(args.apk.read_bytes()).hexdigest(),
                      "jni_exports_checked": len(methods), "zip_integrity": "pass",
                      "apk_bytes": args.apk.stat().st_size,
                      "native_bytes": len(library), "dex_bytes": dex_bytes,
                      "public_native_exports": len(exports), "kotlin_runtime": "absent",
                      "legacy_packaging": "pass", "device_test": "pending"}, indent=2))


if __name__ == "__main__":
    main()
