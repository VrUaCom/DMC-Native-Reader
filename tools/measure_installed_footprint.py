#!/usr/bin/env python3
"""Measure installed Android package/code footprint for release evidence.

This tool intentionally measures only the installed package code directory under
/data/app. It does not include mutable user data/cache under /data/user or
/data/data. The metric is allocated KiB reported by Android `du -sk`, converted
to bytes with 1024-byte resolution.

It is device evidence, not an APK verifier. `verify_device_apk.py` remains the
package/build authority.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
from typing import NoReturn, Sequence

DEFAULT_PACKAGE = "com.dmcrengine.nativereader"
MAX_INSTALLED_CODE_KIB = 4096
MAX_INSTALLED_CODE_BYTES = MAX_INSTALLED_CODE_KIB * 1024


def fail(message: str) -> NoReturn:
    raise SystemExit(f"Installed-footprint evidence failure: {message}")


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


def package_code_dir(paths: Sequence[str]) -> str:
    parents = {str(PurePosixPath(path).parent) for path in paths}
    if len(parents) != 1:
        fail(
            "installed APK/split paths do not share one package code directory: "
            + ", ".join(sorted(parents))
        )
    code_dir = next(iter(parents))
    if not code_dir.startswith("/data/app/"):
        fail(
            "package code directory is outside /data/app; refusing ambiguous "
            f"installed-size measurement: {code_dir}"
        )
    if code_dir.rstrip("/") == "/data/app":
        fail("refusing to measure the /data/app root")
    return code_dir


def parse_du_kib(output: str, expected_path: str) -> int:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    if len(lines) != 1:
        fail(f"expected one `du -sk` result, got {len(lines)}")
    fields = lines[0].split(maxsplit=1)
    if len(fields) != 2:
        fail(f"could not parse `du -sk` output: {lines[0]}")
    try:
        kib = int(fields[0], 10)
    except ValueError as error:
        fail(f"invalid KiB value from `du -sk`: {fields[0]}")
        raise AssertionError from error
    measured_path = fields[1].strip()
    if measured_path != expected_path:
        fail(
            f"`du -sk` measured unexpected path {measured_path}; "
            f"expected {expected_path}"
        )
    if kib < 0:
        fail("negative installed footprint reported by `du`")
    return kib


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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb", help="adb executable")
    parser.add_argument("--serial", help="ADB device serial; recommended for release evidence")
    parser.add_argument("--package", default=DEFAULT_PACKAGE)
    parser.add_argument(
        "--apk",
        type=Path,
        help="Optional local reviewed APK. Its SHA-256 is recorded in the evidence.",
    )
    parser.add_argument(
        "--expected-apk-sha256",
        help="Optional SHA-256 guard for --apk; mismatch aborts before measurement.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    apk_sha256: str | None = None
    if args.expected_apk_sha256 and args.apk is None:
        fail("--expected-apk-sha256 requires --apk")
    if args.apk is not None:
        apk = args.apk.expanduser().resolve()
        if not apk.is_file():
            fail(f"APK does not exist: {apk}")
        apk_sha256 = sha256_file(apk)
        if args.expected_apk_sha256:
            expected = args.expected_apk_sha256.lower()
            if not re.fullmatch(r"[0-9a-f]{64}", expected):
                fail("--expected-apk-sha256 must be 64 lowercase/uppercase hex characters")
            if apk_sha256 != expected:
                fail(f"APK SHA-256 {apk_sha256} != expected {expected}")

    state = capture(adb_command(args.adb, args.serial, "get-state")).strip()
    if state != "device":
        fail(f"ADB device state is not `device`: {state}")

    serial = capture(adb_command(args.adb, args.serial, "get-serialno")).strip()
    if not serial or serial == "unknown":
        fail("could not determine ADB device serial")
    if args.serial and serial != args.serial:
        fail(f"ADB serial {serial} != requested {args.serial}")

    package_paths = parse_pm_paths(
        capture(adb_command(args.adb, serial, "shell", "pm", "path", args.package))
    )
    code_dir = package_code_dir(package_paths)

    du_output = capture(
        adb_command(args.adb, serial, "shell", "du", "-sk", code_dir)
    )
    installed_code_kib = parse_du_kib(du_output, code_dir)
    installed_code_bytes = installed_code_kib * 1024

    dumpsys = capture(
        adb_command(args.adb, serial, "shell", "dumpsys", "package", args.package)
    )
    version_code, version_name = parse_package_version(dumpsys)

    model = capture(
        adb_command(args.adb, serial, "shell", "getprop", "ro.product.model")
    ).strip()
    fingerprint = capture(
        adb_command(args.adb, serial, "shell", "getprop", "ro.build.fingerprint")
    ).strip()
    android_release = capture(
        adb_command(args.adb, serial, "shell", "getprop", "ro.build.version.release")
    ).strip()

    passed = installed_code_bytes <= MAX_INSTALLED_CODE_BYTES
    report = {
        "package": args.package,
        "versionCode": version_code,
        "versionName": version_name,
        "device_serial": serial,
        "device_model": model,
        "android_release": android_release,
        "build_fingerprint": fingerprint,
        "package_paths": package_paths,
        "package_code_dir": code_dir,
        "measurement": "adb shell du -sk package_code_dir",
        "measurement_resolution_bytes": 1024,
        "installed_package_code_kib": installed_code_kib,
        "installed_package_code_bytes": installed_code_bytes,
        "max_installed_package_code_kib": MAX_INSTALLED_CODE_KIB,
        "max_installed_package_code_bytes": MAX_INSTALLED_CODE_BYTES,
        "mutable_user_data_cache_included": False,
        "reviewed_apk": str(args.apk.expanduser().resolve()) if args.apk else None,
        "reviewed_apk_sha256": apk_sha256,
        "pass": passed,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if passed else 2


if __name__ == "__main__":
    raise SystemExit(main())
