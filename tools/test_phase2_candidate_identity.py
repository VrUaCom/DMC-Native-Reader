#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "run_phase2_exact_head.py"
BOOTSTRAP = ROOT / "tools" / "bootstrap_phase2_self_hosted_ubuntu.sh"
WORKFLOW = ROOT / ".github" / "workflows" / "android.yml"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


def main() -> int:
    runner = RUNNER.read_text(encoding="utf-8")
    bootstrap = BOOTSTRAP.read_text(encoding="utf-8")
    workflow = WORKFLOW.read_text(encoding="utf-8")

    expected_arg = runner.find('"--expected-head"')
    require(expected_arg >= 0, "runner no longer declares --expected-head")
    required_flag = runner.find("required=True", expected_arg, expected_arg + 500)
    require(required_flag >= 0, "--expected-head is no longer mandatory")

    require(
        're.fullmatch(r"[0-9a-f]{40}", expected_head)' in runner,
        "runner no longer enforces a full 40-hex reviewed candidate SHA",
    )
    require(
        'if head != expected_head:' in runner,
        "runner no longer compares observed HEAD to externally nominated HEAD",
    )

    for marker in (
        '"expected_head": expected_head',
        '"head": head',
        '"candidate_identity_source": "external --expected-head"',
        '"candidate_identity_match": True',
        '"source_identity_stable": True',
    ):
        require(marker in runner, f"evidence manifest identity marker missing: {marker}")

    require(
        "PR #33" not in bootstrap,
        "self-hosted bootstrap has regressed to closed PR #33 coupling",
    )
    require(
        "reviewed candidate" in bootstrap.lower(),
        "self-hosted bootstrap no longer names reviewed-candidate authority",
    )

    require(
        'EXPECTED_SOURCE_SHA: ${{ github.event.pull_request.head.sha || github.sha }}'
        in workflow,
        "workflow no longer derives one explicit source SHA",
    )
    require(
        '--expected-head "$EXPECTED_SOURCE_SHA"' in workflow,
        "workflow no longer passes the exact source SHA into the evidence runner",
    )

    print("PASS: Phase-2 reviewed-candidate identity contract is intact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
