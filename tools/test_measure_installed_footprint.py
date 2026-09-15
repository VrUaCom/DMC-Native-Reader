#!/usr/bin/env python3

from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("measure_installed_footprint.py")
spec = spec_from_file_location("measure_installed_footprint", MODULE_PATH)
assert spec is not None and spec.loader is not None
module = module_from_spec(spec)
spec.loader.exec_module(module)


def expect_system_exit(callback) -> None:
    try:
        callback()
    except SystemExit:
        return
    raise AssertionError("expected SystemExit")


def main() -> int:
    paths = module.parse_pm_paths(
        "package:/data/app/~~abc/pkg-xyz/base.apk\n"
        "package:/data/app/~~abc/pkg-xyz/split_config.arm64_v8a.apk\n"
    )
    assert paths == [
        "/data/app/~~abc/pkg-xyz/base.apk",
        "/data/app/~~abc/pkg-xyz/split_config.arm64_v8a.apk",
    ]
    assert module.package_code_dir(paths) == "/data/app/~~abc/pkg-xyz"

    assert module.parse_du_kib(
        "4096\t/data/app/~~abc/pkg-xyz\n",
        "/data/app/~~abc/pkg-xyz",
    ) == 4096
    assert module.MAX_INSTALLED_CODE_KIB == 4096
    assert module.MAX_INSTALLED_CODE_BYTES == 4 * 1024 * 1024
    assert 4096 * 1024 <= module.MAX_INSTALLED_CODE_BYTES
    assert 4097 * 1024 > module.MAX_INSTALLED_CODE_BYTES

    version_code, version_name = module.parse_package_version(
        "  versionCode=33 minSdk=26 targetSdk=36\n"
        "  versionName=1.0.6\n"
    )
    assert version_code == "33"
    assert version_name == "1.0.6"

    expect_system_exit(lambda: module.parse_pm_paths(""))
    expect_system_exit(
        lambda: module.package_code_dir([
            "/data/app/a/base.apk",
            "/data/app/b/split.apk",
        ])
    )
    expect_system_exit(
        lambda: module.package_code_dir(["/data/user/0/pkg/base.apk"])
    )
    expect_system_exit(
        lambda: module.parse_du_kib(
            "4096\t/data/app/other\n",
            "/data/app/expected",
        )
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
