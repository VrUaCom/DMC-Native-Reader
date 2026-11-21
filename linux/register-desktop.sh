#!/usr/bin/env bash
# Registers or unregisters DMC Native Reader desktop integration on Linux
# (MIME types, file associations, application menu launcher).
# Per-user installation ($HOME/.local/share), no root/sudo needed.
#
# Usage:
#   linux/register-desktop.sh            # Install desktop launcher & MIME associations
#   linux/register-desktop.sh --remove   # Uninstall desktop launcher & MIME associations

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"

apps_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mime_dir="${XDG_DATA_HOME:-$HOME/.local/share}/mime/packages"
icons_dir="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/128x128/apps"
pixmaps_dir="${XDG_DATA_HOME:-$HOME/.local/share}/pixmaps"
bin_dir="${HOME}/.local/bin"

if [ "${1:-}" = "--remove" ]; then
    echo "== Removing DMC Native Reader desktop integration..."
    rm -f "$apps_dir/dmc-native-reader.desktop"
    rm -f "$mime_dir/dmc-native-reader-mime.xml"
    rm -f "$icons_dir/dmc-native-reader.png"
    rm -f "$pixmaps_dir/dmc-native-reader.png"
    rm -f "$bin_dir/dmc_native_reader_linux"
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$apps_dir" 2>/dev/null || true
    fi
    if command -v update-mime-database >/dev/null 2>&1; then
        update-mime-database "${XDG_DATA_HOME:-$HOME/.local/share}/mime" 2>/dev/null || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f -t "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor" 2>/dev/null || true
    fi
    echo "== Uninstalled."
    exit 0
fi

echo "== Installing DMC Native Reader desktop integration..."
mkdir -p "$apps_dir" "$mime_dir" "$icons_dir" "$pixmaps_dir" "$bin_dir"

# Symlink or copy desktop entry, adjusting Exec path if local binary or AppImage exists
target_bin="$here/.build/bin/dmc_native_reader_linux"
if [ -f "$repo/dist/DMC_Native_Reader-x86_64.AppImage" ]; then
    target_bin="$repo/dist/DMC_Native_Reader-x86_64.AppImage"
elif [ ! -f "$target_bin" ]; then
    if [ -f "$repo/build/dmc_native_reader_linux" ]; then
        target_bin="$repo/build/dmc_native_reader_linux"
    elif [ -f "$here/.build/AppDir/AppRun" ]; then
        target_bin="$here/.build/AppDir/AppRun"
    fi
fi

if [ -f "$target_bin" ]; then
    ln -sf "$target_bin" "$bin_dir/dmc_native_reader_linux"
    echo "Linked binary to $bin_dir/dmc_native_reader_linux"
fi

# Install icon
if [ -f "$here/dmc-native-reader.png" ]; then
    cp "$here/dmc-native-reader.png" "$icons_dir/dmc-native-reader.png"
    cp "$here/dmc-native-reader.png" "$pixmaps_dir/dmc-native-reader.png"
    echo "Installed application icon to $icons_dir/dmc-native-reader.png"
fi

cp "$here/dmc-native-reader-mime.xml" "$mime_dir/"
cp "$here/dmc-native-reader.desktop" "$apps_dir/"

if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database "${XDG_DATA_HOME:-$HOME/.local/share}/mime" 2>/dev/null || true
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$apps_dir" 2>/dev/null || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor" 2>/dev/null || true
fi

echo "== Successfully registered DMC Native Reader desktop application and MIME types."
