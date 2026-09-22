#!/usr/bin/env python3
"""Fast diagnostic preflight for the canonical Phase-2 execution environment.

This tool installs/downloads nothing and never substitutes for
run_phase2_exact_head.py. It validates whether an already provisioned Linux x64
host is worth spending a full evidence run on.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "tools" / "run_phase2_exact_head.py"
DEFAULT_OUTPUT = ROOT / "build" / "phase2-preflight.json"


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"Phase 2 preflight failure: {message}")


def load_contract():
    spec = importlib.util.spec_from_file_location("dmc_phase2_exact_head", RUNNER_PATH)
    if spec is None or spec.loader is None:
        fail("could not load canonical Phase-2 runner")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def capture(command: list[str], *, cwd: Path = ROOT) -> str:
    completed = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    output = completed.stdout or ""
    if completed.returncode != 0:
        fail(f"command exited {completed.returncode}: {' '.join(command)}\n{output}")
    return output


def require_clean_checkout(path: Path, label: str) -> None:
    dirty = capture(
        ["git", "-C", str(path), "status", "--porcelain", "--untracked-files=all"]
    )
    if dirty.strip():
        fail(f"{label} checkout is dirty")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate an already provisioned Phase-2 Linux x64 host."
    )
    parser.add_argument("--expected-head", required=True)
    parser.add_argument(
        "--sdk",
        help="Android SDK root; defaults to ANDROID_SDK_ROOT/ANDROID_HOME",
    )
    parser.add_argument("--gradle", default="gradle")
    parser.add_argument("--java", default="java")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--cxx", default="c++")
    parser.add_argument(
        "--output",
        default=str(DEFAULT_OUTPUT),
        help="JSON report path; default build/phase2-preflight.json",
    )
    return parser.parse_args()


def require_cpp23(cxx: str) -> str:
    source = """#include <bit>
#include <cstdint>
#include <expected>
#include <utility>
enum class E : unsigned { value = 1 };
int main() {
    std::expected<int, int> value = 7;
    auto swapped = std::byteswap(std::uint32_t{0x01020304u});
    auto underlying = std::to_underlying(E::value);
    return (!value.has_value() || swapped == 0u || underlying != 1u) ? 1 : 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="dmc-phase2-cxx23-") as tmp:
        tmp_path = Path(tmp)
        source_path = tmp_path / "probe.cpp"
        binary_path = tmp_path / "probe"
        source_path.write_text(source, encoding="utf-8")
        capture([cxx, "-std=c++23", str(source_path), "-o", str(binary_path)])
        capture([str(binary_path)])
    return capture([cxx, "--version"]).splitlines()[0].strip()


