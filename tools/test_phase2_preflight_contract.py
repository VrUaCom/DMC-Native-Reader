#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PREFLIGHT = ROOT / "tools" / "run_phase2_preflight.py"
BOOTSTRAP = ROOT / "tools" / "bootstrap_phase2_self_hosted_ubuntu.sh"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


def main() -> int:
    preflight = PREFLIGHT.read_text(encoding="utf-8")
    bootstrap = BOOTSTRAP.read_text(encoding="utf-8")

    require(
        'RUNNER_PATH = ROOT / "tools" / "run_phase2_exact_head.py"' in preflight,
        "preflight no longer imports the canonical exact-head runner",
    )
    require(
        "contract = load_contract()" in preflight,
        "preflight no longer consumes the canonical runner contract",
    )

    for literal in ("9.5.0", "30.0.16248370", "36.0.0", "3.22.1"):
        require(
            literal not in preflight,
            f"preflight duplicated canonical toolchain literal: {literal}",
        )

    for forbidden in ("apt-get", "curl ", "sdkmanager", "git submodule update"):
        require(
            forbidden not in preflight,
            f"preflight gained forbidden install/network mutation: {forbidden}",
        )

    require(
        'parser.add_argument("--expected-head", required=True)' in preflight,
        "preflight exact reviewed HEAD is no longer mandatory",
    )
    require(
        're.fullmatch(r"[0-9a-f]{40}", expected_head)' in preflight,
        "preflight full 40-hex reviewed HEAD validation disappeared",
    )

    require(
        '"schema": "dmc-native-reader.phase2-preflight.v1"' in preflight,
        "preflight schema marker missing",
    )
    require(
        '"authority": "diagnostic-only; run_phase2_exact_head.py remains acceptance authority"'
        in preflight,
        "preflight diagnostic-only authority statement missing",
    )
    require(
        "phase2-evidence.json" not in preflight,
        "preflight must not emit the canonical Phase-2 evidence manifest",
    )
    require(
        "dmc-native-reader.phase2-evidence.v1" not in preflight,
        "preflight must not claim the canonical evidence schema",
    )

    preflight_marker = "python3 tools/run_phase2_preflight.py"
    runner_marker = "python3 tools/run_phase2_exact_head.py"
    preflight_pos = bootstrap.find(preflight_marker)
    runner_pos = bootstrap.find(runner_marker)
    require(preflight_pos >= 0, "bootstrap no longer prints the preflight command")
    require(runner_pos >= 0, "bootstrap no longer prints the full runner command")
    require(
        preflight_pos < runner_pos,
        "bootstrap no longer presents preflight before the full evidence runner",
    )
    require(
        bootstrap.count('"$EXPECTED_HEAD"') >= 2,
        "bootstrap no longer forwards one reviewed HEAD across operator stages",
    )

    print("PASS: Phase-2 preflight remains diagnostic-only and canonical-runner-backed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
