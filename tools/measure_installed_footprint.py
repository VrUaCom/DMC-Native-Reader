#!/usr/bin/env python3
"""Measure authoritative installed Android app/code bytes for release evidence.

The acceptance metric is Android StorageStats.getAppBytes(), exposed on current
Android builds through `pm get-package-storage-stats`. Android defines app bytes as
APK files + optimized compiler output + unpacked native libraries (and OBB when
present), separately from mutable data/cache.

This tool is device evidence, not an APK verifier. `verify_device_apk.py` remains
the package/build authority. The tool fails closed when StorageStats is unavailable
instead of silently substituting a directory-size heuristic.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from typing import NoReturn, Sequence

DEFAULT_PACKAGE = "com.dmcrengine.nativereader"
EXPECTED_VERSION_CODE = "50"
EXPECTED_VERSION_NAME = "1.0.27"
MAX_INSTALLED_APP_BYTES = 4 * 1024 * 1024
ART_COMPILE_NONE = "none"
ART_COMPILE_SPEED = "speed"
ART_COMPILE_MODES = (ART_COMPILE_NONE, ART_COMPILE_SPEED)


def fail(message: str) -> NoReturn:
    raise SystemExit(f"Installed-footprint evidence failure: {message}")


def normalize_user_arg(value: str) -> str:
    """Restrict device evidence to one explicit Android user/profile scope."""
    if value == "current":
        return value
    if re.fullmatch(r"\d+", value):
        return str(int(value, 10))
    fail("--user must be `current` or a non-negative integer Android user ID")


def resolve_user_arg(requested_user: str, current_user: str) -> str:
    """Freeze `current` to one numeric user ID before any package evidence calls."""
    if not re.fullmatch(r"\d+", current_user):
        fail(f"could not determine current Android user ID: {current_user}")
    return current_user if requested_user == "current" else requested_user


def capture(command: Sequence[str]) -> str:
    completed = subprocess.run(
        list(command),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    output = completed.stdout or ""
    if completed.returncode != 0:
        fail(
            f"command exited {completed.returncode}: {' '.join(command)}\n"
            f"{output.strip()}"
        )
    return output


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_adb_file(adb: str, serial: str, remote_path: str) -> str:
    command = adb_command(adb, serial, "exec-out", "cat", remote_path)
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert process.stdout is not None
    digest = hashlib.sha256()
    for chunk in iter(lambda: process.stdout.read(1024 * 1024), b""):
        digest.update(chunk)
    stderr = process.stderr.read() if process.stderr is not None else b""
    return_code = process.wait()
    if return_code != 0:
        fail(
            f"could not hash installed APK {remote_path}; "
            f"adb exited {return_code}: {stderr.decode(errors='replace').strip()}"
        )
    return digest.hexdigest()


def parse_pm_paths(output: str) -> list[str]:
    paths: list[str] = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if not line.startswith("package:"):
            fail(f"unexpected `pm path` output line: {line}")
        path = line[len("package:"):]
        if not path.startswith("/"):
            fail(f"non-absolute package path: {path}")
        paths.append(path)
    if not paths:
        fail("package is not installed or `pm path` returned no package paths")
    return paths


def require_single_base_apk(paths: Sequence[str]) -> str:
    if len(paths) != 1:
        fail(
            "canonical Native Reader acceptance expects one installed base APK; "
            "found package paths: " + ", ".join(paths)
        )
    path = paths[0]
    if not path.endswith("/base.apk"):
        fail(f"installed package path is not a canonical base.apk: {path}")
    return path


def require_stable_installed_identity(
    initial_path: str,
    initial_sha256: str,
    final_path: str,
    final_sha256: str,
) -> None:
    """Reject package replacement/reinstall races during one evidence measurement."""
    if final_path != initial_path:
        fail(
            "installed base.apk path changed during measurement: "
            f"{initial_path} -> {final_path}"
        )
    if final_sha256 != initial_sha256:
        fail(
            "installed base.apk bytes changed during measurement: "
            f"{initial_sha256} -> {final_sha256}"
        )


def parse_storage_stats(output: str) -> dict[str, int]:
    """Parse `pm get-package-storage-stats` byte fields.

    Current AOSP output is `name: <N> bytes (...)`. Only the exact byte count is
    accepted; human-readable suffixes are diagnostic and never parsed.
    """
    if "Error:" in output or "Unknown command" in output:
        fail("Android package StorageStats shell command is unavailable: " + output.strip())

    stats: dict[str, int] = {}
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        match = re.fullmatch(r"([^:]+?)\s*:\s*(\d+)\s+bytes(?:\s+\(.*\))?", line)
        if match is None:
            continue
        key = re.sub(r"\s+", "_", match.group(1).strip().lower())
        value = int(match.group(2), 10)
        stats[key] = value

    if "code" not in stats:
        fail(
            "StorageStats output did not expose authoritative `code` bytes; "
            "do not substitute `du` or another heuristic. Output: " + output.strip()
        )
    return stats


def acceptance_installed_app_bytes(
    baseline_bytes: int,
    stress_bytes: int | None,
) -> int:
    """Gate on the larger observed authoritative StorageStats code measurement."""
    if baseline_bytes < 0 or (stress_bytes is not None and stress_bytes < 0):
        fail("installed app byte measurements must be non-negative")
    if stress_bytes is None:
        return baseline_bytes
    return max(baseline_bytes, stress_bytes)


def parse_package_version(dumpsys: str) -> tuple[str | None, str | None]:
    code_match = re.search(r"(?m)^\s*versionCode=(\d+)\b", dumpsys)
    name_match = re.search(r"(?m)^\s*versionName=([^\s]+)\s*$", dumpsys)
    return (
        code_match.group(1) if code_match else None,
        name_match.group(1) if name_match else None,
    )


def adb_command(adb: str, serial: str | None, *args: str) -> list[str]:
    command = [adb]
    if serial:
        command += ["-s", serial]
    command.extend(args)
    return command


def package_path_command(adb: str, serial: str, user: str, package: str) -> list[str]:
    return adb_command(
        adb, serial, "shell", "pm", "path", "--user", user, package)


def storage_stats_command(adb: str, serial: str, user: str, package: str) -> list[str]:
    return adb_command(
        adb,
        serial,
        "shell",
        "pm",
        "get-package-storage-stats",
        "--user",
        user,
        package,
    )


def art_compile_command(
    adb: str,
    serial: str,
    package: str,
    mode: str,
) -> list[str]:
    """Build the package-scoped ART compile stress command."""
    if mode != ART_COMPILE_SPEED:
        fail(f"unsupported ART compile stress mode: {mode}")
    return adb_command(
        adb,
        serial,
        "shell",
        "cmd",
        "package",
        "compile",
        "-m",
        mode,
        "-f",
        package,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb", help="adb executable")
    parser.add_argument("--serial", help="ADB device serial; recommended for release evidence")
    parser.add_argument("--package", default=DEFAULT_PACKAGE)
    parser.add_argument(
        "--user",
        default="current",
        help="Android user/profile to measure (`current` or a numeric user ID).",
    )
    parser.add_argument(
        "--apk",
        type=Path,
        required=True,
        help="Exact reviewed APK installed on the device; SHA-256 is verified against base.apk.",
    )
    parser.add_argument(
        "--expected-apk-sha256",
        help="Optional expected SHA-256 guard for --apk; mismatch aborts before measurement.",
    )
    parser.add_argument(
        "--art-compile-mode",
        choices=ART_COMPILE_MODES,
        default=ART_COMPILE_NONE,
        help=(
            "Optional package-scoped ART stress state. `speed` performs a full-AOT "
            "compile after the baseline StorageStats measurement, then measures again."
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    requested_user = normalize_user_arg(args.user)

    apk = args.apk.expanduser().resolve()
    if not apk.is_file():
        fail(f"APK does not exist: {apk}")
    reviewed_apk_sha256 = sha256_file(apk)
    if args.expected_apk_sha256:
        expected = args.expected_apk_sha256.lower()
        if not re.fullmatch(r"[0-9a-f]{64}", expected):
            fail("--expected-apk-sha256 must be 64 hexadecimal characters")
        if reviewed_apk_sha256 != expected:
            fail(f"APK SHA-256 {reviewed_apk_sha256} != expected {expected}")

    state = capture(adb_command(args.adb, args.serial, "get-state")).strip()
    if state != "device":
        fail(f"ADB device state is not `device`: {state}")

    serial = capture(adb_command(args.adb, args.serial, "get-serialno")).strip()
    if not serial or serial == "unknown":
        fail("could not determine ADB device serial")
    if args.serial and serial != args.serial:
        fail(f"ADB serial {serial} != requested {args.serial}")

    current_user = capture(
        adb_command(args.adb, serial, "shell", "am", "get-current-user")
    ).strip()
    resolved_user = resolve_user_arg(requested_user, current_user)

    package_paths = parse_pm_paths(
        capture(package_path_command(
            args.adb, serial, resolved_user, args.package))
    )
    installed_base_apk = require_single_base_apk(package_paths)
    installed_apk_sha256 = sha256_adb_file(
        args.adb, serial, installed_base_apk
    )
    if installed_apk_sha256 != reviewed_apk_sha256:
        fail(
            "installed base.apk is not the reviewed APK: "
            f"installed={installed_apk_sha256} reviewed={reviewed_apk_sha256}"
        )

    baseline_storage_output = capture(
        storage_stats_command(
            args.adb, serial, resolved_user, args.package)
    )
    baseline_storage_stats = parse_storage_stats(baseline_storage_output)
    baseline_installed_app_bytes = baseline_storage_stats["code"]

    art_compile_output: str | None = None
    stress_storage_stats: dict[str, int] | None = None
    stress_installed_app_bytes: int | None = None
    if args.art_compile_mode == ART_COMPILE_SPEED:
        art_compile_output = capture(
            art_compile_command(
                args.adb,
                serial,
                args.package,
                args.art_compile_mode,
            )
        )
        stress_storage_output = capture(
            storage_stats_command(
                args.adb, serial, resolved_user, args.package)
        )
        stress_storage_stats = parse_storage_stats(stress_storage_output)
        stress_installed_app_bytes = stress_storage_stats["code"]

    installed_app_bytes = acceptance_installed_app_bytes(
        baseline_installed_app_bytes,
        stress_installed_app_bytes,
    )

    dumpsys = capture(
        adb_command(args.adb, serial, "shell", "dumpsys", "package", args.package)
    )
    version_code, version_name = parse_package_version(dumpsys)
    if version_code != EXPECTED_VERSION_CODE or version_name != EXPECTED_VERSION_NAME:
        fail(
            "installed package identity mismatch: "
            f"versionCode={version_code} versionName={version_name}; "
            f"expected {EXPECTED_VERSION_CODE}/{EXPECTED_VERSION_NAME}"
        )

    model = capture(
        adb_command(args.adb, serial, "shell", "getprop", "ro.product.model")
    ).strip()
    fingerprint = capture(
        adb_command(args.adb, serial, "shell", "getprop", "ro.build.fingerprint")
    ).strip()
    android_release = capture(
        adb_command(args.adb, serial, "shell", "getprop", "ro.build.version.release")
    ).strip()
    sdk_level = capture(
        adb_command(args.adb, serial, "shell", "getprop", "ro.build.version.sdk")
    ).strip()

    final_package_paths = parse_pm_paths(
        capture(package_path_command(
            args.adb, serial, resolved_user, args.package))
    )
    final_installed_base_apk = require_single_base_apk(final_package_paths)
    final_installed_apk_sha256 = sha256_adb_file(
        args.adb, serial, final_installed_base_apk
    )
    require_stable_installed_identity(
        installed_base_apk,
        installed_apk_sha256,
        final_installed_base_apk,
        final_installed_apk_sha256,
    )

    passed = installed_app_bytes <= MAX_INSTALLED_APP_BYTES
    report = {
        "schema": "dmc-native-reader.installed-footprint.v3",
        "package": args.package,
        "versionCode": version_code,
        "versionName": version_name,
        "device_serial": serial,
        "device_model": model,
        "android_release": android_release,
        "android_sdk": sdk_level,
        "build_fingerprint": fingerprint,
        "requested_android_user": requested_user,
        "resolved_android_user": resolved_user,
        "current_android_user": current_user,
        "package_paths": package_paths,
        "installed_base_apk": installed_base_apk,
        "final_package_paths": final_package_paths,
        "final_installed_base_apk": final_installed_base_apk,
        "measurement": "Android StorageStats.getAppBytes via pm get-package-storage-stats",
        "baseline_installed_app_bytes": baseline_installed_app_bytes,
        "baseline_storage_stats_bytes": baseline_storage_stats,
        "art_compile_mode": args.art_compile_mode,
        "art_compile_output": (
            art_compile_output.strip() if art_compile_output is not None else None
        ),
        "stress_installed_app_bytes": stress_installed_app_bytes,
        "stress_storage_stats_bytes": stress_storage_stats,
        "installed_app_bytes": installed_app_bytes,
        "installed_app_bytes_acceptance_rule": "max(baseline, art-stress-if-requested)",
        "max_installed_app_bytes": MAX_INSTALLED_APP_BYTES,
        "storage_stats_bytes": baseline_storage_stats,
        "mutable_user_data_cache_included_in_gate": False,
        "reviewed_apk": str(apk),
        "reviewed_apk_sha256": reviewed_apk_sha256,
        "installed_apk_sha256": installed_apk_sha256,
        "final_installed_apk_sha256": final_installed_apk_sha256,
        "artifact_sha256_match": True,
        "installed_package_identity_stable": True,
        "pass": passed,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if passed else 2


if __name__ == "__main__":
    raise SystemExit(main())
