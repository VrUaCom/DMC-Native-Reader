#!/usr/bin/env bash
set -euo pipefail

# Prepare an Ubuntu/WSL2 x64 checkout for the DMC Native Reader Phase-2
# exact-head evidence runner without GitHub-hosted Actions minutes.
#
# This script intentionally does NOT:
# - register a GitHub runner or handle runner registration tokens;
# - modify the vendored DMC Rengine submodule;
# - change product source/build policy.
#
# It installs only the host/toolchain prerequisites needed by the already
# canonical tools/run_phase2_exact_head.py path.

EXPECTED_GRADLE="9.5.0"
EXPECTED_NDK="30.0.16248370"
EXPECTED_PLATFORM="android-36"
EXPECTED_BUILD_TOOLS="36.0.0"
EXPECTED_ANDROID_CMAKE="3.22.1"
ANDROID_CMDLINE_TOOLS_REVISION="15859902"
ANDROID_CMDLINE_TOOLS_SHA256="4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583"

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

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "ERROR: bootstrap must run from the DMC-Native-Reader checkout." >&2
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
  openjdk-17-jdk-headless \
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
  javac
  java
  unzip
  zipinfo
  strings
  curl
  sha256sum
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
        "ERROR: host CMake >= 3.22.1 is required; found " + ".".join(map(str, version))
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

java_home="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")"
java_major="$(java -version 2>&1 | sed -n '1s/.*version "\([0-9][0-9]*\).*/\1/p')"
if [[ "$java_major" != "17" ]]; then
  echo "ERROR: canonical Phase-2 build requires JDK 17; found major=$java_major" >&2
  exit 1
fi

TOOL_ROOT="${PHASE2_TOOL_ROOT:-$HOME/.local/share/dmc-native-reader/phase2}"
mkdir -p "$TOOL_ROOT"

GRADLE_HOME="$TOOL_ROOT/gradle-$EXPECTED_GRADLE"
if [[ ! -x "$GRADLE_HOME/bin/gradle" ]]; then
  gradle_zip="$probe_dir/gradle-$EXPECTED_GRADLE-bin.zip"
  gradle_sha="$probe_dir/gradle-$EXPECTED_GRADLE-bin.zip.sha256"
  gradle_url="https://services.gradle.org/distributions/gradle-$EXPECTED_GRADLE-bin.zip"
  curl -fsSL "$gradle_url" -o "$gradle_zip"
  curl -fsSL "$gradle_url.sha256" -o "$gradle_sha"
  printf '%s  %s\n' "$(tr -d '[:space:]' < "$gradle_sha")" "$gradle_zip" | sha256sum -c -
  unzip -q "$gradle_zip" -d "$TOOL_ROOT"
fi

if [[ "$($GRADLE_HOME/bin/gradle --version | sed -n 's/^Gradle //p' | head -n 1)" != "$EXPECTED_GRADLE" ]]; then
  echo "ERROR: Gradle $EXPECTED_GRADLE bootstrap verification failed." >&2
  exit 1
fi

ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
ANDROID_HOME="$ANDROID_SDK_ROOT"
mkdir -p "$ANDROID_SDK_ROOT/cmdline-tools"
SDKMANAGER="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"

if [[ ! -x "$SDKMANAGER" ]]; then
  cmdline_zip="$probe_dir/commandlinetools-linux-${ANDROID_CMDLINE_TOOLS_REVISION}_latest.zip"
  cmdline_url="https://dl.google.com/android/repository/commandlinetools-linux-${ANDROID_CMDLINE_TOOLS_REVISION}_latest.zip"
  cmdline_unpack="$probe_dir/android-cmdline-tools"
  mkdir -p "$cmdline_unpack"
  curl -fsSL "$cmdline_url" -o "$cmdline_zip"
  printf '%s  %s\n' "$ANDROID_CMDLINE_TOOLS_SHA256" "$cmdline_zip" | sha256sum -c -
  unzip -q "$cmdline_zip" -d "$cmdline_unpack"
  rm -rf "$ANDROID_SDK_ROOT/cmdline-tools/latest"
  mv "$cmdline_unpack/cmdline-tools" "$ANDROID_SDK_ROOT/cmdline-tools/latest"
fi

export JAVA_HOME="$java_home"
export ANDROID_SDK_ROOT
export ANDROID_HOME
export PATH="$GRADLE_HOME/bin:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:$PATH"

yes | "$SDKMANAGER" --licenses >/dev/null || true
"$SDKMANAGER" \
  "platform-tools" \
  "platforms;$EXPECTED_PLATFORM" \
  "build-tools;$EXPECTED_BUILD_TOOLS" \
  "ndk;$EXPECTED_NDK" \
  "cmake;$EXPECTED_ANDROID_CMAKE"

for required_path in \
  "$ANDROID_SDK_ROOT/platform-tools" \
  "$ANDROID_SDK_ROOT/platforms/$EXPECTED_PLATFORM" \
  "$ANDROID_SDK_ROOT/build-tools/$EXPECTED_BUILD_TOOLS" \
  "$ANDROID_SDK_ROOT/ndk/$EXPECTED_NDK" \
  "$ANDROID_SDK_ROOT/cmake/$EXPECTED_ANDROID_CMAKE"; do
  if [[ ! -e "$required_path" ]]; then
    echo "ERROR: canonical Android component missing after install: $required_path" >&2
    exit 1
  fi
done

git submodule update --init --recursive

ENV_FILE="$REPO_ROOT/build/phase2-self-hosted-env.sh"
mkdir -p "$(dirname "$ENV_FILE")"
{
  printf 'export JAVA_HOME=%q\n' "$JAVA_HOME"
  printf 'export ANDROID_SDK_ROOT=%q\n' "$ANDROID_SDK_ROOT"
  printf 'export ANDROID_HOME=%q\n' "$ANDROID_HOME"
  printf 'export GRADLE_HOME=%q\n' "$GRADLE_HOME"
  printf 'export PATH=%q\n' "$GRADLE_HOME/bin:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:$PATH"
} > "$ENV_FILE"

head_sha="$(git rev-parse HEAD)"
cmake_version="$(cmake --version | head -n 1)"
cxx_version="$(c++ --version | head -n 1)"
python_version="$(python3 --version 2>&1)"
gradle_version="$($GRADLE_HOME/bin/gradle --version | sed -n 's/^Gradle //p' | head -n 1)"

echo
printf '%s\n' "Phase-2 self-hosted bootstrap: PASS"
printf '%s\n' "HEAD: $head_sha"
printf '%s\n' "OS: $(uname -s)"
printf '%s\n' "ARCH: $(uname -m)"
printf '%s\n' "CMake: $cmake_version"
printf '%s\n' "C++: $cxx_version"
printf '%s\n' "Python: $python_version"
printf '%s\n' "Java: 17"
printf '%s\n' "Gradle: $gradle_version"
printf '%s\n' "Android SDK: $ANDROID_SDK_ROOT"
printf '%s\n' "NDK: $EXPECTED_NDK"
printf '%s\n' "C++23 capability probe: PASS"
echo
printf '%s\n' "Direct exact-head evidence command:"
printf '  source %q\n' "$ENV_FILE"
printf '  python3 tools/run_phase2_exact_head.py --sdk %q --gradle %q --expected-head %q\n' \
  "$ANDROID_SDK_ROOT" "$GRADLE_HOME/bin/gradle" "$head_sha"
echo
printf '%s\n' "Optional GitHub self-hosted registration can still be added later."
printf '%s\n' "Do not commit runner tokens, credentials, or local build outputs."
