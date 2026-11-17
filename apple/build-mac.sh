#!/usr/bin/env bash
# Builds DMC Native Reader for macOS (Apple silicon or Intel):
#   1. the C++ core and its C API with CMake (FFmpeg off: no game videos yet),
#   2. the SwiftUI shell with SwiftPM,
#   3. "DMC Native Reader.app" in apple/.build, signed for this Mac only.
#
#   apple/build-mac.sh            # release build, then the .app
#   apple/build-mac.sh --open     # ... and start it
#
# Needs the Xcode command line tools (xcode-select --install) and CMake
# (brew install cmake). Nothing outside apple/.build is written.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
build="$here/.build"
core="$build/core"
libs="$build/core-libs"
app="$build/DMC Native Reader.app"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

command -v cmake >/dev/null || { echo "CMake is missing: brew install cmake" >&2; exit 1; }
command -v swift >/dev/null || { echo "Swift is missing: xcode-select --install" >&2; exit 1; }

echo "== C++ core"
cmake -S "$repo/app/src/main/cpp" -B "$core" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DDMC_NATIVE_READER_FFMPEG=OFF \
    -DDMC_NATIVE_READER_BUILD_TESTS=OFF >/dev/null
cmake --build "$core" --target dmc_reader_capi -j "$jobs"

rm -rf "$libs" && mkdir -p "$libs"
for lib in dmc_reader_capi dmc_native_reader_core dmc_native_reader_rengine_viewer \
           dmc_rengine_reader_core dmc_native_reader_stb_vorbis; do
    found="$(find "$core" -name "lib$lib.a" -print -quit)"
    [ -n "$found" ] || { echo "lib$lib.a was not built" >&2; exit 1; }
    cp "$found" "$libs/"
done

echo "== SwiftUI shell"
swift build --package-path "$here" -c release
binary="$(swift build --package-path "$here" -c release --show-bin-path)/DMCNativeReader"

echo "== $app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$binary" "$app/Contents/MacOS/DMCNativeReader"
cp "$here/Info.plist" "$app/Contents/Info.plist"
codesign --force --sign - "$app" >/dev/null
echo "Built: $app"

if [ "${1:-}" = "--open" ]; then open "$app"; fi
