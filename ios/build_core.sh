#!/bin/bash
# Builds DMCNativeReader::Core and its pinned DMCRengine::ReaderCore as static
# libraries for one Apple platform, from the canonical CMake target in
# app/src/main/cpp. Shells link the Core target; they never enumerate parser or
# renderer sources themselves (docs/MODULAR_SPIDER_V33.md).
#
# Runs as an Xcode pre-build phase, reading PLATFORM_NAME / ARCHS /
# IPHONEOS_DEPLOYMENT_TARGET from the build environment, or standalone:
#   PLATFORM_NAME=iphonesimulator ARCHS=arm64 ios/build_core.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM="${PLATFORM_NAME:-iphonesimulator}"   # iphoneos | iphonesimulator
ARCH_LIST="${ARCHS:-arm64}"
DEPLOYMENT="${IPHONEOS_DEPLOYMENT_TARGET:-16.0}"
BUILD="$ROOT/ios/build/cmake/$PLATFORM"
OUT="$ROOT/ios/build/core/$PLATFORM"

# Xcode runs build phases with a minimal PATH.
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake not found. Install it with: brew install cmake" >&2
    exit 1
fi
if [ ! -f "$ROOT/app/src/main/cpp/vendor/dmc-rengine-cpp/cmake/reader_core.cmake" ]; then
    echo "error: Rengine submodule missing. Run: git submodule update --init" >&2
    exit 1
fi

cmake -S "$ROOT/app/src/main/cpp" -B "$BUILD" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$PLATFORM" \
    -DCMAKE_OSX_ARCHITECTURES="${ARCH_LIST// /;}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DDMC_NATIVE_READER_BUILD_TESTS=OFF

cmake --build "$BUILD" --config Release \
    --target dmc_native_reader_core dmc_rengine_reader_core \
    -j "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "$OUT"
for lib in libdmc_native_reader_core.a libdmc_rengine_reader_core.a; do
    found="$(find "$BUILD" -name "$lib" -print -quit)"
    if [ -z "$found" ]; then
        echo "error: $lib was not produced" >&2
        exit 1
    fi
    cp "$found" "$OUT/$lib"
done
echo "Core static libraries for $PLATFORM ($ARCH_LIST): $OUT"
