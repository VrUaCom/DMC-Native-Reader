#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "tools" / "run_phase2_exact_head.py"


def load_runner():
    spec = importlib.util.spec_from_file_location("dmc_phase2_exact_head", RUNNER_PATH)
    if spec is None or spec.loader is None:
        raise SystemExit("FAIL: could not load canonical Phase-2 runner")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_properties(path: Path, values: dict[str, str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "".join(f"{key}={value}\n" for key, value in values.items()),
        encoding="utf-8",
    )


def make_valid_sdk(root: Path, runner) -> None:
    write_properties(
        root / "platforms" / runner.EXPECTED_ANDROID_PLATFORM / "source.properties",
        {"AndroidVersion.ApiLevel": runner.EXPECTED_ANDROID_PLATFORM.removeprefix("android-")},
    )
    write_properties(
        root / "build-tools" / runner.EXPECTED_BUILD_TOOLS / "source.properties",
        {"Pkg.Revision": runner.EXPECTED_BUILD_TOOLS},
    )
    write_properties(
        root / "ndk" / runner.EXPECTED_NDK / "source.properties",
        {"Pkg.Revision": runner.EXPECTED_NDK},
    )
    write_properties(
        root / "cmake" / runner.EXPECTED_ANDROID_CMAKE / "source.properties",
        {"Pkg.Revision": runner.EXPECTED_ANDROID_CMAKE},
    )


def expect_failure(runner, sdk: Path, label: str) -> None:
    try:
        runner.validate_android_sdk_metadata(sdk)
    except SystemExit:
        return
    raise SystemExit(f"FAIL: metadata mismatch did not fail closed: {label}")


def main() -> int:
    runner = load_runner()

    with tempfile.TemporaryDirectory(prefix="dmc-phase2-sdk-meta-") as tmp:
        sdk = Path(tmp)
        make_valid_sdk(sdk, runner)
        verified = runner.validate_android_sdk_metadata(sdk)
        expected = {
            "platform_api_level": "36",
            "build_tools_revision": runner.EXPECTED_BUILD_TOOLS,
            "ndk_revision": runner.EXPECTED_NDK,
            "android_cmake_revision": runner.EXPECTED_ANDROID_CMAKE,
        }
        if verified != expected:
            raise SystemExit(f"FAIL: valid metadata projection mismatch: {verified!r}")

    mismatch_cases = (
        ("platform-api", "platforms", runner.EXPECTED_ANDROID_PLATFORM,
         "AndroidVersion.ApiLevel", "35"),
        ("build-tools", "build-tools", runner.EXPECTED_BUILD_TOOLS,
         "Pkg.Revision", "35.0.0"),
        ("ndk", "ndk", runner.EXPECTED_NDK,
         "Pkg.Revision", "29.0.0"),
        ("android-cmake", "cmake", runner.EXPECTED_ANDROID_CMAKE,
         "Pkg.Revision", "3.18.1"),
    )
    for label, family, version, key, wrong_value in mismatch_cases:
        with tempfile.TemporaryDirectory(prefix=f"dmc-phase2-{label}-") as tmp:
            sdk = Path(tmp)
            make_valid_sdk(sdk, runner)
            write_properties(
                sdk / family / version / "source.properties",
                {key: wrong_value},
            )
            expect_failure(runner, sdk, label)

    with tempfile.TemporaryDirectory(prefix="dmc-phase2-missing-key-") as tmp:
        sdk = Path(tmp)
        make_valid_sdk(sdk, runner)
        write_properties(
            sdk / "build-tools" / runner.EXPECTED_BUILD_TOOLS / "source.properties",
            {"Pkg.Desc": "missing revision"},
        )
        expect_failure(runner, sdk, "missing-key")

    with tempfile.TemporaryDirectory(prefix="dmc-phase2-missing-file-") as tmp:
        sdk = Path(tmp)
        make_valid_sdk(sdk, runner)
        (
            sdk / "ndk" / runner.EXPECTED_NDK / "source.properties"
        ).unlink()
        expect_failure(runner, sdk, "missing-file")

    print("PASS: canonical Android SDK metadata validator is fail-closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