def main() -> int:
    args = parse_args()
    contract = load_contract()

    expected_head = args.expected_head.strip().lower()
    if not re.fullmatch(r"[0-9a-f]{40}", expected_head):
        fail("--expected-head must be one externally reviewed full 40-hex SHA")

    if platform.system() != "Linux":
        fail(f"canonical preflight requires Linux; found {platform.system()}")
    machine = platform.machine().lower()
    if machine not in {"x86_64", "amd64"}:
        fail(f"canonical preflight requires x86_64; found {machine}")

    observed_head = capture(["git", "rev-parse", "HEAD"]).strip().lower()
    if observed_head != expected_head:
        fail(f"HEAD {observed_head} != reviewed candidate {expected_head}")
    require_clean_checkout(ROOT, "Native Reader")

    rengine_path = ROOT / contract.RENGINE_REL
    if not rengine_path.is_dir():
        fail(f"pinned Rengine checkout missing: {rengine_path}")
    gitlink = capture(
        ["git", "rev-parse", f"HEAD:{contract.RENGINE_REL.as_posix()}"]
    ).strip()
    rengine_checkout = capture(
        ["git", "-C", str(rengine_path), "rev-parse", "HEAD"]
    ).strip()
    if rengine_checkout != gitlink:
        fail(f"Rengine checkout {rengine_checkout} != gitlink {gitlink}")
    require_clean_checkout(rengine_path, "Rengine")

    sdk_text = args.sdk or os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")
    if not sdk_text:
        fail("Android SDK not specified")
    sdk = Path(sdk_text).expanduser().resolve()
    if not sdk.is_dir():
        fail(f"Android SDK missing: {sdk}")

    required_paths = {
        "platform_tools": sdk / "platform-tools",
        "platform": sdk / "platforms" / contract.EXPECTED_ANDROID_PLATFORM,
        "build_tools": sdk / "build-tools" / contract.EXPECTED_BUILD_TOOLS,
        "ndk": sdk / "ndk" / contract.EXPECTED_NDK,
        "android_cmake": sdk / "cmake" / contract.EXPECTED_ANDROID_CMAKE,
    }
    for label, path in required_paths.items():
        if not path.exists():
            fail(f"{label} missing: {path}")

    sdk_metadata = contract.validate_android_sdk_metadata(sdk)
    ndk_clang = contract.find_ndk_clang(required_paths["ndk"])
    ndk_clang_output = capture([str(ndk_clang), "--version"])
    if "clang" not in ndk_clang_output.lower():
        fail("NDK clang++ output does not identify Clang")

    gradle_output = capture([args.gradle, "--version"])
    gradle_match = re.search(r"(?m)^Gradle\s+(\S+)\s*$", gradle_output)
    if not gradle_match:
        fail("could not parse Gradle version")
    gradle_version = gradle_match.group(1)
    if gradle_version != contract.EXPECTED_GRADLE:
        fail(f"Gradle {gradle_version} != canonical {contract.EXPECTED_GRADLE}")

    java_output = capture([args.java, "-version"])
    java_match = re.search(r'version\s+"(?:1\.)?(\d+)', java_output)
    if not java_match or int(java_match.group(1)) != contract.EXPECTED_JAVA_MAJOR:
        fail(f"canonical Phase-2 preflight requires JDK {contract.EXPECTED_JAVA_MAJOR}")

    cmake_output = capture([args.cmake, "--version"])
    cmake_version = contract.parse_version_tuple(
        cmake_output,
        r"^cmake version\s+(\d+(?:\.\d+)+)",
        "CMake",
    )
    if cmake_version < contract.MIN_HOST_CMAKE:
        fail(
            "host CMake "
            + ".".join(map(str, cmake_version))
            + " is older than canonical minimum "
            + ".".join(map(str, contract.MIN_HOST_CMAKE))
        )

    ctest_output = capture([args.ctest, "--version"])
    cxx_version = require_cpp23(args.cxx)

    report = {
        "schema": "dmc-native-reader.phase2-preflight.v1",
        "passed": True,
        "authority": "diagnostic-only; run_phase2_exact_head.py remains acceptance authority",
        "repository": "VrUaCom/DMC-Native-Reader",
        "expected_head": expected_head,
        "head": observed_head,
        "source_identity_match": True,
        "rengine_gitlink": gitlink,
        "rengine_checkout": rengine_checkout,
        "rengine_identity_match": True,
        "host": {
            "os": platform.system(),
            "arch": platform.machine(),
            "cxx23_probe": "PASS",
            "cxx_version": cxx_version,
            "cmake_version": ".".join(map(str, cmake_version)),
            "ctest_version_output": ctest_output.strip(),
        },
        "toolchain": {
            "java_major": contract.EXPECTED_JAVA_MAJOR,
            "gradle": gradle_version,
            "android_sdk": str(sdk),
            "android_component_metadata": sdk_metadata,
            "ndk_clang_path": str(ndk_clang),
            "ndk_clang_version_output": ndk_clang_output.strip(),
        },
    }

    output_path = Path(args.output).expanduser()
    if not output_path.is_absolute():
        output_path = ROOT / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
    output_path.write_text(payload, encoding="utf-8")
    print(payload, end="")
    print(f"Preflight report: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
