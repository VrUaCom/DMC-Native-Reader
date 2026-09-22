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
# Canonical Phase-2 toolchain values are owned by run_phase2_exact_head.py.
# This bootstrap imports those values instead of maintaining a second copy.

ANDROID_CMDLINE_TOOLS_REVISION="15859902"
ANDROID_CMDLINE_TOOLS_SHA256="4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583"

EXPECTED_HEAD=""
while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --expected-head)
      if [[ "$#" -lt 2 ]]; then
        echo "ERROR: --expected-head requires the reviewed candidate SHA." >&2
        exit 2
      fi
      EXPECTED_HEAD="${2,,}"
      shift 2
      ;;
    -h|--help)
      echo "Usage: $0 --expected-head <reviewed-candidate-head-sha>"
      exit 0
      ;;
    *)
      echo "ERROR: unknown argument: $1" >&2
      echo "Usage: $0 --expected-head <reviewed-candidate-head-sha>" >&2
      exit 2
      ;;
  esac
done

if [[ ! "$EXPECTED_HEAD" =~ ^[0-9a-f]{40}$ ]]; then
  echo "ERROR: --expected-head must be the full 40-hex reviewed candidate HEAD SHA." >&2
  exit 2
fi

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "ERROR: Phase-2 direct evidence requires Linux." >&2
  exit 1
fi

arch="$(uname -m)"
case "$arch" in
  x86_64|amd64) ;;
  *)
    echo "ERROR: Phase-2 direct evidence requires x86_64; found: $arch" >&2
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

initial_head="$(git rev-parse HEAD)"
if [[ "$initial_head" != "$EXPECTED_HEAD" ]]; then
  echo "ERROR: local checkout HEAD $initial_head != supplied reviewed candidate HEAD $EXPECTED_HEAD" >&2
  echo "Fetch/checkout the reviewed candidate HEAD before bootstrapping." >&2
  exit 1
fi

initial_dirty="$(git status --porcelain --untracked-files=all)"
if [[ -n "$initial_dirty" ]]; then
  echo "ERROR: worktree must be clean before Phase-2 bootstrap." >&2
  printf '%s\n' "$initial_dirty" >&2
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

mapfile -t phase2_contract < <(python3 -B - <<'PY'
import importlib.util
from pathlib import Path

path = Path("tools/run_phase2_exact_head.py").resolve()
spec = importlib.util.spec_from_file_location("dmc_phase2_exact_head", path)
if spec is None or spec.loader is None:
    raise SystemExit("ERROR: could not load canonical Phase-2 runner")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

print(module.EXPECTED_GRADLE)
print(module.EXPECTED_JAVA_MAJOR)
print(module.EXPECTED_NDK)
print(module.EXPECTED_ANDROID_PLATFORM)
print(module.EXPECTED_BUILD_TOOLS)
print(module.EXPECTED_ANDROID_CMAKE)
print(".".join(map(str, module.MIN_HOST_CMAKE)))
PY
)

if [[ "${#phase2_contract[@]}" -ne 7 ]]; then
  echo "ERROR: failed to import canonical Phase-2 toolchain contract." >&2
  exit 1
fi

EXPECTED_GRADLE="${phase2_contract[0]}"
EXPECTED_JAVA_MAJOR="${phase2_contract[1]}"
EXPECTED_NDK="${phase2_contract[2]}"
EXPECTED_PLATFORM="${phase2_contract[3]}"
EXPECTED_BUILD_TOOLS="${phase2_contract[4]}"
EXPECTED_ANDROID_CMAKE="${phase2_contract[5]}"
MIN_HOST_CMAKE="${phase2_contract[6]}"

python3 - "$MIN_HOST_CMAKE" <<'PY'
import re
import subprocess
import sys

