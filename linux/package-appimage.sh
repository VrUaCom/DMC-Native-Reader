#!/usr/bin/env bash
# Packages DMC Native Reader as a standalone, portable AppImage for Linux.
# Assembles AppDir with:
#   - AppRun entrypoint script
#   - dmc-native-reader.desktop
#   - dmc-native-reader.png (icon)
#   - usr/bin/dmc_native_reader_linux
#   - usr/bin/dmc_native_reader_cli
#   - usr/share/applications, usr/share/icons, usr/share/mime
#
# Usage:
#   linux/package-appimage.sh              # Build release & produce DMC_Native_Reader-x86_64.AppImage
#   linux/package-appimage.sh --no-build   # Package existing binaries without rebuilding
#   linux/package-appimage.sh --appdir-only# Only prepare AppDir structure
#
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
build="$here/.build"
appdir="$build/AppDir"
out_dir="$repo/dist"
out_appimage="$out_dir/DMC_Native_Reader-x86_64.AppImage"
out_tar="$out_dir/DMC_Native_Reader-x86_64.tar.gz"

NO_BUILD=0
APPDIR_ONLY=0

for arg in "$@"; do
    case "$arg" in
        --no-build) NO_BUILD=1 ;;
        --appdir-only) APPDIR_ONLY=1 ;;
        -h|--help)
            echo "Usage: $0 [--no-build] [--appdir-only]"
            exit 0
            ;;
    esac
done

if [ "$NO_BUILD" -eq 0 ]; then
    echo "== Building DMC Native Reader release binaries..."
    "$here/build-linux.sh"
fi

bin_linux="$build/bin/dmc_native_reader_linux"
bin_cli="$build/bin/dmc_native_reader_cli"

if [ ! -f "$bin_cli" ]; then
    # Fallback search in build root
    if [ -f "$build/dmc_native_reader_cli" ]; then
        bin_cli="$build/dmc_native_reader_cli"
    fi
fi

if [ ! -f "$bin_cli" ]; then
    echo "error: required binary dmc_native_reader_cli not found in $build" >&2
    exit 1
fi

echo "== Assembling AppDir structure at $appdir..."
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin"
mkdir -p "$appdir/usr/lib"
mkdir -p "$appdir/usr/share/applications"
mkdir -p "$appdir/usr/share/icons/hicolor/128x128/apps"
mkdir -p "$appdir/usr/share/mime/packages"
mkdir -p "$out_dir"

# Copy binaries
if [ -f "$bin_linux" ]; then
    cp -p "$bin_linux" "$appdir/usr/bin/dmc_native_reader_linux"
    chmod +x "$appdir/usr/bin/dmc_native_reader_linux"
fi
cp -p "$bin_cli" "$appdir/usr/bin/dmc_native_reader_cli"
chmod +x "$appdir/usr/bin/dmc_native_reader_cli"

# Copy metadata, desktop file, and icon
cp -p "$here/dmc-native-reader.desktop" "$appdir/dmc-native-reader.desktop"
cp -p "$here/dmc-native-reader.desktop" "$appdir/usr/share/applications/dmc-native-reader.desktop"
cp -p "$here/dmc-native-reader.png" "$appdir/dmc-native-reader.png"
cp -p "$here/dmc-native-reader.png" "$appdir/.DirIcon"
cp -p "$here/dmc-native-reader.png" "$appdir/usr/share/icons/hicolor/128x128/apps/dmc-native-reader.png"
cp -p "$here/dmc-native-reader-mime.xml" "$appdir/usr/share/mime/packages/dmc-native-reader-mime.xml"

# Copy or generate AppRun
if [ -f "$here/AppRun" ]; then
    cp -p "$here/AppRun" "$appdir/AppRun"
else
    cat <<'EOF' > "$appdir/AppRun"
#!/usr/bin/env bash
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH:-}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
if [ "$1" = "--cli" ]; then
    shift
    exec "${HERE}/usr/bin/dmc_native_reader_cli" "$@"
fi
exec "${HERE}/usr/bin/dmc_native_reader_linux" "$@"
EOF
fi
chmod +x "$appdir/AppRun"

echo "== AppDir created successfully."

if [ "$APPDIR_ONLY" -eq 1 ]; then
    echo "== Stopped at AppDir (--appdir-only requested)."
    exit 0
fi

# Build portable AppImage
HAVE_APPIMAGETOOL=0
APPIMAGETOOL=""

if command -v appimagetool >/dev/null 2>&1; then
    HAVE_APPIMAGETOOL=1
    APPIMAGETOOL="appimagetool"
elif [ -f "$build/tools/appimagetool" ] && [ -x "$build/tools/appimagetool" ]; then
    HAVE_APPIMAGETOOL=1
    APPIMAGETOOL="$build/tools/appimagetool"
elif [ "$(uname -s)" = "Linux" ] && (command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1); then
    echo "== Fetching appimagetool..."
    mkdir -p "$build/tools"
    TOOL_URL="https://github.com/AppImage/AppImageKit/releases/download/13/appimagetool-x86_64.AppImage"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$build/tools/appimagetool" "$TOOL_URL" || true
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$build/tools/appimagetool" "$TOOL_URL" || true
    fi
    if [ -f "$build/tools/appimagetool" ]; then
        chmod +x "$build/tools/appimagetool"
        HAVE_APPIMAGETOOL=1
        APPIMAGETOOL="$build/tools/appimagetool"
    fi
fi

if [ "$HAVE_APPIMAGETOOL" -eq 1 ]; then
    echo "== Running appimagetool..."
    ARCH=x86_64 "$APPIMAGETOOL" "$appdir" "$out_appimage"
    chmod +x "$out_appimage"
    echo "== Successfully generated AppImage: $out_appimage"
else
    echo "== Note: appimagetool not found on this host ($(uname -s)). Creating portable standalone archive..."
    tar -czf "$out_tar" -C "$build" AppDir
    echo "== Generated portable AppDir archive: $out_tar"
    echo "   On Linux x86_64, run: ARCH=x86_64 appimagetool $appdir $out_appimage"
fi
