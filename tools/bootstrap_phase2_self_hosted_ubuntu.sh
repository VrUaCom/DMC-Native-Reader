#!/usr/bin/env bash
set -euo pipefail

# Prepare an Ubuntu/WSL2 x64 machine for the DMC Native Reader Phase-2
# self-hosted exact-head evidence workflow.
#
# This script intentionally does NOT:
# - register a GitHub runner;
# - handle runner registration tokens/credentials;
# - modify the vendored DMC Rengine submodule;
# - install Android SDK/NDK, JDK, or Gradle (the canonical workflow owns those).

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "ERROR: Phase-2 self-hosted evidence requires Linux." >&2
  exit 1
fi

arch="$(uname -m)"
case "$arch" in
  x86_64|amd64) ;;
  *)
    echo "ERROR: Phase-2 self-hosted evidence requires x86_64; found: $arch" >&2
    exit 1
    ;;
esac

if ! command -v apt-get >/dev/null 2>&1; then
  echo "ERROR: This bootstrap supports Ubuntu/Debian (apt-get) only." >&2
  exit 1
fi

if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
  SUDO=()
elif command -v sudo >/dev/null 2>&1; then
  SUDO=(sudo)
else
  echo "ERROR: sudo is required when not running as root." >&2
  exit 1
fi

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y --no-install-recommends \
  build-essential \
  binutils \
  ca-certificates \
  cmake \
  curl \
  git \
  ninja-build \
  python3 \
  unzip \
  zip

required_commands=(
  bash
  git
  python3
  cmake
  ctest
  c++
  unzip
  zipinfo
  strings
  curl
)

missing=0
for command_name in "${required_commands[@]}"; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "MISSING: $command_name" >&2
    missing=1
  fi
done

if [[ "$missing" -ne 0 ]]; then
  echo "ERROR: required host tools are still missing after bootstrap." >&2
  exit 1
fi

python3 - <<'PY'
import re
import subprocess

text = subprocess.check_output(["cmake", "--version"], text=True)
match = re.search(r"cmake version\s+(\d+)\.(\d+)\.(\d+)", text)
if not match:
    raise SystemExit("ERROR: could not parse CMake version")
version = tuple(int(part) for part in match.groups())
if version < (3, 22, 1):
    raise SystemExit(
        "ERROR: CMake >= 3.22.1 is required; found " + ".".join(map(str, version))
    )
PY

probe_dir="$(mktemp -d)"
trap 'rm -rf "$probe_dir"' EXIT
cat > "$probe_dir/cpp23_probe.cpp" <<'CPP'
#include <bit>
#include <cstdint>
#include <expected>
#include <utility>

enum class ProbeEnum : unsigned { value = 1 };

int main() {
    std::expected<int, int> value = 7;
    const auto swapped = std::byteswap(std::uint32_t{0x01020304u});
    const auto underlying = std::to_underlying(ProbeEnum::value);
    return (!value.has_value() || swapped == 0u || underlying != 1u) ? 1 : 0;
}
CPP

if ! c++ -std=c++23 "$probe_dir/cpp23_probe.cpp" -o "$probe_dir/cpp23_probe"; then
  echo "ERROR: host C++ compiler/standard library lacks the required C++23 surface." >&2
  echo "Required: std::expected, std::byteswap, std::to_underlying." >&2
  echo "Use a newer Ubuntu/WSL x64 environment (Ubuntu 24.04 LTS recommended)." >&2
  exit 1
fi

"$probe_dir/cpp23_probe"

cmake_version="$(cmake --version | head -n 1)"
cxx_version="$(c++ --version | head -n 1)"
python_version="$(python3 --version 2>&1)"

echo
printf '%s\n' "Phase-2 self-hosted host bootstrap: PASS"
printf '%s\n' "OS: $(uname -s)"
printf '%s\n' "ARCH: $(uname -m)"
printf '%s\n' "CMake: $cmake_version"
printf '%s\n' "C++: $cxx_version"
printf '%s\n' "Python: $python_version"
printf '%s\n' "C++23 capability probe: PASS"
echo
printf '%s\n' "Next steps:"
printf '%s\n' "1. In GitHub: Settings -> Actions -> Runners -> New self-hosted runner."
printf '%s\n' "2. Select Linux / x64 and run GitHub's registration commands here."
printf '%s\n' "3. Start the runner and confirm it is Online/Idle."
printf '%s\n' "4. Dispatch 'DMC Native Reader core architecture' with runner=self-hosted."
printf '%s\n' "5. Do not commit runner tokens, credentials, or _work/_diag directories."