minimum = tuple(int(part) for part in sys.argv[1].split("."))
text = subprocess.check_output(["cmake", "--version"], text=True)
match = re.search(r"cmake version\s+(\d+)\.(\d+)\.(\d+)", text)
if not match:
    raise SystemExit("ERROR: could not parse CMake version")
version = tuple(int(part) for part in match.groups())
if version < minimum:
    raise SystemExit(
        "ERROR: host CMake >= " + ".".join(map(str, minimum)) +
        " is required; found " + ".".join(map(str, version))
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

java_home=""
preferred_java_home="/usr/lib/jvm/java-17-openjdk-amd64"
if [[ -x "$preferred_java_home/bin/java" && -x "$preferred_java_home/bin/javac" ]]; then
  java_home="$preferred_java_home"
else
  while IFS= read -r candidate; do
    candidate_home="$(dirname "$(dirname "$candidate")")"
    candidate_major="$("$candidate_home/bin/java" -version 2>&1 | sed -n '1s/.*version "\([0-9][0-9]*\).*/\1/p')"
    if [[ "$candidate_major" == "$EXPECTED_JAVA_MAJOR" ]]; then
      java_home="$candidate_home"
      break
    fi
  done < <(find /usr/lib/jvm -type f -path '*/bin/javac' 2>/dev/null | sort)
fi

if [[ -z "$java_home" ]]; then
  echo "ERROR: installed JDK $EXPECTED_JAVA_MAJOR could not be located under /usr/lib/jvm." >&2
  exit 1
fi

java_major="$("$java_home/bin/java" -version 2>&1 | sed -n '1s/.*version "\([0-9][0-9]*\).*/\1/p')"
if [[ "$java_major" != "$EXPECTED_JAVA_MAJOR" ]]; then
  echo "ERROR: canonical Phase-2 build requires JDK $EXPECTED_JAVA_MAJOR; found major=$java_major" >&2
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

export JAVA_HOME="$java_home"
export PATH="$JAVA_HOME/bin:$PATH"

if [[ "$($GRADLE_HOME/bin/gradle --version | sed -n 's/^Gradle //p' | head -n 1)" != "$EXPECTED_GRADLE" ]]; then
  echo "ERROR: Gradle $EXPECTED_GRADLE bootstrap verification failed." >&2
  exit 1
fi

# Use a Phase-2-owned Android SDK by default. This prevents an unrelated global
# Android SDK / stale cmdline-tools/latest from influencing exact-head evidence.
ANDROID_SDK_ROOT="${PHASE2_ANDROID_SDK_ROOT:-$TOOL_ROOT/android-sdk}"
ANDROID_HOME="$ANDROID_SDK_ROOT"
mkdir -p "$ANDROID_SDK_ROOT/cmdline-tools"
SDKMANAGER="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"
CMDLINE_MARKER="$ANDROID_SDK_ROOT/cmdline-tools/.dmc-phase2-cli"
EXPECTED_CMDLINE_MARKER="$ANDROID_CMDLINE_TOOLS_REVISION:$ANDROID_CMDLINE_TOOLS_SHA256"
actual_cmdline_marker=""
if [[ -f "$CMDLINE_MARKER" ]]; then
  actual_cmdline_marker="$(cat "$CMDLINE_MARKER")"
fi

if [[ ! -x "$SDKMANAGER" || "$actual_cmdline_marker" != "$EXPECTED_CMDLINE_MARKER" ]]; then
  cmdline_zip="$probe_dir/commandlinetools-linux-${ANDROID_CMDLINE_TOOLS_REVISION}_latest.zip"
  cmdline_url="https://dl.google.com/android/repository/commandlinetools-linux-${ANDROID_CMDLINE_TOOLS_REVISION}_latest.zip"
  cmdline_unpack="$probe_dir/android-cmdline-tools"
  rm -rf "$cmdline_unpack"
  mkdir -p "$cmdline_unpack"
  curl -fsSL "$cmdline_url" -o "$cmdline_zip"
  printf '%s  %s\n' "$ANDROID_CMDLINE_TOOLS_SHA256" "$cmdline_zip" | sha256sum -c -
  unzip -q "$cmdline_zip" -d "$cmdline_unpack"
  rm -rf "$ANDROID_SDK_ROOT/cmdline-tools/latest"
  mv "$cmdline_unpack/cmdline-tools" "$ANDROID_SDK_ROOT/cmdline-tools/latest"
  printf '%s\n' "$EXPECTED_CMDLINE_MARKER" > "$CMDLINE_MARKER"
fi

export ANDROID_SDK_ROOT
export ANDROID_HOME
export PATH="$GRADLE_HOME/bin:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:$JAVA_HOME/bin:$PATH"

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

post_submodule_head="$(git rev-parse HEAD)"
if [[ "$post_submodule_head" != "$EXPECTED_HEAD" ]]; then
  echo "ERROR: repository HEAD changed during bootstrap: $EXPECTED_HEAD -> $post_submodule_head" >&2
  exit 1
fi
post_submodule_dirty="$(git status --porcelain --untracked-files=all)"
if [[ -n "$post_submodule_dirty" ]]; then
  echo "ERROR: bootstrap left the source checkout dirty after submodule initialization." >&2
  printf '%s\n' "$post_submodule_dirty" >&2
  exit 1
fi

ENV_FILE="$REPO_ROOT/build/phase2-self-hosted-env.sh"
mkdir -p "$(dirname "$ENV_FILE")"
{
  printf 'export JAVA_HOME=%q\n' "$JAVA_HOME"
  printf 'export ANDROID_SDK_ROOT=%q\n' "$ANDROID_SDK_ROOT"
  printf 'export ANDROID_HOME=%q\n' "$ANDROID_HOME"
  printf 'export GRADLE_HOME=%q\n' "$GRADLE_HOME"
  printf 'export PATH=%q\n' "$GRADLE_HOME/bin:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:$JAVA_HOME/bin:$PATH"
} > "$ENV_FILE"

head_sha="$(git rev-parse HEAD)"
if [[ "$head_sha" != "$EXPECTED_HEAD" ]]; then
  echo "ERROR: repository HEAD no longer matches supplied reviewed candidate HEAD." >&2
  exit 1
fi
cmake_version="$(cmake --version | head -n 1)"
cxx_version="$(c++ --version | head -n 1)"
python_version="$(python3 --version 2>&1)"
gradle_version="$($GRADLE_HOME/bin/gradle --version | sed -n 's/^Gradle //p' | head -n 1)"

echo
printf '%s\n' "Phase-2 direct-run bootstrap: PASS"
printf '%s\n' "HEAD: $head_sha"
printf '%s\n' "OS: $(uname -s)"
printf '%s\n' "ARCH: $(uname -m)"
printf '%s\n' "CMake: $cmake_version"
printf '%s\n' "C++: $cxx_version"
printf '%s\n' "Python: $python_version"
printf '%s\n' "Java: $EXPECTED_JAVA_MAJOR ($JAVA_HOME)"
printf '%s\n' "Gradle: $gradle_version"
printf '%s\n' "Android SDK: $ANDROID_SDK_ROOT"
printf '%s\n' "NDK: $EXPECTED_NDK"
printf '%s\n' "C++23 capability probe: PASS"
echo
printf '%s\n' "Direct exact-head evidence command:"
printf '  source %q\n' "$ENV_FILE"
printf '  python3 tools/run_phase2_exact_head.py --sdk %q --gradle %q --java %q --expected-head %q\n' \
  "$ANDROID_SDK_ROOT" "$GRADLE_HOME/bin/gradle" "$JAVA_HOME/bin/java" "$EXPECTED_HEAD"
echo
printf '%s\n' "Optional GitHub self-hosted registration can still be added later."
printf '%s\n' "Do not commit runner tokens, credentials, or local build outputs."
