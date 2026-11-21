#!/usr/bin/env bash
# Builds DMC Native Reader for Linux (x86_64, aarch64, etc.):
#   1. Native X11 desktop application: dmc_native_reader_linux
#   2. Headless CLI companion: dmc_native_reader_cli
#
# Usage:
#   linux/build-linux.sh                # build release binaries in linux/.build
#   linux/build-linux.sh --run [file]   # build and launch the desktop viewer
#   linux/build-linux.sh --cli [file]   # build and inspect file with CLI
#   linux/build-linux.sh --test         # build and run unit tests
#
# Requirements:
#   Ubuntu/Debian:
#     sudo apt install build-essential cmake libx11-dev libxext-dev
#   Fedora/RHEL:
#     sudo dnf install gcc-c++ cmake libX11-devel libXext-devel
#   Arch Linux:
#     sudo pacman -S base-devel cmake libx11 libxext
#
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
build="$here/.build"
bin_dir="$build/bin"
jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

command -v cmake >/dev/null || { echo "CMake is missing: please install cmake" >&2; exit 1; }

echo "== Configuring DMC Native Reader (Linux)"
mkdir -p "$build" "$bin_dir"

BUILD_TESTS="OFF"
if [ "${1:-}" = "--test" ]; then
    BUILD_TESTS="ON"
fi

cmake -S "$repo/app/src/main/cpp" -B "$build" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
    -DDMC_NATIVE_READER_FFMPEG="${DMC_FFMPEG:-OFF}" \
    -DDMC_NATIVE_READER_BUILD_TESTS="$BUILD_TESTS"

echo "== Building dmc_native_reader_linux and dmc_native_reader_cli (-j $jobs)"
cmake --build "$build" --target dmc_native_reader_linux dmc_native_reader_cli -j "$jobs"

cp "$build/dmc_native_reader_linux" "$bin_dir/"
cp "$build/dmc_native_reader_cli" "$bin_dir/"

echo "== Successfully built:"
echo "   $bin_dir/dmc_native_reader_linux"
echo "   $bin_dir/dmc_native_reader_cli"

if [ "${1:-}" = "--test" ]; then
    echo "== Running test suite"
    ctest --test-dir "$build" --output-on-failure
fi

if [ "${1:-}" = "--run" ]; then
    shift
    echo "== Launching dmc_native_reader_linux"
    "$bin_dir/dmc_native_reader_linux" "$@"
elif [ "${1:-}" = "--cli" ]; then
    shift
    "$bin_dir/dmc_native_reader_cli" inspect "$@"
fi
