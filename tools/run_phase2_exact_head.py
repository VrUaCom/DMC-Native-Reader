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


def require_static_contract() -> None:
    root_gradle = (ROOT / "build.gradle.kts").read_text(encoding="utf-8")
    app_gradle = (ROOT / "app/build.gradle.kts").read_text(encoding="utf-8")
    cmake = (ROOT / "app/src/main/cpp/CMakeLists.txt").read_text(encoding="utf-8")
    profile = (
        ROOT / "app/src/main/cpp/include/dmcresource/cpp23_profile.h"
    ).read_text(encoding="utf-8")

    if f'version "{EXPECTED_AGP}"' not in root_gradle:
        fail(f"Android Gradle Plugin pin is not {EXPECTED_AGP}")

    required_cmake = (
        "cxx_std_23",
        "CXX_STANDARD 23",
        "CXX_STANDARD_REQUIRED ON",
        "CXX_EXTENSIONS OFF",
    )
    for marker in required_cmake:
        if marker not in cmake:
            fail(f"CMake C++23 contract marker missing: {marker}")
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
    run_logged(
        "02-cmake-build",
        [args.cmake, "--build", str(HOST_BUILD_DIR.relative_to(ROOT)), "--parallel", "2"],
        env=env,
    )
    run_logged(
        "03-ctest",
        [args.ctest, "--test-dir", str(HOST_BUILD_DIR.relative_to(ROOT)), "--output-on-failure"],
        env=env,
    )

    run_logged(
        "04-gradle-android",
        [args.gradle, "--no-daemon", "clean", ":app:assembleDebug", ":app:assembleRelease"],
        env=env,
    )

    debug_apk = ROOT / "app/build/outputs/apk/debug/app-debug.apk"
    release_apk = ROOT / "app/build/outputs/apk/release/app-release-unsigned.apk"
    for apk in (debug_apk, release_apk):
        if not apk.is_file():
            fail(f"expected APK missing: {apk}")

    run_logged(
        "05-device-apk-verifier",
        [
            sys.executable,
            "tools/verify_device_apk.py",
            str(debug_apk.relative_to(ROOT)),
            "--sdk",
            str(sdk),
        ],
        env=env,
    )

    final_dirty = capture(["git", "status", "--porcelain", "--untracked-files=all"])
    if final_dirty.strip():
        fail("build changed source/unignored files; evidence is not from a clean worktree")

    evidence = {
        "schema": "dmc-native-reader.phase2-evidence.v1",
        "timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "repository": "VrUaCom/DMC-Native-Reader",
        "branch": branch,
        "head": head,
        "rengine_gitlink": gitlink,
        "rengine_checkout": rengine_checkout,
        "toolchain": {
            "agp": EXPECTED_AGP,
            "gradle": gradle_version,
            "java_major": EXPECTED_JAVA_MAJOR,
            "host_cmake": ".".join(map(str, cmake_version)),
            "ctest_version_output": ctest_version_text.strip(),
            "android_sdk": str(sdk),
            "android_platform": EXPECTED_ANDROID_PLATFORM,
            "build_tools": EXPECTED_BUILD_TOOLS,
            "android_cmake": EXPECTED_ANDROID_CMAKE,
            "android_ndk": EXPECTED_NDK,
            "android_ndk_clang_path": str(ndk_clang),
            "android_ndk_clang_version_output": ndk_clang_version_text.strip(),
        },
        "java_version_output": java_version_text.strip(),
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
