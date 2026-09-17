#!/usr/bin/env python3
"""Reproduce the Phase 2 C++23 host + Android evidence path.

This runner intentionally installs nothing and writes only ignored build outputs.
It is suitable for an authorized local/self-hosted environment when GitHub-hosted
runners are unavailable. It does not modify the vendored Rengine submodule.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import NoReturn, Sequence
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE_DIR = ROOT / "build" / "phase2-evidence"
HOST_BUILD_DIR = ROOT / "build" / "host-phase2"
RENGINE_REL = Path("app/src/main/cpp/vendor/dmc-rengine-cpp")
EXPECTED_AGP = "9.3.0"
EXPECTED_GRADLE = "9.5.0"
EXPECTED_JAVA_MAJOR = 17
EXPECTED_NDK = "30.0.16248370"
EXPECTED_ANDROID_PLATFORM = "android-36"
EXPECTED_BUILD_TOOLS = "36.0.0"
EXPECTED_ANDROID_CMAKE = "3.22.1"
MIN_HOST_CMAKE = (3, 22, 1)
RUNTIME_DSO = "lib/arm64-v8a/libdmcviewer.so"
REQUIRED_RUNTIME_MARKERS = (b"spider.crusader", b"spider.cpp23")
EXPECTED_MAX_APK_BYTES = 4 * 1024 * 1024
EXPECTED_MAX_INSTALLED_APP_BYTES = 4 * 1024 * 1024
EXPECTED_SIZE_AUTHORITY = "absolute-package-metrics+StorageStats.getAppBytes<=4MiB"
EXPECTED_DEBUG_SIGNER_SHA256 = (
    "f483539463f89dd957a8f7c68a3bb75da17450163f2e8767b4c47d5f1899adac")
EXPECTED_CTESTS = (
    "black_widow_state",
    "composite_builder",
    "composite_mod_scene",
    "composite_placement",
    "core_model_pipeline",
    "cxx23_profile",
    "dds_ptx_v1",
    "em028_corpus_contract",
    "gdata_legacy",
    "mod_spatial_adapter",
    "module_registry",
    "png_export_session",
    "ptx_model_texture",
    "ptx_runtime_compat",
    "ptx_transaction",
    "render_scene",
    "scm_authority",
    "spider_event_execution",
    "spider_model_execution",
    "uv_gallery",
    "session_inspection",
    "tm2_legacy",
    "workspace_graph",
)


def fail(message: str) -> NoReturn:
    raise SystemExit(f"Phase 2 evidence failure: {message}")


def capture(command: Sequence[str], *, cwd: Path = ROOT, env: dict[str, str] | None = None) -> str:
    print("+", " ".join(command), flush=True)
    completed = subprocess.run(
        list(command),
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    output = completed.stdout or ""
    if output:
        print(output, end="" if output.endswith("\n") else "\n", flush=True)
    if completed.returncode != 0:
        fail(f"command exited {completed.returncode}: {' '.join(command)}")
    return output


def run_logged(
    name: str,
    command: Sequence[str],
    *,
    cwd: Path = ROOT,
    env: dict[str, str] | None = None,
) -> None:
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    log_path = EVIDENCE_DIR / f"{name}.log"
    print("+", " ".join(command), flush=True)
    with log_path.open("w", encoding="utf-8", newline="\n") as log:
        process = subprocess.Popen(
            list(command),
            cwd=cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert process.stdout is not None
        for line in process.stdout:
            print(line, end="", flush=True)
            log.write(line)
        return_code = process.wait()
    if return_code != 0:
        fail(f"{name} exited {return_code}; see {log_path}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_version_tuple(text: str, pattern: str, label: str) -> tuple[int, ...]:
    match = re.search(pattern, text, re.MULTILINE)
    if not match:
        fail(f"could not parse {label} version")
    return tuple(int(piece) for piece in match.group(1).split("."))


def find_ndk_clang(ndk_path: Path) -> Path:
    prebuilt_root = ndk_path / "toolchains" / "llvm" / "prebuilt"
    if not prebuilt_root.is_dir():
        fail(f"NDK LLVM prebuilt directory missing: {prebuilt_root}")
    candidates: list[Path] = []
    for host_dir in sorted(prebuilt_root.iterdir()):
        if not host_dir.is_dir():
            continue
        for name in ("clang++", "clang++.exe"):
            candidate = host_dir / "bin" / name
            if candidate.is_file():
                candidates.append(candidate)
    if len(candidates) != 1:
        fail(
            "expected exactly one host NDK clang++ executable; found: " +
            ", ".join(str(path) for path in candidates)
        )
    return candidates[0]


def require_clean_checkout(path: Path, label: str) -> None:
    dirty = capture([
        "git", "-C", str(path), "status", "--porcelain", "--untracked-files=all",
    ])
    if dirty.strip():
        fail(f"{label} checkout is dirty; exact-head evidence requires read-only source state")


def require_runtime_markers(apk: Path) -> None:
    with zipfile.ZipFile(apk) as archive:
        try:
            native_bytes = archive.read(RUNTIME_DSO)
        except KeyError:
            fail(f"canonical runtime DSO missing from APK: {RUNTIME_DSO}")
    missing = [marker.decode("ascii") for marker in REQUIRED_RUNTIME_MARKERS if marker not in native_bytes]
    if missing:
        fail(
            f"runtime profile marker(s) missing from {apk.name}/{RUNTIME_DSO}: " +
            ", ".join(missing)
        )


def read_host_compiler_evidence() -> tuple[str, str]:
    configure_log = EVIDENCE_DIR / "01-cmake-configure.log"
    cache_file = HOST_BUILD_DIR / "CMakeCache.txt"
    if not configure_log.is_file() or not cache_file.is_file():
        fail("host CMake compiler evidence files are missing after configure")

    log_text = configure_log.read_text(encoding="utf-8", errors="replace")
    compiler_id = re.search(
        r"(?m)^-- The CXX compiler identification is\s+(.+?)\s*$", log_text)
    if not compiler_id:
        fail("could not identify the actual host C++ compiler from CMake configure log")

    cache_text = cache_file.read_text(encoding="utf-8", errors="replace")
    compiler_path = re.search(
        r"(?m)^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+?)\s*$", cache_text)
    if not compiler_path:
        fail("could not identify CMAKE_CXX_COMPILER from CMakeCache.txt")

    return compiler_id.group(1).strip(), compiler_path.group(1).strip()


def read_ctest_inventory(ctest: str, env: dict[str, str]) -> list[str]:
    output = capture(
        [
            ctest,
            "--test-dir",
            str(HOST_BUILD_DIR.relative_to(ROOT)),
            "--show-only=json-v1",
        ],
        env=env,
    )
    try:
        report = json.loads(output)
    except json.JSONDecodeError as error:
        fail(f"CTest inventory is not valid JSON: {error}")
    tests = report.get("tests") if isinstance(report, dict) else None
    if not isinstance(tests, list):
        fail("CTest inventory JSON does not contain a tests array")
    names: list[str] = []
    for test in tests:
        if not isinstance(test, dict) or not isinstance(test.get("name"), str):
            fail("CTest inventory contains a test without a string name")
        names.append(test["name"])

    expected = set(EXPECTED_CTESTS)
    actual = set(names)
    if len(names) != len(EXPECTED_CTESTS) or actual != expected:
        missing = sorted(expected - actual)
        unexpected = sorted(actual - expected)
        fail(
            "CTest inventory drift: "
            f"registered={len(names)} expected={len(EXPECTED_CTESTS)} "
            f"missing={missing} unexpected={unexpected}"
        )

    inventory_path = EVIDENCE_DIR / "02-ctest-inventory.json"
    inventory_path.write_text(
        json.dumps(
            {
                "registered_count": len(names),
                "expected_count": len(EXPECTED_CTESTS),
                "tests": sorted(names),
            },
            indent=2,
            sort_keys=True,
        ) + "\n",
        encoding="utf-8",
    )
    return sorted(names)


def xml_local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def read_ctest_junit(path: Path) -> dict[str, object]:
    if not path.is_file():
        fail(f"CTest JUnit evidence is missing: {path}")
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as error:
        fail(f"CTest JUnit evidence is not valid XML: {error}")

    cases = [element for element in root.iter() if xml_local_name(element.tag) == "testcase"]
    names: list[str] = []
    non_run: list[str] = []
    skipped: list[str] = []
    failures: list[str] = []
    errors: list[str] = []
    for case in cases:
        name = case.get("name")
        if not name:
            fail("CTest JUnit contains a testcase without a name")
        names.append(name)
        status = case.get("status")
        if status != "run":
            non_run.append(f"{name}:{status or '<missing>'}")
        child_tags = {xml_local_name(child.tag) for child in case}
        if "skipped" in child_tags:
            skipped.append(name)
        if "failure" in child_tags:
            failures.append(name)
        if "error" in child_tags:
            errors.append(name)

    expected = set(EXPECTED_CTESTS)
    actual = set(names)
    if len(names) != len(EXPECTED_CTESTS) or actual != expected:
        missing = sorted(expected - actual)
        unexpected = sorted(actual - expected)
        fail(
            "CTest execution drift: "
            f"executed={len(names)} expected={len(EXPECTED_CTESTS)} "
            f"missing={missing} unexpected={unexpected}"
        )
    if non_run or skipped or failures or errors:
        fail(
            "CTest JUnit did not prove an all-run/all-pass execution: "
            f"non_run={sorted(non_run)} skipped={sorted(skipped)} "
            f"failures={sorted(failures)} errors={sorted(errors)}"
        )

    return {
        "executed_count": len(names),
        "non_run_count": len(non_run),
        "skipped_count": len(skipped),
        "failure_count": len(failures),
        "error_count": len(errors),
        "tests": sorted(names),
    }


def read_verifier_report(
    log_name: str,
    expected_apk: Path,
    expected_signing_policy: str,
) -> dict:
    verifier_log = EVIDENCE_DIR / f"{log_name}.log"
    if not verifier_log.is_file():
        fail(f"APK verifier log missing after successful execution: {verifier_log}")
    try:
        report = json.loads(verifier_log.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        fail(f"APK verifier did not emit machine-readable JSON evidence: {error}")
    if not isinstance(report, dict):
        fail("APK verifier report is not a JSON object")
    if report.get("sha256") != sha256_file(expected_apk):
        fail(
            f"APK verifier report is not bound to {expected_apk.name}: "
            f"{report.get('sha256')}"
        )
    if report.get("signing_policy") != expected_signing_policy:
        fail(
            "APK verifier signing policy mismatch: "
            f"{report.get('signing_policy')} != {expected_signing_policy}"
        )
    if expected_signing_policy == "stable-debug":
        if report.get("signed") is not True or \
                report.get("signer_sha256") != EXPECTED_DEBUG_SIGNER_SHA256:
            fail("debug APK verifier report is missing the stable test signing authority")
        if report.get("apk_signing_block_present") is not True:
            fail("debug APK report does not prove a structurally present APK Signing Block")
    elif expected_signing_policy == "unsigned-release":
        if report.get("signed") is not False or report.get("signer_sha256") is not None:
            fail("release APK verifier report does not prove the unsigned boundary")
        if report.get("apk_signing_block_present") is not False:
            fail("release APK report still contains an APK Signing Block")
        if report.get("jar_signature_entries") != []:
            fail(
                "release APK report still contains JAR signature material: "
                f"{report.get('jar_signature_entries')}"
            )
    else:
        fail(f"unknown expected signing policy: {expected_signing_policy}")
    if report.get("duplicate_zip_entry_names"):
        fail("APK verifier reported duplicate ZIP entry names")
    duplicate_waste = report.get("duplicate_large_payload_waste_bytes")
    if duplicate_waste is None:
        fail("APK verifier report is missing duplicate payload metrics")
    if duplicate_waste != 0:
        fail(
            "APK contains avoidable large duplicate payloads; "
            f"wasted bytes={duplicate_waste} groups={report.get('duplicate_large_payload_groups')}"
        )
    if report.get("max_apk_bytes") != EXPECTED_MAX_APK_BYTES:
        fail(
            "APK verifier size policy drifted from 4 MiB: "
            f"{report.get('max_apk_bytes')}"
        )
    if report.get("max_installed_app_bytes") != EXPECTED_MAX_INSTALLED_APP_BYTES:
        fail(
            "installed StorageStats app-size policy drifted from 4 MiB: "
            f"{report.get('max_installed_app_bytes')}"
        )
    if report.get("installed_size_measurement") != \
            "required-on-device-via-StorageStats.getAppBytes":
        fail("APK verifier installed-size measurement authority marker is missing")
    if report.get("historical_v26_growth_comparable") is not False:
        fail("historical v26 package-growth data must not be acceptance authority")
    if report.get("size_acceptance_authority") != EXPECTED_SIZE_AUTHORITY:
        fail("APK verifier size authority marker is missing or unexpected")
    return report


def require_static_contract() -> None:
    root_gradle = (ROOT / "build.gradle.kts").read_text(encoding="utf-8")
    app_gradle = (ROOT / "app/build.gradle.kts").read_text(encoding="utf-8")
    cmake = (ROOT / "app/src/main/cpp/CMakeLists.txt").read_text(encoding="utf-8")
    profile = (
        ROOT / "app/src/main/cpp/include/dmcresource/cpp23_profile.h"
    ).read_text(encoding="utf-8")
    measure_tool = (ROOT / "tools/measure_installed_footprint.py").read_text(encoding="utf-8")
    rengine_reader_cmake_path = ROOT / RENGINE_REL / "cmake/reader_core.cmake"
    if not rengine_reader_cmake_path.is_file():
        fail("pinned Rengine reader_core.cmake is missing")
    rengine_reader_cmake = rengine_reader_cmake_path.read_text(encoding="utf-8")

    if f'version "{EXPECTED_AGP}"' not in root_gradle:
        fail(f"Android Gradle Plugin pin is not {EXPECTED_AGP}")

    required_cmake = (
        "cxx_std_23",
        "CXX_STANDARD 23",
        "CXX_STANDARD_REQUIRED ON",
        "CXX_EXTENSIONS OFF",
        "DMC_NATIVE_READER_CORE_SOURCES",
        "Duplicate source entry in DMC_NATIVE_READER_CORE_SOURCES",
        "DMC_NATIVE_READER_TESTS",
        "Duplicate test entry in DMC_NATIVE_READER_TESTS",
    )
    for marker in required_cmake:
        if marker not in cmake:
            fail(f"CMake C++23/dedup contract marker missing: {marker}")
    if "cxx_std_20" in cmake:
        fail("Native Reader CMake still contains cxx_std_20")
    if "-std=c++" in app_gradle:
        fail("Gradle must not own the C++ language mode")
    if f'ndkVersion = "{EXPECTED_NDK}"' not in app_gradle:
        fail(f"Gradle NDK pin is not {EXPECTED_NDK}")
    if f'version = "{EXPECTED_ANDROID_CMAKE}"' not in app_gradle:
        fail(f"Android externalNativeBuild CMake pin is not {EXPECTED_ANDROID_CMAKE}")
    for marker in (
        "std::expected",
        "std::byteswap",
        "std::to_underlying",
        "DMC_NATIVE_READER_LANGUAGE_LEVEL",
    ):
        if marker not in profile:
            fail(f"C++23 profile marker missing: {marker}")

    if "get-package-storage-stats" not in measure_tool or \
            "StorageStats.getAppBytes" not in measure_tool:
        fail("installed-size tool must use Android StorageStats app bytes")
    if "du -sk" in measure_tool or "parse_du" in measure_tool:
        fail("installed-size tool must not retain a directory-size fallback")
    if "installed_apk_sha256" not in measure_tool or "artifact_sha256_match" not in measure_tool:
        fail("installed-size tool must bind device evidence to the reviewed APK hash")

    if "target_compile_features(dmc_rengine_reader_core PUBLIC cxx_std_20)" not in rengine_reader_cmake:
        fail("pinned Rengine ReaderCore no longer exposes its C++20 target-scoped contract")
    if "CMAKE_CXX_STANDARD" in rengine_reader_cmake:
        fail("pinned Rengine ReaderCore must not impose a global C++ standard")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run exact-head Phase 2 C++23 host/APK verification."
    )
    parser.add_argument("--sdk", help="Android SDK root; defaults to ANDROID_SDK_ROOT/ANDROID_HOME")
    parser.add_argument("--gradle", default="gradle", help="Gradle executable (must be 9.5.0)")
    parser.add_argument("--cmake", default="cmake", help="CMake executable for host regressions")
    parser.add_argument("--ctest", default="ctest", help="CTest executable matching the host CMake")
    parser.add_argument("--java", default="java", help="Java executable (must be JDK 17)")
    parser.add_argument(
        "--expected-head",
        help="Optional SHA guard. The run aborts if HEAD differs before any build work.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    head = capture(["git", "rev-parse", "HEAD"]).strip()
    branch = capture(["git", "branch", "--show-current"]).strip()
    if args.expected_head and head != args.expected_head:
        fail(f"HEAD {head} does not match --expected-head {args.expected_head}")

    dirty = capture(["git", "status", "--porcelain", "--untracked-files=all"])
    if dirty.strip():
        fail("worktree is not clean; exact-head evidence must not include local source changes")

    require_static_contract()

    rengine_path = ROOT / RENGINE_REL
    if not rengine_path.exists():
        fail("Rengine submodule checkout is missing; initialize the pinned submodule first")
    gitlink = capture(["git", "rev-parse", f"HEAD:{RENGINE_REL.as_posix()}"]).strip()
    rengine_checkout = capture(["git", "-C", str(rengine_path), "rev-parse", "HEAD"]).strip()
    if gitlink != rengine_checkout:
        fail(f"Rengine checkout {rengine_checkout} != gitlink {gitlink}")
    require_clean_checkout(rengine_path, "Rengine submodule")

    sdk_text = args.sdk or os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")
    if not sdk_text:
        fail("Android SDK not specified; use --sdk or ANDROID_SDK_ROOT/ANDROID_HOME")
    sdk = Path(sdk_text).expanduser().resolve()
    if not sdk.exists():
        fail(f"Android SDK does not exist: {sdk}")

    ndk_path = sdk / "ndk" / EXPECTED_NDK
    required_sdk_paths = {
        "NDK": ndk_path,
        "Android platform": sdk / "platforms" / EXPECTED_ANDROID_PLATFORM,
        "SDK Build Tools": sdk / "build-tools" / EXPECTED_BUILD_TOOLS,
        "Android CMake": sdk / "cmake" / EXPECTED_ANDROID_CMAKE,
    }
    for label, path in required_sdk_paths.items():
        if not path.exists():
            fail(f"{label} missing: {path}")

    ndk_clang = find_ndk_clang(ndk_path)
    ndk_clang_version_text = capture([str(ndk_clang), "--version"])
    if "clang" not in ndk_clang_version_text.lower():
        fail("NDK clang++ --version output does not identify a Clang toolchain")

    gradle_version_text = capture([args.gradle, "--version"])
    gradle_match = re.search(r"(?m)^Gradle\s+(\S+)\s*$", gradle_version_text)
    if not gradle_match:
        fail("could not parse Gradle version")
    gradle_version = gradle_match.group(1)
    if gradle_version != EXPECTED_GRADLE:
        fail(f"Gradle {gradle_version} != canonical {EXPECTED_GRADLE}")

    cmake_version_text = capture([args.cmake, "--version"])
    cmake_version = parse_version_tuple(
        cmake_version_text, r"^cmake version\s+(\d+(?:\.\d+)+)", "CMake"
    )
    if cmake_version < MIN_HOST_CMAKE:
        fail(
            "host CMake " + ".".join(map(str, cmake_version)) +
            " is older than required 3.22.1"
        )

    ctest_version_text = capture([args.ctest, "--version"])
    java_version_text = capture([args.java, "-version"])
    java_match = re.search(r'version\s+"(?:1\.)?(\d+)', java_version_text)
    if not java_match or int(java_match.group(1)) != EXPECTED_JAVA_MAJOR:
        fail(f"canonical Android build requires JDK {EXPECTED_JAVA_MAJOR}")

    env = os.environ.copy()
    env["ANDROID_SDK_ROOT"] = str(sdk)
    env["ANDROID_HOME"] = str(sdk)

    shutil.rmtree(HOST_BUILD_DIR, ignore_errors=True)
    shutil.rmtree(EVIDENCE_DIR, ignore_errors=True)
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)

    run_logged(
        "00-package-policy-unit",
        [sys.executable, "tools/test_verify_device_apk.py"],
        env=env,
    )

    run_logged(
        "01-cmake-configure",
        [
            args.cmake,
            "-S",
            "app/src/main/cpp",
            "-B",
            str(HOST_BUILD_DIR.relative_to(ROOT)),
            "-DDMC_NATIVE_READER_BUILD_TESTS=ON",
            "-DCMAKE_BUILD_TYPE=Debug",
        ],
        env=env,
    )
    host_compiler_id, host_compiler_path = read_host_compiler_evidence()
    ctest_inventory = read_ctest_inventory(args.ctest, env)

    run_logged(
        "02-cmake-build",
        [args.cmake, "--build", str(HOST_BUILD_DIR.relative_to(ROOT)), "--parallel", "2"],
        env=env,
    )
    ctest_junit = EVIDENCE_DIR / "03-ctest-junit.xml"
    run_logged(
        "03-ctest",
        [
            args.ctest,
            "--test-dir",
            str(HOST_BUILD_DIR.relative_to(ROOT)),
            "--output-on-failure",
            "--output-junit",
            str(ctest_junit),
        ],
        env=env,
    )
    ctest_results = read_ctest_junit(ctest_junit)

    run_logged(
        "04-gradle-android",
        [args.gradle, "--no-daemon", ":app:clean", ":app:assembleDebug", ":app:assembleRelease"],
        env=env,
    )

    debug_apk = ROOT / "app/build/outputs/apk/debug/app-debug.apk"
    release_apk = ROOT / "app/build/outputs/apk/release/app-release-unsigned.apk"
    for apk in (debug_apk, release_apk):
        if not apk.is_file():
            fail(f"expected APK missing: {apk}")
        if apk.stat().st_size > EXPECTED_MAX_APK_BYTES:
            fail(
                f"{apk.name} exceeds 4 MiB package pre-gate: "
                f"{apk.stat().st_size} > {EXPECTED_MAX_APK_BYTES}"
            )
        require_runtime_markers(apk)

    run_logged(
        "05-debug-apk-verifier",
        [
            sys.executable,
            "tools/verify_device_apk.py",
            str(debug_apk.relative_to(ROOT)),
            "--sdk",
            str(sdk),
            "--signing-policy",
            "stable-debug",
        ],
        env=env,
    )
    debug_package_policy = read_verifier_report(
        "05-debug-apk-verifier", debug_apk, "stable-debug")

    run_logged(
        "06-release-apk-verifier",
        [
            sys.executable,
            "tools/verify_device_apk.py",
            str(release_apk.relative_to(ROOT)),
            "--sdk",
            str(sdk),
            "--signing-policy",
            "unsigned-release",
        ],
        env=env,
    )
    release_package_policy = read_verifier_report(
        "06-release-apk-verifier", release_apk, "unsigned-release")

    final_head = capture(["git", "rev-parse", "HEAD"]).strip()
    final_gitlink = capture(["git", "rev-parse", f"HEAD:{RENGINE_REL.as_posix()}"]).strip()
    final_rengine_checkout = capture(
        ["git", "-C", str(rengine_path), "rev-parse", "HEAD"]
    ).strip()
    if final_head != head:
        fail(f"repository HEAD changed during evidence run: {head} -> {final_head}")
    if final_gitlink != gitlink:
        fail(f"Rengine gitlink changed during evidence run: {gitlink} -> {final_gitlink}")
    if final_rengine_checkout != rengine_checkout:
        fail(
            "Rengine checkout changed during evidence run: "
            f"{rengine_checkout} -> {final_rengine_checkout}"
        )

    require_clean_checkout(rengine_path, "Rengine submodule")
    final_dirty = capture(["git", "status", "--porcelain", "--untracked-files=all"])
    if final_dirty.strip():
        fail("build changed source/unignored files; evidence is not from a clean worktree")

    evidence = {
        "schema": "dmc-native-reader.phase2-evidence.v1",
        "timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "repository": "VrUaCom/DMC-Native-Reader",
        "branch": branch,
        "head": head,
        "source_identity_stable": True,
        "rengine_gitlink": gitlink,
        "rengine_checkout": rengine_checkout,
        "rengine_worktree_clean": True,
        "toolchain": {
            "agp": EXPECTED_AGP,
            "gradle": gradle_version,
            "java_major": EXPECTED_JAVA_MAJOR,
            "host_cmake": ".".join(map(str, cmake_version)),
            "host_cxx_compiler": host_compiler_id,
            "host_cxx_compiler_path": host_compiler_path,
            "ctest_version_output": ctest_version_text.strip(),
            "android_sdk": str(sdk),
            "android_platform": EXPECTED_ANDROID_PLATFORM,
            "build_tools": EXPECTED_BUILD_TOOLS,
            "android_cmake": EXPECTED_ANDROID_CMAKE,
            "android_ndk": EXPECTED_NDK,
            "android_ndk_clang_path": str(ndk_clang),
            "android_ndk_clang_version_output": ndk_clang_version_text.strip(),
        },
        "ctest": {
            "registered_count": len(ctest_inventory),
            "expected_count": len(EXPECTED_CTESTS),
            "executed_count": ctest_results["executed_count"],
            "non_run_count": ctest_results["non_run_count"],
            "skipped_count": ctest_results["skipped_count"],
            "failure_count": ctest_results["failure_count"],
            "error_count": ctest_results["error_count"],
            "tests": ctest_inventory,
            "executed_tests": ctest_results["tests"],
            "passed": True,
            "log": "03-ctest.log",
            "junit": "03-ctest-junit.xml",
        },
        "rengine_language_contract": "target-scoped cxx_std_20",
        "runtime_markers": [marker.decode("ascii") for marker in REQUIRED_RUNTIME_MARKERS],
        "java_version_output": java_version_text.strip(),
        "size_contract": {
            "max_apk_bytes": EXPECTED_MAX_APK_BYTES,
            "max_installed_app_bytes": EXPECTED_MAX_INSTALLED_APP_BYTES,
            "installed_measurement_authority": "Android StorageStats.getAppBytes",
            "installed_measurement_tool": "tools/measure_installed_footprint.py",
            "installed_measurement_required_on_device": True,
            "historical_v26_growth_comparable": False,
        },
        "package_policy": debug_package_policy,
        "package_policies": {
            "debug": debug_package_policy,
            "release": release_package_policy,
        },
        "artifacts": {
            "debug_apk": {
                "path": str(debug_apk.relative_to(ROOT)),
                "size": debug_apk.stat().st_size,
                "sha256": sha256_file(debug_apk),
            },
            "release_apk": {
                "path": str(release_apk.relative_to(ROOT)),
                "size": release_apk.stat().st_size,
                "sha256": sha256_file(release_apk),
            },
        },
        "logs": sorted(path.name for path in EVIDENCE_DIR.glob("*.log")),
    }
    manifest = EVIDENCE_DIR / "phase2-evidence.json"
    manifest.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print("\nPhase 2 exact-head evidence completed successfully.")
    print(json.dumps(evidence, indent=2, sort_keys=True))
    print(f"Evidence manifest: {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
