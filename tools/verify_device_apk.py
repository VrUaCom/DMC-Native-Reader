"""Verify the modular 1.0.38/v65 DMC Native Reader APK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile

MAX_APK_BYTES = 4 * 1024 * 1024
MAX_NATIVE_BYTES = 4 * 1024 * 1024
MAX_DEX_BYTES = 1024 * 1024
MAX_INSTALLED_APP_BYTES = 4 * 1024 * 1024
DUPLICATE_PAYLOAD_MIN_BYTES = 64 * 1024
NDK_VERSION = "30.0.16248370"
CPP_STANDARD = "C++23"
CPP_STANDARD_AUTHORITY = "cmake-target-scoped"
SPIDER_CPP_PROFILE = "spider.cpp23"
PAGE_ALIGNMENT = 16 * 1024
RENGINE_PIN = "caf445226c7d61841292384a10e93e4f58ae29f9"
SCM_AUTHORITY_BASE = "809824882c60487962e99ee41f16bca7e3ccbc83"
SIGNING_STABLE_DEBUG = "stable-debug"
SIGNING_UNSIGNED_RELEASE = "unsigned-release"
EXPECTED_DEBUG_SIGNER_SHA256 = (
    "f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac")
APK_SIGNING_BLOCK_MAGIC = b"APK Sig Block 42"
ZIP_EOCD_SIGNATURE = b"PK\x05\x06"
ZIP_EOCD_MIN_SIZE = 22
ZIP_MAX_COMMENT_SIZE = 65535


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def run(*command):
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)


def validate_signing_result(policy, returncode, output):
    """Validate apksigner output without weakening package verification.

    Debug/device-test APKs must carry the stable public test signer and a valid
    v2 signature. Canonical release evidence is deliberately unsigned until the
    external production-signing authority runs. Structural unsigned checks are
    performed separately so a broken signature cannot masquerade as unsigned.
    """
    digests = re.findall(
        r"Signer #\d+ certificate SHA-256 digest: ([0-9a-f]+)", output)
    v2_verified = (
        "Verified using v2 scheme (APK Signature Scheme v2): true" in output)

    if policy == SIGNING_STABLE_DEBUG:
        require(returncode == 0, "Stable device-test APK signature verification failed")
        require(digests == [EXPECTED_DEBUG_SIGNER_SHA256],
                "Stable device-test signing certificate mismatch")
        require(v2_verified, "APK v2 signature missing")
        return True, EXPECTED_DEBUG_SIGNER_SHA256

    if policy == SIGNING_UNSIGNED_RELEASE:
        require(returncode != 0,
                "Unsigned release APK unexpectedly verifies as signed")
        require(not digests,
                "Unsigned release APK unexpectedly exposes signer certificate")
        require(not v2_verified,
                "Unsigned release APK unexpectedly reports a valid v2 signature")
        return False, None

    raise SystemExit("Unknown signing policy: " + str(policy))


def find_eocd_offset(apk_bytes: bytes) -> int:
    """Locate the non-ZIP64 EOCD record using its exact trailing comment size."""
    if len(apk_bytes) < ZIP_EOCD_MIN_SIZE:
        raise SystemExit("APK is too small to contain a ZIP EOCD record")
    lower_bound = max(
        0, len(apk_bytes) - ZIP_EOCD_MIN_SIZE - ZIP_MAX_COMMENT_SIZE)
    for offset in range(len(apk_bytes) - ZIP_EOCD_MIN_SIZE, lower_bound - 1, -1):
        if apk_bytes[offset:offset + 4] != ZIP_EOCD_SIGNATURE:
            continue
        comment_size = struct.unpack_from("<H", apk_bytes, offset + 20)[0]
        if offset + ZIP_EOCD_MIN_SIZE + comment_size == len(apk_bytes):
            return offset
    raise SystemExit("APK ZIP EOCD record not found")


def has_apk_signing_block(apk: Path) -> bool:
    """Return whether a structurally valid APK Signing Block precedes the ZIP CD."""
    apk_bytes = apk.read_bytes()
    eocd_offset = find_eocd_offset(apk_bytes)
    central_dir_offset = struct.unpack_from("<I", apk_bytes, eocd_offset + 16)[0]
    require(central_dir_offset != 0xffffffff,
            "ZIP64 central-directory offset is unsupported for canonical APK evidence")
    require(central_dir_offset <= len(apk_bytes),
            "ZIP central-directory offset lies beyond APK bytes")
    if central_dir_offset < 24:
        return False

    footer = apk_bytes[central_dir_offset - 24:central_dir_offset]
    if footer[8:] != APK_SIGNING_BLOCK_MAGIC:
        return False

    block_size = struct.unpack_from("<Q", footer, 0)[0]
    total_size = block_size + 8
    require(block_size >= 24,
            "APK Signing Block footer has an invalid size")
    require(total_size <= central_dir_offset,
            "APK Signing Block extends before the start of the APK")
    block_offset = central_dir_offset - total_size
    header_size = struct.unpack_from("<Q", apk_bytes, block_offset)[0]
    require(header_size == block_size,
            "APK Signing Block header/footer sizes disagree")
    return True


def find_jar_signature_entries(names):
    """Return v1/JAR signature material, excluding an unsigned manifest alone."""
    signature_suffixes = (".SF", ".RSA", ".DSA", ".EC")
    result = []
    for name in names:
        upper = name.upper()
        if upper.startswith("META-INF/") and upper.endswith(signature_suffixes):
            result.append(name)
    return sorted(result)


def find_duplicate_names(names):
    """Return sorted entry names that appear more than once."""
    seen = set()
    duplicates = set()
    for name in names:
        if name in seen:
            duplicates.add(name)
        else:
            seen.add(name)
    return sorted(duplicates)


def zip_data_offset(apk: Path, info: zipfile.ZipInfo) -> int:
    with apk.open("rb") as stream:
        stream.seek(info.header_offset)
        header = stream.read(30)
    require(len(header) == 30 and header[:4] == b"PK\x03\x04",
            "Invalid local ZIP header for native library")
    filename_len, extra_len = struct.unpack_from("<HH", header, 26)
    return info.header_offset + 30 + filename_len + extra_len


def find_elf_reader(sdk: Path) -> str:
    ndk_readers = sorted((sdk / "ndk" / NDK_VERSION / "toolchains" / "llvm" /
                          "prebuilt").glob("*/bin/llvm-readelf"))
    if ndk_readers:
        return str(ndk_readers[0])
    for candidate in ("llvm-readelf", "readelf"):
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
    raise SystemExit("No llvm-readelf/readelf available for native ABI verification")


def load_segment_alignments(elf_reader: str, native_path: Path):
    program_headers = run(
        elf_reader, "--program-headers", "--wide", str(native_path))
    alignments = []
    for line in program_headers.splitlines():
        fields = line.split()
        if not fields or fields[0] != "LOAD":
            continue
        try:
            alignments.append(int(fields[-1], 0))
        except ValueError as error:
            raise SystemExit(
                f"Could not parse PT_LOAD alignment from: {line}") from error
    require(alignments, "Native library exposes no PT_LOAD segments")
    return alignments


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--sdk", default=os.environ.get("ANDROID_SDK_ROOT") or
                        os.environ.get("ANDROID_HOME"))
    parser.add_argument(
        "--signing-policy",
        choices=(SIGNING_STABLE_DEBUG, SIGNING_UNSIGNED_RELEASE),
        default=SIGNING_STABLE_DEBUG,
        help="Expected APK signing state for this artifact.",
    )
    args = parser.parse_args()
    require(args.sdk, "Provide --sdk or ANDROID_SDK_ROOT")
    sdk = Path(args.sdk)
    apk_bytes = args.apk.stat().st_size
    require(apk_bytes <= MAX_APK_BYTES,
            f"APK exceeds modular size budget: {apk_bytes} > {MAX_APK_BYTES}")

    build_tools = sdk / "build-tools/36.0.0"
    aapt2 = build_tools / "aapt2"
    apksigner = build_tools / "apksigner"
    elf_reader = find_elf_reader(sdk)

    badging = run(str(aapt2), "dump", "badging", str(args.apk))
    require("package: name='com.dmcrengine.nativereader'" in badging,
            "Wrong application ID")
    require("versionCode='68' versionName='1.0.41'" in badging,
            "Wrong release identity")
    require("native-code: 'arm64-v8a'" in badging, "Wrong ABI")

    manifest = run(str(aapt2), "dump", "xmltree", str(args.apk),
                   "--file", "AndroidManifest.xml")
    require(re.search(
        r"extractNativeLibs[^\n]*=(?:false\b|(?:\(type 0x12\))?0x0+\b)",
        manifest), "extractNativeLibs must be false for the modular APK")

    signing_process = subprocess.run(
        [str(apksigner), "verify", "--verbose", "--print-certs", str(args.apk)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    signing = signing_process.stdout or ""
    signed, signer_sha256 = validate_signing_result(
        args.signing_policy, signing_process.returncode, signing)
    apk_signing_block_present = has_apk_signing_block(args.apk)

    duplicate_entry_names = []
    duplicate_large_payload_groups = []
    duplicate_large_payload_waste_bytes = 0
    largest_entries = []
    jar_signature_entries = []

    with zipfile.ZipFile(args.apk) as archive:
        require(archive.testzip() is None, "ZIP integrity failure")
        entries = archive.infolist()
        entry_names = [info.filename for info in entries]
        duplicate_entry_names = find_duplicate_names(entry_names)
        require(not duplicate_entry_names,
                "APK contains duplicate ZIP entry names: " +
                ", ".join(duplicate_entry_names))

        jar_signature_entries = find_jar_signature_entries(entry_names)
        if args.signing_policy == SIGNING_STABLE_DEBUG:
            require(apk_signing_block_present,
                    "v2-signed debug APK has no structurally valid APK Signing Block")
        elif args.signing_policy == SIGNING_UNSIGNED_RELEASE:
            require(not apk_signing_block_present,
                    "Unsigned release APK still contains an APK Signing Block")
            require(not jar_signature_entries,
                    "Unsigned release APK still contains JAR signature material: " +
                    ", ".join(jar_signature_entries))

        libs = sorted(
            name for name in entry_names
            if name.startswith("lib/") and name.endswith(".so"))
        expected_libs = ["lib/arm64-v8a/libdmcviewer.so"]
        require(libs == expected_libs,
                "Modular APK must contain exactly one native DSO: libdmcviewer.so; "
                "found: " + ", ".join(libs))
        native_entries = [
            info for info in entries
            if info.filename.startswith("lib/") and info.filename.endswith(".so")
        ]
        require(len(native_entries) == 1 and
                native_entries[0].filename == expected_libs[0],
                "APK must contain exactly one physical native library entry")
        require(not any(
            marker in name.lower()
            for name in entry_names
            for marker in ("dmcshim", "dmccore00")),
            "Recovery shim/core duplicate leaked into canonical APK")

        native_info = native_entries[0]
        require(native_info.compress_type == zipfile.ZIP_STORED,
                "libdmcviewer.so must be stored uncompressed for direct mmap")
        require(native_info.file_size <= MAX_NATIVE_BYTES,
                f"Native DSO exceeds modular size budget: {native_info.file_size} > {MAX_NATIVE_BYTES}")
        native_offset = zip_data_offset(args.apk, native_info)
        require(native_offset % PAGE_ALIGNMENT == 0,
                f"libdmcviewer.so is not 16 KiB ZIP-aligned: offset={native_offset}")
        native_bytes = archive.read(native_info)

        require("classes.dex" in entry_names, "Java shell missing")
        dex_files = [info for info in entries if info.filename.endswith(".dex")]
        dex_bytes = sum(info.file_size for info in dex_files)
        require(dex_bytes <= MAX_DEX_BYTES,
                f"Java shell exceeds size budget: {dex_bytes} > {MAX_DEX_BYTES}")
        require(not any(b"Lkotlin/" in archive.read(info) for info in dex_files),
                "Unexpected Kotlin runtime in Java-only shell")

        payload_groups = {}
        for info in entries:
            if info.is_dir() or info.file_size < DUPLICATE_PAYLOAD_MIN_BYTES:
                continue
            if info.filename.startswith("META-INF/"):
                continue
            digest = hashlib.sha256(archive.read(info)).hexdigest()
            payload_groups.setdefault((digest, info.file_size), []).append(info.filename)

        for (digest, size), names in sorted(payload_groups.items()):
            if len(names) < 2:
                continue
            group = {
                "sha256": digest,
                "bytes_each": size,
                "entries": sorted(names),
                "wasted_duplicate_bytes": size * (len(names) - 1),
            }
            duplicate_large_payload_groups.append(group)
            duplicate_large_payload_waste_bytes += group["wasted_duplicate_bytes"]

        runtime_duplicate_groups = [
            group for group in duplicate_large_payload_groups
            if any(name.endswith((".so", ".dex")) for name in group["entries"])
        ]
        require(not runtime_duplicate_groups,
                "Duplicate large runtime payload detected in APK: " +
                json.dumps(runtime_duplicate_groups, sort_keys=True))

        largest_entries = [
            {
                "entry": info.filename,
                "uncompressed_bytes": info.file_size,
                "compressed_bytes": info.compress_size,
            }
            for info in sorted(
                (info for info in entries if not info.is_dir()),
                key=lambda item: item.file_size,
                reverse=True)[:12]
        ]

    for forbidden in (b"libdmcshim", b"libdmccore00"):
        require(forbidden not in native_bytes,
                "Recovery runtime dependency leaked into libdmcviewer.so")

    for marker in (
        "spider.crusader", SPIDER_CPP_PROFILE,
        "native.texture-set", "native.uv-projection",
        "formats.mod.mesh-reader", "formats.scm.mesh-reader",
        "formats.texture.spider-reader", "formats.evt.structural-reader"):
        require(marker.encode() in native_bytes, "Missing native module/profile: " + marker)

    root = Path(__file__).resolve().parents[1]
    bridge = (root / "app/src/main/java/com/dmcrengine/nativeviewer/NativeBridge.java").read_text()
    native_cpp = (root / "app/src/main/cpp/app_native.cpp").read_text()
    cmake = (root / "app/src/main/cpp/CMakeLists.txt").read_text()
    app_gradle = (root / "app/build.gradle.kts").read_text()
    cpp23_profile = (
        root / "app/src/main/cpp/include/dmcresource/cpp23_profile.h").read_text()
    spider_cpp = (
        root / "app/src/main/cpp/include/dmcresource/spider/cpp23_language.h").read_text()
    workspace_graph = (
        root / "app/src/main/cpp/include/dmcresource/workspace_graph.h").read_text()
    resource_session_cpp = (
        root / "app/src/main/cpp/modules/resource_session.cpp").read_text()

    require("cxx_std_23" in cmake and "cxx_std_20" not in cmake,
            "Native Reader CMake targets must use canonical C++23 only")
    require("CXX_STANDARD 23" in cmake and
            "CXX_STANDARD_REQUIRED ON" in cmake and
            "CXX_EXTENSIONS OFF" in cmake,
            "Native Reader targets must require strict ISO C++23")
    require("DMC_NATIVE_READER_CPP23=1" in cmake and
            "DMC_NATIVE_READER_SPIDER_CPP=1" in cmake,
            "C++23 / Spider C++ compile definitions missing")
    require("DMC_NATIVE_READER_CORE_SOURCES" in cmake and
            "Duplicate source entry in DMC_NATIVE_READER_CORE_SOURCES" in cmake,
            "CMake must reject duplicate Native Reader core sources")
    require("DMC_NATIVE_READER_TESTS" in cmake and
            "Duplicate test entry in DMC_NATIVE_READER_TESTS" in cmake,
            "CMake must reject duplicate Native Reader test entries")
    require('ndkVersion = "' + NDK_VERSION + '"' in app_gradle,
            "Android Gradle NDK pin does not match canonical r30 LTS")
    require("-std=c++" not in app_gradle,
            "Gradle must not own C++ language mode; use target-scoped CMake")
    require("DMC Native Reader product core requires C++23" in cpp23_profile and
            "__cpp_lib_expected < 202202L" in cpp23_profile and
            "std::expected" in cpp23_profile and
            "dmc.native-reader.cpp23" in cpp23_profile,
            "C++23 compile/profile contract missing")
    require(SPIDER_CPP_PROFILE in spider_cpp and
            '"dmcresource/spider/crusader.h"' in spider_cpp,
            "Spider C++23 profile must remain a typed layer over Crusader")
    require("WorkspaceResult" in workspace_graph and
            "WorkspaceGraphError" in workspace_graph,
            "WorkspaceGraph has not migrated to typed C++23 results")

    rengine_path = root / "app/src/main/cpp/vendor/dmc-rengine-cpp"
    rengine_gitlink = run(
        "git", "-C", str(root), "rev-parse",
        "HEAD:app/src/main/cpp/vendor/dmc-rengine-cpp").strip()
    require(rengine_gitlink == RENGINE_PIN,
            "DMC Rengine gitlink does not match the canonical v33 pin: " +
            rengine_gitlink)
    rengine_checkout = run(
        "git", "-C", str(rengine_path), "rev-parse", "HEAD").strip()
    require(rengine_checkout == RENGINE_PIN,
            "Checked-out DMC Rengine submodule does not match the canonical v33 pin: " +
            rengine_checkout)

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
    require("spider::actions::compose_mod_sessions" in native_cpp and
            "spider::actions::attach_ptx" in native_cpp,
            "JNI composition/attachment must route through Spider actions")
    require("attach_session_ptx(" not in resource_session_cpp and
            "attach_session_part_ptx(" not in resource_session_cpp,
            "Obsolete direct Session PTX attachment implementation reappeared")

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
    for required_source in (
        "modules/composite_builder.cpp",
        "modules/composite_placement.cpp",
        "modules/mod_attachment_resolver.cpp",
        "modules/workspace_graph.cpp",
        "spider/session_compose_actions.cpp",
        "spider/session_texture_actions.cpp"):
        require(required_source in cmake,
                "Portable core missing modular source: " + required_source)
    require("cxx23_profile" in cmake,
            "Portable regression set must include the C++23 profile gate")
    require("spider/session_actions.cpp" not in cmake,
            "Legacy monolithic Spider session_actions.cpp must not be compiled")
    require(not (root / "app/src/main/cpp/spider/session_actions.cpp").exists(),
            "Dead duplicate Spider session_actions.cpp must not remain in source tree")

    methods = re.findall(r"public\s+static\s+native\s+\S+\s+(\w+)\s*\(", bridge)
    with tempfile.TemporaryDirectory() as temp:
        native_path = Path(temp) / "libdmcviewer.so"
        native_path.write_bytes(native_bytes)
        symbols = run(elf_reader, "--dyn-syms", "--wide", str(native_path))
        header = run(elf_reader, "-h", str(native_path))
        require("AArch64" in header, "Native library is not AArch64")
        load_alignments = load_segment_alignments(elf_reader, native_path)
        require(all(alignment >= PAGE_ALIGNMENT for alignment in load_alignments),
                "Every ELF PT_LOAD must support 16 KiB pages; alignments=" +
                ",".join(hex(value) for value in load_alignments))
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

    native_size = len(native_bytes)
    print(json.dumps({
        "apk": str(args.apk),
        "versionName": "1.0.41",
        "versionCode": 68,
        "abi": "arm64-v8a",
        "cpp_standard": CPP_STANDARD,
        "cpp_standard_authority": CPP_STANDARD_AUTHORITY,
        "spider_cpp_profile": SPIDER_CPP_PROFILE,
        "ndk_version": NDK_VERSION,
        "signing_policy": args.signing_policy,
        "signed": signed,
        "signer_sha256": signer_sha256,
        "apk_signing_block_present": apk_signing_block_present,
        "jar_signature_entries": jar_signature_entries,
        "sha256": hashlib.sha256(args.apk.read_bytes()).hexdigest(),
        "native_dso_count": 1,
        "native_dso": "lib/arm64-v8a/libdmcviewer.so",
        "native_zip_alignment": PAGE_ALIGNMENT,
        "native_elf_load_alignments": load_alignments,
        "extractNativeLibs": False,
        "jni_exports_checked": len(methods),
        "elf_reader": elf_reader,
        "rengine_pin": RENGINE_PIN,
        "rengine_gitlink": rengine_gitlink,
        "rengine_checkout": rengine_checkout,
        "scm_authority_base": SCM_AUTHORITY_BASE,
        "zip_integrity": "pass",
        "duplicate_zip_entry_names": duplicate_entry_names,
        "duplicate_large_payload_min_bytes": DUPLICATE_PAYLOAD_MIN_BYTES,
        "duplicate_large_payload_groups": duplicate_large_payload_groups,
        "duplicate_large_payload_waste_bytes": duplicate_large_payload_waste_bytes,
        "largest_entries": largest_entries,
        "apk_bytes": apk_bytes,
        "native_bytes": native_size,
        "dex_bytes": dex_bytes,
        "max_apk_bytes": MAX_APK_BYTES,
        "max_native_bytes": MAX_NATIVE_BYTES,
        "max_dex_bytes": MAX_DEX_BYTES,
        "max_installed_app_bytes": MAX_INSTALLED_APP_BYTES,
        "installed_size_measurement": "required-on-device-via-StorageStats.getAppBytes",
        "installed_size_tool": "tools/measure_installed_footprint.py",
        "historical_v26_growth_comparable": False,
        "size_acceptance_authority": "absolute-package-metrics+StorageStats.getAppBytes<=4MiB",
        "modular_native_architecture": "pass",
        "device_test": "pending",
    }, indent=2))


if __name__ == "__main__":
    main()
