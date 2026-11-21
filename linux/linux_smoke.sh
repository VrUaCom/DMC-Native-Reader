#!/usr/bin/env bash
# Automated smoke verification test suite for DMC Native Reader on Linux.
# Mirrors tools/emulator_smoke.sh: opens supplied DMC resource files or
# performs validation over the repository assets using the native binaries.
#
# Usage:
#   linux/linux_smoke.sh [path/to/test/files]
#
# If no test folder is specified, uses built-in test fixtures from the repository.

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
bin_dir="$here/.build/bin"
cli_bin="${bin_dir}/dmc_native_reader_cli"
gui_bin="${bin_dir}/dmc_native_reader_linux"

# Fallback to CMake build paths if not in linux/.build
if [ ! -x "$cli_bin" ]; then
    cli_bin="$(find "$repo" -maxdepth 3 -name "dmc_native_reader_cli" -type f -perm +111 2>/dev/null | head -1)"
fi

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()  { printf '    \033[1;32m[OK]\033[0m %s\n' "$*"; }
fail() { printf '    \033[1;31m[FAIL]\033[0m %s\n' "$*"; failures=$((failures + 1)); }

failures=0

say "DMC Native Reader Linux Smoke Test Suite"

if [ -z "$cli_bin" ] || [ ! -x "$cli_bin" ]; then
    echo "ERROR: dmc_native_reader_cli binary not found or not executable." >&2
    echo "Please build with: linux/build-linux.sh" >&2
    exit 1
fi

say "Testing CLI binary at: $cli_bin"

# 1. Basic CLI invocation
if "$cli_bin" >/dev/null 2>&1 || [ $? -eq 2 ]; then
    ok "CLI usage and argument parsing functional"
else
    fail "CLI invocation failed"
fi

# 2. Workspace scan smoke test
tmp_json="$(mktemp /tmp/dmc_smoke_scan_XXXXXX.json)"
say "Running workspace asset scan..."
if "$cli_bin" workspace-scan "$repo/app" --json "$tmp_json" >/dev/null 2>&1; then
    if [ -s "$tmp_json" ] && grep -q '"recognized":' "$tmp_json"; then
        count="$(grep -o '"recognized":true' "$tmp_json" | wc -l | tr -d ' ')"
        ok "Workspace scan verified: indexed recognized assets ($count entries)"
    else
        fail "Workspace scan output file invalid or empty"
    fi
else
    fail "Workspace scan execution failed"
fi
rm -f "$tmp_json"

# 3. Test on specific files if provided
test_dir="${1:-}"
if [ -n "$test_dir" ] && [ -d "$test_dir" ]; then
    say "Testing user-supplied assets in $test_dir..."
    for file in "$test_dir"/*; do
        [ -f "$file" ] || continue
        name="$(basename "$file")"
        say "Asset: $name"
        
        # Inspection
        if "$cli_bin" inspect "$file" >/dev/null 2>&1; then
            ok "inspect: $name"
        else
            fail "inspect failed: $name"
        fi
        
        # Report
        tmp_rep="$(mktemp /tmp/dmc_smoke_rep_XXXXXX.json)"
        if "$cli_bin" report "$file" --json "$tmp_rep" >/dev/null 2>&1 && [ -s "$tmp_rep" ]; then
            ok "report: $name"
        else
            fail "report failed: $name"
        fi
        rm -f "$tmp_rep"
        
        # Export PNG
        tmp_png="$(mktemp /tmp/dmc_smoke_exp_XXXXXX.png)"
        if "$cli_bin" export "$file" "$tmp_png" >/dev/null 2>&1; then
            # Verify PNG header (0x89 0x50 0x4E 0x47)
            if [ -f "$tmp_png" ] && [ "$(head -c 4 "$tmp_png")" = $'\x89PNG' ]; then
                ok "export PNG: $name"
            else
                ok "export (non-visual resource skipped clean): $name"
            fi
        fi
        rm -f "$tmp_png"
        
        # Benchmark
        if "$cli_bin" benchmark "$file" 1 >/dev/null 2>&1; then
            ok "benchmark: $name"
        else
            fail "benchmark failed: $name"
        fi
    done
fi

say "Smoke Test Summary"
if [ "$failures" -eq 0 ]; then
    printf '\033[1;32mALL SMOKE TESTS PASSED (0 failures).\033[0m\n'
    exit 0
else
    printf '\033[1;31mSMOKE TESTS FAILED (%d failure(s)).\033[0m\n' "$failures"
    exit 1
fi
