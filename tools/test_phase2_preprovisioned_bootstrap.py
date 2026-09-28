#!/usr/bin/env python3
from __future__ import annotations

import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOOTSTRAP = ROOT / "tools" / "bootstrap_phase2_self_hosted_ubuntu.sh"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


def main() -> int:
    subprocess.run(["bash", "-n", str(BOOTSTRAP)], check=True)
    text = BOOTSTRAP.read_text(encoding="utf-8")

    require("--preprovisioned" in text, "pre-provisioned mode disappeared")
    require(
        '[[ ! "$EXPECTED_HEAD" =~ ^[0-9a-f]{40}$ ]]' in text,
        "reviewed exact-head guard disappeared",
    )

    require(
        re.search(
            r'if \[\[ "\$PREPROVISIONED" -eq 0 \]\]; then\s+'
            r'"\$\{SUDO\[@\]\}" apt-get update',
            text,
        ) is not None,
        "apt install is no longer explicitly install-mode only",
    )
    require(
        re.search(
            r'if \[\[ "\$PREPROVISIONED" -eq 0 \]\]; then\s+'
            r'required_commands\+\=\(curl\)',
            text,
        ) is not None,
        "curl is no longer install-mode-only host tooling",
    )

    gradle_branch = re.search(
        r'if \[\[ "\$PREPROVISIONED" -eq 1 \]\]; then\s+'
        r'GRADLE_HOME=.*?\s+else\s+'
        r'GRADLE_HOME=.*?curl -fsSL .*?unzip -q .*?\s+fi',
        text,
        re.DOTALL,
    )
    require(
        gradle_branch is not None,
        "Gradle download is no longer isolated to install mode",
    )

    android_branch = re.search(
        r'# Install mode owns a private SDK\. Pre-provisioned mode consumes an exact'
        r'.*?if \[\[ "\$PREPROVISIONED" -eq 1 \]\]; then'
        r'.*?else'
        r'.*?curl -fsSL .*?'
        r'.*?yes \| "\$SDKMANAGER" --licenses'
        r'.*?"\$SDKMANAGER" \\'
        r'.*?fi',
        text,
        re.DOTALL,
    )
    require(
        android_branch is not None,
        "Android SDK download/sdkmanager mutation is no longer isolated to install mode",
    )

    require(
        re.search(
            r'if \[\[ "\$PREPROVISIONED" -eq 0 \]\]; then\s+'
            r'git submodule update --init --recursive\s+fi',
            text,
        ) is not None,
        "git submodule update is no longer install-mode only",
    )

    for required in (
        "PHASE2_JAVA_HOME",
        "PHASE2_GRADLE_HOME",
        "PHASE2_ANDROID_SDK_ROOT",
        'gitlink="$(git rev-parse "HEAD:$RENGINE_REL")"',
        'rengine_checkout="$(git -C "$rengine_path" rev-parse HEAD',
        'Rengine checkout must remain clean/read-only.',
    ):
        require(required in text, f"pre-provisioned fail-closed contract missing: {required}")

    require(
        text.count("curl -fsSL") == 3,
        "unexpected network download command added to bootstrap",
    )
    require(
        text.count("git submodule update --init --recursive") == 1,
        "unexpected additional submodule mutation path added",
    )

    print("PASS: pre-provisioned Phase-2 bootstrap remains validation-only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
